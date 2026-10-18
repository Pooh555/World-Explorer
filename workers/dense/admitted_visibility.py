"""Read a pinned native pose-admission receipt; no solver or private map fallback."""

import io
from pathlib import Path
import struct

from cached_sources import checked_path
from pilot import parse, read_pinned


def load_admitted_cache(root, receipt_sha256, cancelled=lambda: False):
    import numpy as np
    from PIL import Image
    root = Path(root).resolve(strict=True)
    receipt = parse(read_pinned(checked_path(root, 'manifest.json'), receipt_sha256, 1024**2))
    if (type(receipt['schema_version']) is not int or receipt['schema_version'] != 1 or receipt['kind'] != 'POSE_ADMITTED_DEPTH_VISIBILITY_V1' or
            receipt['units'] != 'ARBITRARY_POSE_CAMERA_UNITS' or receipt['depth_convention'] != 'AXIAL_CAMERA_Z' or
            receipt['pixel_centers'] != 'integer' or receipt['camera_axes'] != '+x_right,+y_down,+z_forward'):
        raise ValueError('Unsupported native visibility receipt')
    if any(type(receipt[key]) is not int or not 0 <= receipt[key] < 2**64 for key in ('world_epoch', 'pose_revision')):
        raise ValueError('Invalid admitted pose revision')
    frames, files = receipt['frames'], receipt['files']
    if (not isinstance(frames, list) or not 1 <= len(frames) <= 18 or
            not isinstance(files, dict) or len(files) != 4+2*len(frames)):
        raise ValueError('Invalid admitted visibility inventory')
    required = {'poses.weposes', 'dense-inputs.json', 'native-recipe.json', 'depth-recipe.json'}
    for frame in frames:
        required.update((frame['image'], frame['depth_asset']))
    if set(files) != required:
        raise ValueError('Visibility file inventory differs')
    total = 0
    for name, record in files.items():
        if (set(record) != {'bytes', 'sha256'} or type(record['bytes']) is not int or
                not 0 <= record['bytes'] <= 256*1024**2):
            raise ValueError('Invalid visibility file byte budget')
        path = checked_path(root, name)
        if path.stat().st_size != record['bytes']:
            raise ValueError('Visibility file size differs')
        total += record['bytes']
    if total > 768*1024**2:
        raise ValueError('Visibility aggregate byte budget exceeded')
    def read(name):
        if cancelled():
            raise InterruptedError('Admitted visibility read cancelled')
        return read_pinned(checked_path(root, name), files[name]['sha256'], files[name]['bytes'])
    # Hash all dependencies, including the original pose snapshot and both recipes.
    read('poses.weposes')
    native_recipe = parse(read('native-recipe.json'))
    recipe = parse(read('depth-recipe.json'))
    manifest = parse(read('dense-inputs.json'))
    if (files['poses.weposes']['sha256'] != receipt['pose_asset_sha256'] or
            files['dense-inputs.json']['sha256'] != receipt['input_manifest_sha256'] or
            files['depth-recipe.json']['sha256'] != receipt['depth_recipe_sha256'] or
            native_recipe['input_profile'] != 'colmap_native_dense_input_v1' or
            recipe['input_profile'] != 'colmap_resized_dense_input_v1' or
            manifest['kind'] != 'DENSE_INPUTS_RESIZED_GRID' or
            manifest['pose_asset_sha256'] != receipt['pose_asset_sha256'] or
            manifest['depth_recipe_sha256'] != receipt['depth_recipe_sha256'] or
            manifest['world_epoch'] != receipt['world_epoch'] or manifest['pose_revision'] != receipt['pose_revision']):
        raise ValueError('Visibility dependency chain differs')
    indices = [frame['manifest_index'] for frame in frames]
    if (any(type(index) is not int for index in indices) or indices != sorted(set(indices)) or
            indices[0] < 0 or indices[-1] >= len(manifest['frames'])):
        raise ValueError('Invalid admitted frame order')
    pixels = 0
    for frame in frames:
        width, height = frame['width'], frame['height']
        if (type(width) is not int or type(height) is not int or not 1 <= width <= 4096 or
                not 1 <= height <= 4096 or width*height > 4096**2):
            raise ValueError('Invalid admitted depth grid')
        if (any(type(frame[key]) is not int or not 0 <= frame[key] < 2**64
                for key in ('frame', 'calibration', 'valid_pixels', 'unknown_pixels')) or
                not frame['frame'] or not frame['calibration']):
            raise ValueError('Invalid admitted frame/count identity')
        k = np.asarray(frame['intrinsics'], dtype=np.float64)
        if k.shape != (4,) or not np.isfinite(k).all() or np.any(k[:2] <= 0):
            raise ValueError('Invalid admitted intrinsics')
        pixels += width*height
    if pixels > 48_000_000:
        raise ValueError('Admitted decoded pixel budget exceeded')
    views = []
    for frame in frames:
        original = manifest['frames'][frame['manifest_index']]
        expected = dict(original)
        name = Path(original['image']).name
        expected.update(image='images/'+name, manifest_index=frame['manifest_index'],
                        depth_asset='assets/'+name+'.wedepth', depth_sha256=frame['depth_sha256'],
                        valid_pixels=frame['valid_pixels'], unknown_pixels=frame['unknown_pixels'], uncertainty=frame['uncertainty'])
        if (frame != expected or frame['depth_sha256'] != files[frame['depth_asset']]['sha256'] or
                frame['calibrated_image_sha256'] != files[frame['image']]['sha256']):
            raise ValueError('Admitted frame differs from checked dense domain')
        encoded = read(frame['depth_asset'])
        if len(encoded) < 356:
            raise ValueError('Truncated admitted depth header')
        fields = struct.unpack_from('<8s7I4Q4d', encoded)
        magic, version, convention, producer, scale, width, height, uncertainty = fields[:8]
        if (magic != b'WEXDEP01' or version != 1 or convention != 1 or producer != 2 or scale != 0 or
                (width, height) != (frame['width'], frame['height']) or uncertainty not in (0, 1) or
                list(fields[8:12]) != [receipt['world_epoch'], receipt['pose_revision'], frame['frame'], frame['calibration']] or
                list(fields[12:]) != frame['intrinsics'] or
                encoded[100:356].decode('ascii') != receipt['pose_asset_sha256']+frame['observation_image_sha256']+
                frame['calibrated_image_sha256']+receipt['depth_recipe_sha256']):
            raise ValueError('Admitted depth metadata differs')
        count = width*height
        if len(encoded) != 356+count*(17 if uncertainty else 13):
            raise ValueError('Admitted depth payload differs')
        depth = np.frombuffer(encoded, dtype='<f4', offset=356, count=count).reshape(height, width)
        valid = np.frombuffer(encoded, dtype=np.uint8, offset=356+count*4, count=count).reshape(height, width)
        confidence = np.frombuffer(encoded, dtype='<f4', offset=356+count*5, count=count).reshape(height, width)
        support = np.frombuffer(encoded, dtype='<u4', offset=356+count*9, count=count).reshape(height, width)
        sigma = np.frombuffer(encoded, dtype='<f4', offset=356+count*13, count=count).reshape(height, width) if uncertainty else None
        unknown = valid == 0
        positive = valid == 1
        planes = [depth, confidence]+([sigma] if uncertainty else [])
        if (not np.all(unknown | positive) or any(not np.isfinite(plane).all() for plane in planes) or
                np.any(depth[positive] <= 0) or np.any((confidence < 0) | (confidence > 1)) or
                np.any(support[positive] < 1) or np.any(support > 1_000_000) or
                any(np.any(plane[unknown] != 0) or np.signbit(plane[unknown]).any() for plane in planes) or
                np.any(support[unknown] != 0) or (uncertainty and np.any(sigma < 0)) or
                int(positive.sum()) != frame['valid_pixels'] or int(unknown.sum()) != frame['unknown_pixels'] or
                frame['uncertainty'] != ('CAMERA_UNIT_STANDARD_DEVIATION' if uncertainty else 'UNAVAILABLE')):
            raise ValueError('Admitted depth planes/summary differ')
        with Image.open(io.BytesIO(read(frame['image']))) as rgb:
            if rgb.format != 'PNG' or rgb.mode != 'RGB' or rgb.size != (width, height):
                raise ValueError('Admitted RGB8 domain differs')
            gray = np.asarray(rgb.convert('L'), dtype=np.float32)/255
        pose = frame['world_from_camera']
        if type(pose['scale']) not in (int, float) or pose['scale'] != 1:
            raise ValueError('Visibility reader requires explicit unit camera-to-world scale')
        q = np.array(pose['rotation_wxyz'], dtype=np.float64)
        translation = np.array(pose['translation'], dtype=np.float64)
        if q.shape != (4,) or translation.shape != (3,) or not np.isfinite(q).all() or not np.isfinite(translation).all() or abs(q@q-1) > 1e-8:
            raise ValueError('Invalid admitted camera pose')
        w, x, y, z = q
        rotation = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                             [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                             [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        views.append({'name': name, 'intrinsics': frame['intrinsics'], 'cam_from_world': np.column_stack((rotation.T, -rotation.T@translation)),
                      'depth': depth, 'valid': valid, 'confidence': confidence, 'support': support, 'sigma': sigma, 'gray': gray})
    return views, receipt
