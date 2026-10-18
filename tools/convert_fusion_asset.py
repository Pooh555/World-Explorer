#!/usr/bin/env python3
"""Convert pinned retained fusion arrays to uncompressed project interchange.

No solve, fusion, cleanup, quality promotion or scene publication. Requires NumPy
in the existing isolated fusion environment. Native admission is a separate step.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'workers' / 'dense'))
from pilot import parse, read_pinned, publish
from cached_sources import checked_path, new_output

MAX_BYTES = 256 * 1024**2
VOXEL_DTYPE = [('lattice', '<i4', (3,)), ('tsdf', '<f4'), ('weight', '<f4'),
               ('observation_count', '<u4'), ('support_sum', '<u4'), ('front_count', '<u4'),
               ('sigma2_sum', '<f8'), ('sigma_weight', '<f4')]


def arrays(data, specification):
    import numpy as np
    # Bound both ZIP expansion and NPY-declared allocations before NumPy loading.
    result = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        if (len(entries) != len(specification) or
                {entry.filename for entry in entries} != {key+'.npy' for key in specification} or
                sum(entry.file_size for entry in entries) > MAX_BYTES):
            raise ValueError('Invalid/oversized fusion array inventory')
        for entry in entries:
            if entry.file_size > MAX_BYTES or entry.flag_bits & 1:
                raise ValueError('Invalid fusion ZIP entry')
            encoded = archive.read(entry)
            stream = io.BytesIO(encoded)
            version = np.lib.format.read_magic(stream)
            if version != (1, 0):
                raise ValueError('Unsupported NPY version')
            shape, fortran, dtype = np.lib.format.read_array_header_1_0(stream)
            key = entry.filename[:-4]
            expected_dtype, expected_shape = specification[key]
            allowed = expected_dtype if isinstance(expected_dtype, tuple) else (expected_dtype,)
            if (fortran or dtype not in [np.dtype(item) for item in allowed] or
                    shape != expected_shape or
                    len(encoded)-stream.tell() != dtype.itemsize * math.prod(shape)):
                raise ValueError('Fusion array dtype/shape/bytes differ')
            result[key] = np.frombuffer(encoded, dtype=dtype, offset=stream.tell()).reshape(shape)
    return result


def convert(directory, manifest_sha, request_path, request_sha, destination):
    import numpy as np
    directory, request_path = Path(directory).resolve(strict=True), Path(request_path).absolute()
    manifest_bytes = read_pinned(checked_path(directory, 'manifest.json'), manifest_sha, 1024**2)
    manifest = parse(manifest_bytes)
    request_bytes = read_pinned(request_path, request_sha, 1024**2)
    request = parse(request_bytes)
    if (manifest['kind'] != 'PARTIAL_OBSERVED_TSDF_V1' or manifest['walking_enabled'] is not False or
            manifest['request_sha256'] != request_sha or request['schema_version'] != 1 or
            set(request) != {'schema_version', 'cache', 'recipe', 'receipt_sha256', 'recipe_sha256'}):
        raise ValueError('Invalid terminal fusion/request identity')
    recipe_path = request_path.parent / request['recipe']
    cache = (request_path.parent / request['cache']).resolve(strict=True)
    recipe_bytes = read_pinned(recipe_path, request['recipe_sha256'], 1024**2)
    receipt_bytes = read_pinned(checked_path(cache, 'manifest.json'), request['receipt_sha256'], 1024**2)
    if (manifest['receipt_sha256'] != request['receipt_sha256'] or
            manifest['recipe_sha256'] != request['recipe_sha256']):
        raise ValueError('Fusion dependency pins differ')
    output = new_output(Path(destination), [directory, request_path, recipe_path, cache])
    n, v, t = (manifest[key] for key in ('observed_voxels', 'vertices', 'triangles'))
    if any(type(c) is not int or c < 0 or c > cap
           for c, cap in zip((n, v, t), (16777216, 4000000, 8000000))):
        raise ValueError('Fusion counts exceed native bounds')
    metadata = json.dumps({'manifest': manifest_bytes.decode('utf-8'),
                           'receipt': receipt_bytes.decode('utf-8'),
                           'recipe': recipe_bytes.decode('utf-8'),
                           'request': request_bytes.decode('utf-8')},
                          sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
    total = 36 + len(metadata) + n*44 + v*24 + t*12
    if len(metadata) > 4*1024**2 or total > MAX_BYTES:
        raise ValueError('Native fusion output byte budget exceeded')
    specs = {key: (dtype, (n, 3) if key == 'lattice' else (n,))
             for key, dtype, *rest in VOXEL_DTYPE}
    mesh_specs = {'positions': ('<f4', (v, 3)), 'normals': ('<f4', (v, 3)),
                  'triangles': (('<i4', '<i8'), (t, 3))}
    if set(manifest['files']) != {'observed-voxels.npz', 'mesh.npz'}:
        raise ValueError('Fusion source file inventory differs')
    decoded = []
    for name, spec in [('observed-voxels.npz', specs), ('mesh.npz', mesh_specs)]:
        record = manifest['files'][name]
        if type(record['bytes']) is not int or not 0 <= record['bytes'] <= MAX_BYTES:
            raise ValueError('Fusion source byte count exceeds limit')
        data = read_pinned(checked_path(directory, name), record['sha256'], record['bytes'])
        if len(data) != record['bytes']:
            raise ValueError('Fusion source byte count differs')
        decoded.append(arrays(data, spec))
    voxels, mesh = decoded
    order = np.lexsort(tuple(voxels['lattice'][:, k] for k in (2, 1, 0)))
    packed = np.empty(n, dtype=VOXEL_DTYPE)
    for key in voxels:
        packed[key] = voxels[key][order]
    vertices = np.concatenate((mesh['positions'], mesh['normals']), axis=1).astype('<f4')
    triangles = mesh['triangles']
    if triangles.size and (triangles.min() < 0 or triangles.max() >= v):
        raise ValueError('Fusion indices exceed native vertex range')
    data = (struct.pack('<8sIQQQ', b'WEFUSN01', len(metadata), n, v, t) + metadata +
            packed.tobytes() + vertices.tobytes() + triangles.astype('<u4').tobytes())
    if len(data) != total:
        raise ValueError('Fusion encoded length differs')
    publish(output, data)
    return {'asset_sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'manifest_sha256': manifest_sha, 'receipt_sha256': request['receipt_sha256'],
            'recipe_sha256': request['recipe_sha256'], 'pose_sha256': manifest['pose_asset_sha256'],
            'scope': 'conversion only; native admission required'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    parser.add_argument('manifest_sha')
    parser.add_argument('request')
    parser.add_argument('request_sha')
    parser.add_argument('destination')
    args = parser.parse_args()
    try:
        print(json.dumps(convert(args.directory, args.manifest_sha, args.request,
                                 args.request_sha, args.destination), indent=2))
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
