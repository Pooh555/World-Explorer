#!/usr/bin/env python3
"""Bounded resized nine-view COLMAP CUDA development experiment, not a scene job."""

import argparse
import io
import json
from pathlib import Path
import signal
import sys
import time

from colmap_io import encode_depth, read_depth, read_support
from pilot import digest, parse, publish, read_pinned
from resize_inputs import transformed, validate_resize
from resized_model import adapt_model, neighbor_rows
from source_selection import DEFAULT_POLICY, plan_sources, report_digest


CONFIDENCE = 'consistent_source_fraction; not probability; sigma unavailable'


def worker_digest():
    directory=Path(__file__).parent
    files={name:digest((directory/name).read_bytes()) for name in
           ('neighbors.py','resized_model.py','colmap_io.py','pilot.py','resize_inputs.py','source_selection.py','plan_sources.py')}
    return digest(json.dumps(files,sort_keys=True,separators=(',', ':')).encode())


def validate_recipe(recipe):
    common = {'schema_version','input_profile','depth_producer','backend','scope',
              'selection_indices','patch_match','environment_sha256','worker_sha256','confidence','resize'}
    adaptive = recipe.get('scope') == 'PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION'
    extra = {'selection_policy','selection_sha256','neighbors'} if adaptive else {'neighbor_radius'}
    if (set(recipe) != common | extra or
            type(recipe['schema_version']) is not int or recipe['schema_version'] != 1 or
            recipe['input_profile'] != 'colmap_resized_dense_input_v1' or
            recipe['depth_producer'] != 'MULTIVIEW_STEREO' or
            recipe['backend'] != 'pycolmap-cuda12==4.2.1' or
            recipe['scope'] not in ('PARTIAL_NINE_VIEW_NEIGHBOR_QUALIFICATION',
                                    'PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION') or
            recipe['confidence'] != CONFIDENCE):
        raise ValueError('Unsupported resized neighbor recipe')
    if adaptive and (recipe['selection_policy'] != DEFAULT_POLICY or
                     not isinstance(recipe['selection_sha256'], str) or
                     len(recipe['selection_sha256']) != 64 or
                     any(c not in '0123456789abcdef' for c in recipe['selection_sha256'])):
        raise ValueError('Unsupported/unpinned adaptive source policy')
    validate_resize(recipe['resize'])
    return recipe['patch_match']


def prepare(args):
    recipe=parse(read_pinned(args.recipe,args.recipe_sha256,1024**2))
    settings=validate_recipe(recipe)
    if (recipe['worker_sha256'] != worker_digest() or
            recipe['environment_sha256'] != digest(Path(__file__).with_name('environment.txt').read_bytes())):
        raise ValueError('Selected neighbor implementation/environment differs')
    native=parse(read_pinned(args.native_manifest,args.native_manifest_sha256,1024**2))
    manifest=parse(read_pinned(args.manifest,args.manifest_sha256,1024**2))
    expected=transformed(native,args.native_manifest_sha256,recipe,args.recipe_sha256)
    if len(expected['frames']) != len(manifest['frames']):
        raise ValueError('Resized inventory differs')
    for frame,actual in zip(expected['frames'],manifest['frames']):
        frame['calibrated_image_sha256']=actual['calibrated_image_sha256']
    if expected != manifest:
        raise ValueError('Resized manifest does not match native inputs/selected recipe')
    if recipe['scope'] == 'PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION':
        selection = recipe['selection_indices']
        if (not isinstance(selection, list) or not 9 <= len(selection) <= 18 or
                any(type(i) is not int for i in selection) or selection != sorted(set(selection)) or
                selection[:9] != list(range(9)) or selection[-1] >= len(manifest['frames'])):
            raise ValueError('Invalid bounded reference/support inventory')
        rows = recipe['neighbors']
        names = [Path(manifest['frames'][i]['image']).name for i in selection]
        if not isinstance(rows, dict) or set(rows) != set(names):
            raise ValueError('Adaptive source graph is not closed')
        for name, sources in rows.items():
            if (not isinstance(sources, list) or not 2 <= len(sources) <= 6 or
                    any(not isinstance(v, str) for v in sources) or
                    len(set(sources)) != len(sources) or name in sources or not set(sources) <= set(names)):
                raise ValueError('Invalid bounded source row')
    else:
        rows=neighbor_rows(manifest['frames'],recipe['selection_indices'],recipe['neighbor_radius'])
    stage=args.stage.resolve(strict=True)
    completion=parse(read_pinned(stage/'completion.json',manifest['pose_completion_sha256'],16*1024**2))
    inventory={v['path']:v for v in completion['files']}
    if len(inventory) != len(completion['files']):
        raise ValueError('Duplicate native model inventory')
    if args.output.is_symlink():
        raise ValueError('Neighbor output cannot be a symlink')
    output=args.output.resolve(strict=False)
    inputs=[stage,args.manifest.resolve(strict=True).parent,args.native_manifest.resolve(strict=True),
            args.recipe.resolve(strict=True)]
    if any(output==p or output.is_relative_to(p) or p.is_relative_to(output) for p in inputs):
        raise ValueError('Neighbor workspace overlaps an input')
    if output.exists():
        raise ValueError('Neighbor workspace must be new')
    return recipe,settings,native,manifest,rows,stage,inventory,output


def run(args):
    recipe,settings,native,manifest,rows,stage,inventory,output=prepare(args)
    import importlib.metadata
    import socket
    import numpy as np
    import PIL
    from PIL import Image
    def no_network(*_args,**_kwargs):
        raise RuntimeError('Network is disabled in the dense worker')
    socket.socket=no_network
    import pycolmap
    if (pycolmap.__version__ != '4.2.1' or
            importlib.metadata.version('pycolmap-cuda12') != '4.2.1' or PIL.__version__ != '11.3.0'):
        raise RuntimeError('Pinned isolated dense environment required')
    if not args.prepare_only and not pycolmap.has_cuda:
        raise RuntimeError('CUDA-enabled COLMAP required for MVS; no fallback')
    expected=pycolmap.PatchMatchOptions().todict()
    expected.update(gpu_index='0',max_image_size=-1,cache_size=1.,num_threads=2,
                    write_consistency_graph=True)
    options=pycolmap.PatchMatchOptions(settings)
    if settings != expected or options.todict() != expected:
        raise ValueError('Complete fixed PatchMatch configuration required; filters unchanged')
    token=pycolmap.CancellationToken()
    def cancel(_signum,_frame):
        token.cancel()
    signal.signal(signal.SIGINT,cancel)
    signal.signal(signal.SIGTERM,cancel)
    if token.is_cancelled:
        return 130
    output.mkdir()
    for name in ('native','sparse','images','assets','stereo/depth_maps',
                 'stereo/normal_maps','stereo/consistency_graphs'):
        (output/name).mkdir(parents=True)
    for name in ('cameras.bin','images.bin','points3D.bin','rigs.bin','frames.bin'):
        key='training/sparse/'+name
        if key not in inventory:
            if name in ('rigs.bin','frames.bin'):
                continue
            raise ValueError('Missing native sparse model')
        entry=inventory[key]
        if name in ('cameras.bin','images.bin') and entry['sha256'] != manifest[name.replace('.bin','_sha256')]:
            raise ValueError('Native model dependency differs')
        path=stage/key
        if path.resolve(strict=True).relative_to(stage).as_posix() != key:
            raise ValueError('Native model path uses a symlink')
        if token.is_cancelled:
            return 130
        publish(output/'native'/name,read_pinned(path,entry['sha256'],256*1024**2))
    reconstruction=pycolmap.Reconstruction(output/'native')
    selection_report = None
    try:
        if recipe['scope'] == 'PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION':
            selection_report = plan_sources(reconstruction, native, recipe['selection_policy'],
                                            lambda: token.is_cancelled)
            if (report_digest(selection_report) != recipe['selection_sha256'] or
                    selection_report['selected_indices'] != recipe['selection_indices'] or
                    selection_report['neighbors'] != rows):
                raise ValueError('Frozen source selection differs from actual native model')
        adaptation=adapt_model(reconstruction,native,manifest,lambda:token.is_cancelled)
    except InterruptedError:
        return 130
    reconstruction.update_point_3d_errors()
    reconstruction.write_binary(output/'sparse')
    # Read back the actual backend files before admitting a prepared workspace.
    reread=pycolmap.Reconstruction(output/'sparse')
    by_name={image.name:image for image in reread.images.values()}
    for frame in manifest['frames']:
        image=by_name[Path(frame['image']).name];camera=reread.cameras[image.camera_id]
        matrix=camera.calibration_matrix()
        intrinsic=[matrix[0,0],matrix[1,1],matrix[0,2]-.5,matrix[1,2]-.5]
        if (camera.width != frame['width'] or camera.height != frame['height'] or
                not np.allclose(intrinsic,frame['intrinsics'],rtol=0,atol=1e-10)):
            raise ValueError('Serialized resized camera differs')
    selected=[manifest['frames'][i] for i in recipe['selection_indices']]
    for frame in selected:
        if token.is_cancelled:
            return 130
        path=args.manifest.resolve(strict=True).parent/frame['image']
        if path.resolve(strict=True).relative_to(args.manifest.resolve(strict=True).parent).as_posix() != frame['image']:
            raise ValueError('Resized image path uses a symlink')
        data=read_pinned(path,frame['calibrated_image_sha256'],64*1024**2)
        with Image.open(io.BytesIO(data)) as image:
            if image.format!='PNG' or image.mode!='RGB' or image.size!=(frame['width'],frame['height']):
                raise ValueError('Resized decoded RGB8 grid differs')
            image.load()
        publish(output/'images'/Path(frame['image']).name,data)
    config=''.join(name+'\n'+', '.join(sources)+'\n' for name,sources in rows.items())
    publish(output/'stereo/patch-match.cfg',config.encode())
    model=pycolmap.MVSModel();model.read(output,'COLMAP')
    indices={name:model.get_image_idx(name) for name in rows}
    prepared={'schema_version':1,'status':'PARTIAL_NEIGHBOR_INPUTS_PREPARED',
              'manifest_sha256':args.manifest_sha256,'recipe_sha256':args.recipe_sha256,
              'worker_sha256':recipe['worker_sha256'],'adaptation':adaptation,
              'neighbors':rows,'backend_indices':indices,
              'sparse_sha256':{p.name:digest(p.read_bytes()) for p in sorted((output/'sparse').iterdir()) if p.is_file()},
              'config_sha256':digest(config.encode()),'geometry_status':'UNAVAILABLE'}
    if selection_report is not None:
        publish(output/'selection.json', (json.dumps(selection_report, sort_keys=True, indent=2,
                                                    allow_nan=False)+'\n').encode())
        prepared['selection_sha256'] = recipe['selection_sha256']
        prepared['reference_indices'] = recipe['selection_policy']['reference_indices']
    if token.is_cancelled:
        return 130
    publish(output/'prepared.json',(json.dumps(prepared,sort_keys=True,indent=2,allow_nan=False)+'\n').encode())
    if args.prepare_only:
        return 0
    start=time.monotonic()
    pycolmap.patch_match_stereo(output,options=options,cancellation_token=token)
    if token.is_cancelled:
        return 130
    results=[]
    for frame in selected:
        name=Path(frame['image']).name;sources=rows[name]
        depth_file=output/'stereo/depth_maps'/(name+'.geometric.bin')
        graph_file=output/'stereo/consistency_graphs'/(name+'.geometric.bin')
        depth=read_depth(depth_file,frame['width'],frame['height'])
        support=read_support(graph_file,frame['width'],frame['height'],indices[name],
                             [indices[n] for n in sources])
        encoded,valid=encode_depth(frame,manifest,depth,support,source_count=len(sources))
        if token.is_cancelled:
            return 130
        asset='assets/'+name+'.wedepth'
        publish(output/asset,encoded)
        results.append({'frame':frame['frame'],'asset':asset,'sha256':digest(encoded),
                        'valid_pixels':valid,'unknown_pixels':frame['width']*frame['height']-valid,
                        'source_count':len(sources),
                        'native_depth_sha256':digest(depth_file.read_bytes()),
                        'native_graph_sha256':digest(graph_file.read_bytes())})
    report={'schema_version':1,'status':'PARTIAL_NEIGHBOR_PILOT_COMPLETE',
            'scope':'bounded resized views; no fusion, measured scale, room geometry or Q5/N2',
            'reference_indices':recipe.get('selection_policy', {}).get('reference_indices', recipe['selection_indices']),
            'manifest_sha256':args.manifest_sha256,'recipe_sha256':args.recipe_sha256,
            'worker_sha256':recipe['worker_sha256'],'elapsed_seconds':time.monotonic()-start,
            'prepared_sha256':digest((output/'prepared.json').read_bytes()),'frames':results}
    if token.is_cancelled:
        return 130
    publish(output/'result.json',(json.dumps(report,sort_keys=True,indent=2,allow_nan=False)+'\n').encode())
    return 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('manifest','native-manifest','stage','recipe','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    for name in ('manifest-sha256','native-manifest-sha256','recipe-sha256'):
        parser.add_argument('--'+name,required=True)
    parser.add_argument('--prepare-only',action='store_true')
    try:
        return run(parser.parse_args())
    except Exception as error:
        print('Dense neighbor experiment failed: '+str(error),file=sys.stderr)
        return 2


if __name__=='__main__':
    raise SystemExit(main())
