"""Synthetic dense-interchange/admission tests; no installed backend or GPU."""

from array import array
from pathlib import Path
import hashlib
import json
import math
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace

DIRECTORY = Path(__file__).resolve().parents[2] / 'workers/dense'
sys.path.insert(0, str(DIRECTORY))
import colmap_io
import pilot


class dense_test(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='we_dense_')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.w, self.h = 17, 11
        self.header = b'17&11&1&'

    def file(self, data, name='map.bin'):
        path = self.root / name
        path.write_bytes(data)
        return path

    def depth(self, values):
        return self.file(self.header + struct.pack('<'+'f'*len(values), *values))

    def graph(self, *values):
        return self.file(self.header + struct.pack('<'+'i'*len(values), *values))

    def test_depth_preserves_axial_off_axis_samples_and_zero_unknown(self):
        values = [4.] * (self.w*self.h)
        values[0] = 0
        result = colmap_io.read_depth(self.depth(values), self.w, self.h)
        self.assertEqual(list(result), values)
        self.assertEqual(result[150], 4)  # No conversion from Z to ray range.

    def test_depth_rejects_dimensions_truncation_trailing_and_nonfinite(self):
        data = self.depth([4.]*(self.w*self.h)).read_bytes()
        for malformed in [data[:-1], data+b'x', data.replace(b'17&',b'18&',1),
                          self.header + struct.pack('<f', math.nan)*self.w*self.h,
                          self.header + struct.pack('<f', math.inf)*self.w*self.h,
                          self.header + struct.pack('<f', -1)*self.w*self.h]:
            with self.subTest(length=len(malformed)):
                with self.assertRaises(ValueError):
                    colmap_io.read_depth(self.file(malformed),self.w,self.h)
        for w,h in [(True,11),(8193,11),(8192,8192),(0,11)]:
            with self.assertRaises(ValueError):
                colmap_io.read_depth(self.file(data),w,h)

    def test_graph_column_then_row_counts_actual_distinct_sources_plus_reference(self):
        graph = self.graph(15,3,2,1,2, 0,10,1,2)
        result = colmap_io.read_support(graph,self.w,self.h,0,[1,2])
        self.assertEqual(result[3*self.w+15],3)
        self.assertEqual(result[10*self.w],2)
        self.assertEqual(result[15*self.h+3],0)  # Would differ if row/column were swapped.
        self.assertEqual(sum(result),5)

    def test_graph_rejects_malformed_coordinates_counts_ids_and_duplicate_pixels(self):
        cases=[(-1,0,1,1),(17,0,1,1),(0,11,1,1),(0,0,-1),
               (0,0,0),(0,0,3,1,2,3),(0,0,1,0),(0,0,1,3),
               (0,0,2,1,1),(0,0,2,1),(0,0,1,1,0,0,1,2),(0,)]
        for values in cases:
            with self.subTest(values=values):
                with self.assertRaises(ValueError):
                    colmap_io.read_support(self.graph(*values),self.w,self.h,0,[1,2])
        with self.assertRaises(ValueError):
            colmap_io.read_support(self.file(self.header+b'x'),self.w,self.h,0,[1,2])
        for sources in [[],[1,1],[0,1],[-1,1],[True,2]]:
            with self.assertRaises(ValueError):
                colmap_io.read_support(self.graph(),self.w,self.h,0,sources)
        self.assertEqual(sum(colmap_io.read_support(self.graph(),self.w,self.h,0,[1,2])),0)

    def export_fixture(self):
        frame={'width':17,'height':11,'frame':2**53+1,'calibration':5,
               'intrinsics':[23,31,6.25,4.75],'observation_image_sha256':'b'*64,
               'calibrated_image_sha256':'c'*64}
        manifest={'world_epoch':7,'pose_revision':9,'pose_asset_sha256':'a'*64,'depth_recipe_sha256':'d'*64}
        depth=array('f',[0])*(17*11);support=array('I',[0])*(17*11)
        depth[7],support[7]=4,3
        depth[8],support[8]=4,2  # One source is insufficient for the fixed pilot.
        depth[9],support[9]=0,3  # No inferred depth from graph support.
        return frame,manifest,depth,support

    def test_independent_depth_wire_offsets_validity_support_and_unknown_normalization(self):
        frame,manifest,depth,support=self.export_fixture()
        encoded,valid=colmap_io.encode_depth(frame,manifest,depth,support,producer=1)
        self.assertEqual(valid,1)
        self.assertEqual(len(encoded),356+17*11*13)
        self.assertEqual(encoded[:8],b'WEXDEP01')
        self.assertEqual(struct.unpack_from('<I',encoded,16)[0],1) # Synthetic label.
        self.assertEqual(struct.unpack_from('<Q',encoded,52)[0],2**53+1)
        self.assertEqual(struct.unpack_from('<4d',encoded,68),(23,31,6.25,4.75))
        self.assertEqual(encoded[100:164],b'a'*64)
        n=17*11
        self.assertEqual(struct.unpack_from('<f',encoded,356+7*4)[0],4)
        self.assertEqual(encoded[356+n*4+7],1)
        self.assertEqual(struct.unpack_from('<f',encoded,356+n*5+7*4)[0],1)
        self.assertEqual(struct.unpack_from('<I',encoded,356+n*9+7*4)[0],3)
        for i in (0,8,9):
            self.assertEqual(struct.unpack_from('<f',encoded,356+i*4)[0],0)
            self.assertEqual(encoded[356+n*4+i],0)
            self.assertEqual(struct.unpack_from('<f',encoded,356+n*5+i*4)[0],0)
            self.assertEqual(struct.unpack_from('<I',encoded,356+n*9+i*4)[0],0)

    def test_export_rejects_metadata_numeric_and_support_inconsistency(self):
        for kind in ['depth','support','hash','intrinsic','frame']:
            frame,manifest,depth,support=self.export_fixture()
            if kind=='depth':depth[7]=math.nan
            if kind=='support':support[7]=4
            if kind=='hash':manifest['depth_recipe_sha256']='e'*63
            if kind=='intrinsic':frame['intrinsics'][0]=-1
            if kind=='frame':frame['frame']=True
            with self.subTest(kind=kind):
                with self.assertRaises(ValueError):colmap_io.encode_depth(frame,manifest,depth,support)

    def test_hash_checked_reads_preserve_inputs(self):
        path=self.file(b'caller data')
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertEqual(pilot.read_pinned(path,digest,11),b'caller data')
        for hash_value,limit in [('a'*64,11),(digest,10)]:
            with self.assertRaises(ValueError):pilot.read_pinned(path,hash_value,limit)
        self.assertEqual(path.read_bytes(),b'caller data')

    @unittest.skipIf(sys.platform=='win32','Linux-only pilot directory fsync')
    def test_publication_is_atomic_and_exclusive(self):
        path=self.file(b'caller data')
        with self.assertRaises(FileExistsError):pilot.publish(path,b'replacement')
        self.assertEqual(path.read_bytes(),b'caller data')
        self.assertFalse(list(self.root.glob('.dense-*')))
        new=self.root/'new';pilot.publish(new,b'complete')
        self.assertEqual(new.read_bytes(),b'complete')

    @unittest.skipIf(sys.platform=='win32','Linux-only pilot uses directory fsync and native CUDA wheel')
    def test_symlink_reads_are_rejected_without_touching_the_target(self):
        path=self.file(b'caller data');link=self.root/'linked'
        link.symlink_to(path)
        with self.assertRaises(ValueError):pilot.read_pinned(link,hashlib.sha256(b'caller data').hexdigest(),11)
        with self.assertRaises(ValueError):colmap_io.read_depth(link,17,11)
        self.assertEqual(path.read_bytes(),b'caller data')

    def request(self):
        stage = self.root/'stage'
        stage.mkdir()
        completion = json.dumps({'files': []}).encode()
        (stage/'completion.json').write_bytes(completion)
        recipe = {'schema_version':1,'input_profile':'colmap_native_dense_input_v1',
                  'depth_producer':'MULTIVIEW_STEREO','backend':'pycolmap-cuda12==4.2.1',
                  'scope':'PARTIAL_THREE_VIEW_QUALIFICATION','selection_indices':[0,4,8],
                  'patch_match':{},'environment_sha256':pilot.digest((DIRECTORY/'environment.txt').read_bytes()),
                  'worker_sha256':pilot.worker_digest(),
                  'confidence':'consistent_source_fraction; not probability; sigma unavailable'}
        recipe_path=self.file(json.dumps(recipe).encode(),'recipe.json')
        recipe_sha=pilot.digest(recipe_path.read_bytes())
        manifest={'schema_version':1,'kind':'DENSE_INPUTS_NATIVE_GRID','depth_recipe_sha256':recipe_sha,
                  'units':'ARBITRARY_POSE_CAMERA_UNITS','depth_convention':'AXIAL_CAMERA_Z',
                  'preparation_recipe':{'resize':'none','crop':'none'},
                  'frames':[{'width':17,'height':11,'image':f'training/images/{i}.png'} for i in range(9)],
                  'pose_completion_sha256':pilot.digest(completion)}
        manifest_path=self.file(json.dumps(manifest).encode(),'inputs.json')
        return SimpleNamespace(recipe=recipe_path,recipe_sha256=recipe_sha,manifest=manifest_path,
                               manifest_sha256=pilot.digest(manifest_path.read_bytes()),
                               stage=stage,output=self.root/'output')

    def test_prepare_checks_selected_grid_and_input_overlap_without_backend_imports(self):
        args=self.request()
        prepared=pilot.prepare(args)
        self.assertEqual(len(prepared[3]),3)
        self.assertFalse(args.output.exists())
        args.output=args.stage/'overlap'
        with self.assertRaises(ValueError):pilot.prepare(args)
        self.assertFalse(args.output.exists())
        manifest=json.loads(args.manifest.read_bytes());manifest['frames'][0]['width']=8193
        args.manifest.write_text(json.dumps(manifest));args.manifest_sha256=pilot.digest(args.manifest.read_bytes())
        with self.assertRaises(ValueError):pilot.prepare(args)

    @unittest.skipIf(sys.platform=='win32','Linux-only pilot; symlink creation requires local privileges on Windows')
    def test_prepare_rejects_dangling_output_symlink_without_creating_target(self):
        args=self.request();target=self.root/'untouched';args.output.symlink_to(target)
        with self.assertRaises(ValueError):pilot.prepare(args)
        self.assertTrue(args.output.is_symlink())
        self.assertFalse(target.exists())

    def test_recipe_and_json_admission_reject_ambiguity_before_backend_imports(self):
        for data in [b'{"x":1,"x":2}',b'{"x":NaN}',b'{"x":Infinity}']:
            with self.assertRaises(ValueError):pilot.parse(data)
        for value in [{}, {'schema_version':1}, {'schema_version':2}]:
            with self.assertRaises(ValueError):pilot.validate_recipe(value)
        first=pilot.worker_digest()
        self.assertEqual(len(first),64)
        self.assertEqual(first,pilot.worker_digest())


if __name__=='__main__':unittest.main()
