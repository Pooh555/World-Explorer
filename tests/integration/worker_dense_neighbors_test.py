"""Synthetic neighbor selection, support export and real pinned model adaptation."""

from array import array
import copy
import importlib.util
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

DIRECTORY=Path(__file__).resolve().parents[2]/'workers/dense'
sys.path.insert(0,str(DIRECTORY))
import colmap_io
import neighbors
import resized_model


class dense_neighbors_test(unittest.TestCase):
    def test_bounded_rows_are_symmetric_exclude_self_and_close_over_selected_views(self):
        frames=[{'image':f'training/images/{i:06}.png'} for i in range(63)]
        rows=resized_model.neighbor_rows(frames,list(range(9)))
        self.assertEqual([len(v) for v in rows.values()],[3,4,5,6,6,6,5,4,3])
        self.assertEqual(sum(map(len,rows.values())),42)
        for name,sources in rows.items():
            self.assertNotIn(name,sources)
            self.assertEqual(len(set(sources)),len(sources))
            for source in sources:self.assertIn(name,rows[source])
        for selection in ([],list(range(8)),[True]+list(range(1,9)),list(range(1,10,2)),
                          list(range(-1,8)),list(range(60,69)),[0]*9):
            with self.assertRaises(ValueError):resized_model.neighbor_rows(frames,selection)
        for radius in (True,0,2,4):
            with self.assertRaises(ValueError):resized_model.neighbor_rows(frames,list(range(9)),radius)

    def export_fixture(self):
        frame={'width':17,'height':11,'frame':7,'calibration':3,
               'intrinsics':[23,31,6.25,4.75],'observation_image_sha256':'b'*64,
               'calibrated_image_sha256':'c'*64}
        manifest={'world_epoch':1,'pose_revision':2,'pose_asset_sha256':'a'*64,'depth_recipe_sha256':'d'*64}
        depth=array('f',[4])*(17*11);support=array('I',[0])*(17*11)
        support[7]=3;support[8]=7;support[9]=2
        return frame,manifest,depth,support

    def test_support_fraction_uses_actual_reference_neighbor_count_and_preserves_counts(self):
        frame,manifest,depth,support=self.export_fixture()
        data,valid=colmap_io.encode_depth(frame,manifest,depth,support,source_count=6)
        n=17*11
        self.assertEqual(valid,2)
        self.assertEqual(struct.unpack_from('<I',data,356+n*9+7*4)[0],3)
        self.assertAlmostEqual(struct.unpack_from('<f',data,356+n*5+7*4)[0],1/3,places=6)
        self.assertEqual(struct.unpack_from('<f',data,356+n*5+8*4)[0],1)
        self.assertEqual(struct.unpack_from('<I',data,356+n*9+8*4)[0],7)
        self.assertEqual(data[356+n*4+9],0)
        self.assertEqual(struct.unpack_from('<f',data,356+9*4)[0],0)
        for count in (True,1,21,-1):
            with self.assertRaises(ValueError):colmap_io.encode_depth(frame,manifest,depth,support,source_count=count)
        with self.assertRaises(ValueError):colmap_io.encode_depth(frame,manifest,depth,support,source_count=3)

    def require_backend(self):
        if importlib.util.find_spec('pycolmap') is None:self.skipTest('Pinned isolated pycolmap required')
        import pycolmap
        if pycolmap.__version__!='4.2.1':self.skipTest('Pinned pycolmap 4.2.1 required')
        return pycolmap

    def model_fixture(self):
        pycolmap=self.require_backend()
        temp=tempfile.TemporaryDirectory(prefix='we_resized_model_');self.addCleanup(temp.cleanup)
        root=Path(temp.name)
        # Independent legacy COLMAP binary fixture: one SIMPLE_PINHOLE camera,
        # one asymmetric translated view, one observed 3D point and one untracked keypoint.
        (root/'cameras.bin').write_bytes(struct.pack('<QiiQQ3d',1,1,0,17,13,21,7.25,4.75))
        image=(struct.pack('<Qi7di',1,7,1,0,0,0,1,2,3,1)+b'000001.png\0'+
               struct.pack('<QddQddQ',2,9.35,8.95,7,12.25,8.75,2**64-1))
        (root/'images.bin').write_bytes(image)
        (root/'points3D.bin').write_bytes(struct.pack('<QQ3d3BdQii',1,7,0,0,7,31,73,127,0,1,7,0))
        model=pycolmap.Reconstruction(root)
        original={'image':'training/images/000001.png','width':17,'height':13,
                  'calibrated_image_sha256':'a'*64,'intrinsics':[21,21,6.75,4.25],
                  'backend_image_id':7,'backend_camera_id':1,
                  'world_from_camera':{'rotation_wxyz':[0,1,0,0],'translation':[-1,2,3],'scale':1}}
        frame=copy.deepcopy(original);frame.update(width=11,height=11,native_image_sha256='a'*64,
             calibrated_image_sha256='b'*64,intrinsics=[21*11/17,21*11/13,7.25*11/17-.5,4.75*11/13-.5])
        return root,model,{'frames':[original]},{'frames':[frame]}

    def test_actual_owned_model_roundtrip_promotes_focal_lengths_scales_tracks_and_preserves_geometry(self):
        pycolmap=self.require_backend();root,model,native,resized=self.model_fixture()
        original_pose=model.images[7].cam_from_world().matrix().copy()
        original_point=model.points3D[7].xyz.copy()
        result=resized_model.adapt_model(model,native,resized)
        self.assertEqual(result['rescaled_keypoints'],2)
        self.assertEqual(model.cameras[1].model_name,'PINHOLE')
        self.assertAlmostEqual(model.cameras[1].focal_length_x,21*11/17)
        self.assertAlmostEqual(model.cameras[1].focal_length_y,21*11/13)
        self.assertNotEqual(model.cameras[1].focal_length_x,model.cameras[1].focal_length_y)
        output=root/'adapted';output.mkdir();model.write_binary(output)
        read=pycolmap.Reconstruction(output)
        for actual,expected in zip(read.images[7].points2D[1].xy,[12.25*11/17,8.75*11/13]):
            self.assertAlmostEqual(actual,expected)
        self.assertEqual(read.images[7].points2D[0].point3D_id,7)
        self.assertEqual(len(read.points3D[7].track.elements),1)
        self.assertTrue((read.points3D[7].xyz==original_point).all())
        self.assertTrue((read.images[7].cam_from_world().matrix()==original_pose).all())
        projected=read.images[7].project_point(read.points3D[7].xyz)
        for actual,expected in zip(projected,read.images[7].points2D[0].xy):
            self.assertAlmostEqual(actual,expected)

    def test_model_rejects_wrong_calibration_pose_identity_grid_and_cancel(self):
        for kind in ('calibration','pose','identity','grid','dependency','cancel'):
            root,model,native,resized=self.model_fixture()
            frame=resized['frames'][0]
            if kind=='calibration':frame['intrinsics'][2]+=.25
            if kind=='pose':frame['world_from_camera']['translation'][0]+=1
            if kind=='identity':frame['backend_image_id']=8
            if kind=='grid':frame['width']=True
            if kind=='dependency':frame['native_image_sha256']='f'*64
            with self.subTest(kind=kind):
                with self.assertRaises((ValueError,InterruptedError)):
                    resized_model.adapt_model(model,native,resized,lambda:kind=='cancel')

    def test_recipe_rejects_unpinned_and_mislabelled_profile(self):
        for recipe in ({},{'schema_version':1},{'schema_version':True}):
            with self.assertRaises(ValueError):neighbors.validate_recipe(recipe)
        self.assertEqual(len(neighbors.worker_digest()),64)


if __name__=='__main__':unittest.main()
