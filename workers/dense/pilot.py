#!/usr/bin/env python3
"""Isolated, explicitly partial COLMAP CUDA native-grid qualification pilot.

Not the application reconstruction job or a complete scene/geometry pipeline.
Requires a native application-prepared manifest and an exact selected recipe.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import signal
import sys
import tempfile
import time

from colmap_io import encode_depth, read_depth, read_support


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_pinned(path, expected, maximum):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
        raise ValueError('Missing, linked or oversized selected file')
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum or digest(data) != expected:
        raise ValueError('Selected file checksum/budget differs')
    return data


def publish(path, data):
    # Assets first, then a closed/flushed result manifest, all without replacement.
    staged = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.dense-', delete=False) as stream:
            staged = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(staged, path)
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    finally:
        if staged is not None:
            staged.unlink(missing_ok=True)


def worker_digest():
    directory = Path(__file__).parent
    files = {name: digest((directory / name).read_bytes()) for name in ['pilot.py', 'colmap_io.py']}
    return digest(json.dumps(files, sort_keys=True, separators=(',', ':')).encode())


def reject_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def parse(data):
    return json.loads(data, object_pairs_hook=reject_duplicates,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))


def validate_recipe(recipe):
    if (set(recipe) != {'schema_version', 'input_profile', 'depth_producer', 'backend',
                       'scope', 'selection_indices', 'patch_match', 'environment_sha256',
                       'worker_sha256', 'confidence'} or recipe['schema_version'] != 1 or
            recipe['input_profile'] != 'colmap_native_dense_input_v1' or
            recipe['depth_producer'] != 'MULTIVIEW_STEREO' or
            recipe['backend'] != 'pycolmap-cuda12==4.2.1' or
            recipe['scope'] != 'PARTIAL_THREE_VIEW_QUALIFICATION' or
            recipe['selection_indices'] != [0, 4, 8] or
            recipe['confidence'] != 'consistent_source_fraction; not probability; sigma unavailable'):
        raise ValueError('Unsupported dense pilot recipe')
    return recipe['patch_match']


def prepare(args):
    recipe_bytes = read_pinned(args.recipe, args.recipe_sha256, 1024**2)
    recipe = parse(recipe_bytes)
    options = validate_recipe(recipe)
    if recipe['worker_sha256'] != worker_digest():
        raise ValueError('Worker code differs from selected recipe')
    environment = Path(__file__).with_name('environment.txt')
    if recipe['environment_sha256'] != digest(environment.read_bytes()):
        raise ValueError('Selected worker requirement bytes differ')
    manifest = parse(read_pinned(args.manifest, args.manifest_sha256, 1024**2))
    if (manifest['schema_version'] != 1 or manifest['kind'] != 'DENSE_INPUTS_NATIVE_GRID' or
            manifest['depth_recipe_sha256'] != args.recipe_sha256 or
            manifest['units'] != 'ARBITRARY_POSE_CAMERA_UNITS' or
            manifest['depth_convention'] != 'AXIAL_CAMERA_Z' or
            manifest['preparation_recipe']['resize'] != 'none' or
            manifest['preparation_recipe']['crop'] != 'none' or
            not 9 <= len(manifest['frames']) <= 120):
        raise ValueError('Unsupported dense pilot input manifest')
    names = [frame['image'] for frame in manifest['frames']]
    if len(set(names)) != len(names) or names != sorted(names):
        raise ValueError('Duplicate/unordered dense image inventory')
    selected = [manifest['frames'][i] for i in recipe['selection_indices']]
    for frame in selected:
        width, height = frame['width'], frame['height']
        if (type(width) is not int or type(height) is not int or
                not 11 <= width <= 8192 or not 11 <= height <= 8192 or width*height > 4096**2):
            raise ValueError('Dense pixel grid exceeds pilot profile')
        name = frame['image']
        if (not name.startswith('training/images/') or len(Path(name).parts) != 3 or
                '\\' in name or ':' in name or Path(name).name in ('.', '..') or
                not name.endswith('.png')):
            raise ValueError('Unsafe calibrated image name')
    stage = args.stage.resolve(strict=True)
    completion_bytes = read_pinned(stage / 'completion.json', manifest['pose_completion_sha256'], 16*1024**2)
    completion = parse(completion_bytes)
    inventory = {v['path']: v for v in completion['files']}
    if len(inventory) != len(completion['files']):
        raise ValueError('Duplicate committed inventory')
    if args.output.is_symlink():
        raise ValueError('Dense pilot output cannot be a symlink')
    output = args.output.resolve(strict=False)
    inputs = [stage, args.manifest.resolve(strict=True), args.recipe.resolve(strict=True)]
    if any(output == p or output.is_relative_to(p) or p.is_relative_to(output) for p in inputs):
        raise ValueError('Dense pilot output overlaps an input')
    if output.exists() or output.is_symlink():
        raise ValueError('Dense pilot output must be new')
    return recipe, options, manifest, selected, stage, inventory, output


def run(args):
    recipe, settings, manifest, selected, stage, inventory, output = prepare(args)
    # Backends import only after checked selection. No remote fallback/downloads.
    import importlib.metadata
    import socket
    import numpy as np
    from PIL import Image
    def no_network(*_args, **_kwargs):
        raise RuntimeError('Network is disabled in the dense worker')
    socket.socket = no_network
    import pycolmap
    if pycolmap.__version__ != '4.2.1' or not pycolmap.has_cuda:
        raise RuntimeError('Pinned CUDA-enabled official COLMAP 4.2.1 required')
    if importlib.metadata.version('pycolmap-cuda12') != '4.2.1':
        raise RuntimeError('Unexpected COLMAP distribution')
    options = pycolmap.PatchMatchOptions(settings)
    expected = pycolmap.PatchMatchOptions().todict()
    expected.update(gpu_index='0', max_image_size=-1, cache_size=1., num_threads=2,
                    write_consistency_graph=True)
    if options.todict() != expected or settings != expected:
        raise ValueError('Pilot requires the complete fixed native-grid PatchMatch configuration')
    token = pycolmap.CancellationToken()
    def cancel(_signum, _frame):
        token.cancel()
    signal.signal(signal.SIGINT, cancel)
    signal.signal(signal.SIGTERM, cancel)
    output.mkdir()  # Exclusive ownership; failed/partial outputs stay inspectable.
    for name in ['images', 'sparse', 'assets', 'stereo/depth_maps',
                 'stereo/normal_maps', 'stereo/consistency_graphs']:
        (output / name).mkdir(parents=True)
    for name in ['cameras.bin', 'images.bin', 'points3D.bin', 'rigs.bin', 'frames.bin']:
        key = 'training/sparse/' + name
        if key not in inventory:
            if name in ('rigs.bin', 'frames.bin'):
                continue
            raise ValueError('Missing committed native model')
        entry = inventory[key]
        if key.endswith('cameras.bin') and entry['sha256'] != manifest['cameras_sha256']:
            raise ValueError('Camera dependency differs')
        if key.endswith('images.bin') and entry['sha256'] != manifest['images_sha256']:
            raise ValueError('Image model dependency differs')
        if (stage / key).resolve(strict=True).relative_to(stage).as_posix() != key:
            raise ValueError('Native model path uses a symlink')
        if token.is_cancelled:
            return 130
        data = read_pinned(stage / key, entry['sha256'], 256*1024**2)
        publish(output / 'sparse' / name, data)
    for frame in selected:
        if token.is_cancelled:
            return 130
        name = frame['image']
        path = stage / name
        if path.resolve(strict=True).relative_to(stage).as_posix() != name:
            raise ValueError('Calibrated image path uses a symlink')
        data = read_pinned(path, frame['calibrated_image_sha256'], 64*1024**2)
        with Image.open(io.BytesIO(data)) as image:
            if image.mode != 'RGB' or image.size != (frame['width'], frame['height']):
                raise ValueError('Calibrated RGB8 pixel grid differs')
            image.load()
        publish(output / 'images' / Path(name).name, data)
    reconstruction = pycolmap.Reconstruction(output / 'sparse')
    by_name = {image.name: image for image in reconstruction.images.values()}
    for frame in selected:
        image = by_name[Path(frame['image']).name]
        camera = reconstruction.cameras[image.camera_id]
        if camera.model_name not in ('PINHOLE', 'SIMPLE_PINHOLE'):
            raise ValueError('Dense model is not undistorted pinhole')
        k = camera.calibration_matrix()
        intrinsic = [k[0,0], k[1,1], k[0,2]-.5, k[1,2]-.5]
        if (camera.width != frame['width'] or camera.height != frame['height'] or
                not np.allclose(intrinsic, frame['intrinsics'], rtol=0, atol=1e-10)):
            raise ValueError('Dense model calibration differs from manifest')
        world_from_camera = image.cam_from_world().inverse().matrix().copy()
        world_from_camera[1:3] *= -1
        pose = frame['world_from_camera']
        q = pose['rotation_wxyz']
        expected_pose = np.column_stack((pycolmap.Rotation3d([q[1],q[2],q[3],q[0]]).matrix(), pose['translation']))
        if pose['scale'] != 1 or not np.allclose(world_from_camera, expected_pose, rtol=0, atol=1e-8):
            raise ValueError('Dense model pose differs from manifest')
    model = pycolmap.MVSModel()
    model.read(output, 'COLMAP')
    selected_names = [Path(f['image']).name for f in selected]
    indices = {name: model.get_image_idx(name) for name in selected_names}
    config = ''.join(name+'\n'+', '.join(n for n in selected_names if n != name)+'\n'
                     for name in selected_names)
    publish(output / 'stereo/patch-match.cfg', config.encode())
    if token.is_cancelled:
        return 130
    start = time.monotonic()
    pycolmap.patch_match_stereo(output, options=options, cancellation_token=token)
    if token.is_cancelled:
        return 130
    results = []
    for frame in selected:
        name = Path(frame['image']).name
        depth_file = output / 'stereo/depth_maps' / (name+'.geometric.bin')
        graph_file = output / 'stereo/consistency_graphs' / (name+'.geometric.bin')
        depth = read_depth(depth_file, frame['width'], frame['height'])
        support = read_support(graph_file, frame['width'], frame['height'], indices[name],
                               [indices[n] for n in selected_names if n != name])
        encoded, valid = encode_depth(frame, manifest, depth, support)
        if token.is_cancelled:
            return 130
        asset = name+'.wedepth'
        publish(output / 'assets' / asset, encoded)
        results.append({'frame': frame['frame'], 'asset': 'assets/'+asset,
                        'sha256': digest(encoded), 'valid_pixels': valid,
                        'unknown_pixels': frame['width']*frame['height']-valid,
                        'native_depth_sha256': digest(depth_file.read_bytes()),
                        'native_graph_sha256': digest(graph_file.read_bytes())})
    report = {'schema_version': 1, 'status': 'PARTIAL_PILOT_COMPLETE',
              'scope': 'three native calibrated views; no fusion, measured scale or geometry gate',
              'manifest_sha256': args.manifest_sha256, 'recipe_sha256': args.recipe_sha256,
              'worker_sha256': recipe['worker_sha256'], 'selected_images': selected_names,
              'selected_backend_indices': indices, 'patch_match': options.todict(),
              'elapsed_seconds': time.monotonic()-start, 'frames': results}
    if token.is_cancelled:
        return 130
    publish(output / 'result.json', (json.dumps(report,sort_keys=True,indent=2,allow_nan=False)+'\n').encode())
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['manifest', 'stage', 'recipe', 'output']:
        parser.add_argument('--'+name, type=Path, required=True)
    for name in ['manifest-sha256', 'recipe-sha256']:
        parser.add_argument('--'+name, required=True)
    args = parser.parse_args()
    try:
        return run(args)
    except Exception as error:
        print('Dense pilot failed: '+str(error),file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
