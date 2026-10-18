"""Sampled cached-depth/image source scoring. No geometry admission or GPU calls."""

import copy


POLICY = {
    'algorithm': 'CACHED_VISIBILITY_NCC_GREEDY_V1',
    'grid': [32, 24],
    'patch_offsets': [-2, 0, 2],
    'max_sources': 6,
    'roundtrip_pixels': 1.0,
    'relative_depth_error': .01,
    'min_angle_degrees': 3.0,
    'min_ncc': .1,
    'min_patch_variance': 1e-4,
}
MAX_TOTAL_PIXELS = 48_000_000


def validate_views(views):
    import numpy as np
    if not 9 <= len(views) <= 18 or len({view['name'] for view in views}) != len(views):
        raise ValueError('Expected nine references and at most eighteen cached views')
    if sum(view['depth'].size for view in views) > MAX_TOTAL_PIXELS:
        raise ValueError('Decoded cache pixel budget exceeded')
    for view in views:
        depth, gray = view['depth'], view['gray']
        k, pose = np.asarray(view['intrinsics']), np.asarray(view['cam_from_world'])
        if (depth.ndim != 2 or gray.shape != depth.shape or depth.dtype != np.float32 or
                gray.dtype != np.float32 or not 72 <= depth.shape[0] <= 4096 or
                not 96 <= depth.shape[1] <= 4096 or not np.isfinite(depth).all() or
                not np.isfinite(gray).all() or np.any((gray < 0) | (gray > 1)) or
                k.shape != (4,) or not np.isfinite(k).all() or np.any(k[:2] <= 0) or
                pose.shape != (3, 4) or not np.isfinite(pose).all() or
                not np.allclose(pose[:, :3]@pose[:, :3].T, np.eye(3), rtol=0, atol=1e-8) or
                abs(np.linalg.det(pose[:, :3])-1) > 1e-8):
            raise ValueError('Invalid bounded cached optical camera/image/depth')


def sample_grid(shape, heldout=False):
    import numpy as np
    height, width = shape
    offset = .75 if heldout else .25
    x = np.floor(6+(np.arange(32)+offset)*(width-12)/32).astype(np.int64)
    y = np.floor(6+(np.arange(24)+offset)*(height-12)/24).astype(np.int64)
    x, y = np.meshgrid(x, y)
    return x.ravel(), y.ravel()


def bilinear(plane, x, y):
    """Out-of-domain and nonpositive depths stay unknown at the caller."""
    import numpy as np
    height, width = plane.shape
    valid = np.isfinite(x) & np.isfinite(y) & (x >= 0) & (y >= 0) & (x < width-1) & (y < height-1)
    # Never convert unbounded/nonfinite projected coordinates to integer indices.
    u, v = np.where(valid, x, 0), np.where(valid, y, 0)
    ix, iy = np.floor(u).astype(np.int64), np.floor(v).astype(np.int64)
    dx, dy = u-ix, v-iy
    taps = np.stack((plane[iy, ix], plane[iy, ix+1], plane[iy+1, ix], plane[iy+1, ix+1]))
    value = taps[0]*(1-dx)*(1-dy)+taps[1]*dx*(1-dy)+taps[2]*(1-dx)*dy+taps[3]*dx*dy
    return value, valid, taps


def pair_evidence(reference, source, heldout=False):
    """Integer centers, optical +Z, axial depth; sampled warp is not COLMAP NCC."""
    x, y = sample_grid(reference['depth'].shape, heldout)
    return pixel_evidence(reference, source, x, y)[:3]


def pixel_evidence(reference, source, x, y):
    """Bounded explicit pixel samples; expose diagnostic masks without changing V1."""
    import numpy as np
    height, width = reference['depth'].shape
    if (not isinstance(x, np.ndarray) or not isinstance(y, np.ndarray) or
            x.ndim != 1 or y.shape != x.shape or not 1 <= len(x) <= 12_288 or
            x.dtype.kind not in 'iu' or y.dtype.kind not in 'iu' or
            np.any((x < 2) | (x >= width-2) | (y < 2) | (y >= height-2))):
        raise ValueError('Invalid bounded diagnostic pixel inventory')
    x, y = x.astype(np.int64, copy=False), y.astype(np.int64, copy=False)
    offsets = np.array([(dx, dy) for dy in POLICY['patch_offsets'] for dx in POLICY['patch_offsets']])
    px, py = x[:, None]+offsets[:, 0], y[:, None]+offsets[:, 1]
    z = reference['depth'][py, px].astype(np.float64)
    fx, fy, cx, cy = reference['intrinsics']
    sfx, sfy, scx, scy = source['intrinsics']
    rp, sp = np.asarray(reference['cam_from_world']), np.asarray(source['cam_from_world'])
    with np.errstate(over='raise', invalid='raise', divide='raise'):
        p = np.stack(((px-cx)*z/fx, (py-cy)*z/fy, z), axis=-1)
        world = (p-rp[:, 3])@rp[:, :3]
        q = world@sp[:, :3].T+sp[:, 3]
        denominator = np.where(q[..., 2] > 0, q[..., 2], 1)
        u, v = sfx*q[..., 0]/denominator+scx, sfy*q[..., 1]/denominator+scy
        sd, inside, taps = bilinear(source['depth'], u, v)
        inside &= (z > 0) & (q[..., 2] > 0)
        known = inside & (taps > 0).all(axis=0)
        back_point = np.stack(((u-scx)*sd/sfx, (v-scy)*sd/sfy, sd), axis=-1)
        back = ((back_point-sp[:, 3])@sp[:, :3])@rp[:, :3].T+rp[:, 3]
        bz = np.where(back[..., 2] > 0, back[..., 2], 1)
        error = np.hypot(fx*back[..., 0]/bz+cx-px, fy*back[..., 1]/bz+cy-py)
        relative = np.abs(sd-q[..., 2])/denominator
        rc, sc = -rp[:, :3].T@rp[:, 3], -sp[:, :3].T@sp[:, 3]
        a, b = world[:, 4]-rc, world[:, 4]-sc
        sa, sb = np.max(np.abs(a), axis=1), np.max(np.abs(b), axis=1)
        a = a/np.where(sa > 0, sa, 1)[:, None]
        b = b/np.where(sb > 0, sb, 1)[:, None]
        angle = np.degrees(np.arctan2(np.linalg.norm(np.cross(a, b), axis=1), (a*b).sum(axis=1)))
        warped, rgb_inside, _ = bilinear(source['gray'], u, v)
        original = reference['gray'][py, px].astype(np.float64)
        original -= original.mean(axis=1, keepdims=True)
        warped -= warped.mean(axis=1, keepdims=True)
        rv, sv = (original**2).mean(axis=1), (warped**2).mean(axis=1)
        ncc = (original*warped).mean(axis=1)/np.sqrt(np.maximum(rv*sv, 1e-30))
        textured = (rv >= POLICY['min_patch_variance']) & (sv >= POLICY['min_patch_variance'])
        geometric = (known[:, 4] & (back[:, 4, 2] > 0) & (error[:, 4] <= POLICY['roundtrip_pixels']) &
                     (relative[:, 4] <= POLICY['relative_depth_error']) &
                     (angle >= POLICY['min_angle_degrees']))
        good = geometric & inside.all(axis=1) & rgb_inside.all(axis=1) & textured & (ncc >= POLICY['min_ncc'])
        # Quantized only for deterministic secondary ordering, never confidence.
        quality = np.where(known[:, 4] & textured & inside.all(axis=1),
                           np.maximum(0, ncc)/(1+error[:, 4]) * np.minimum(angle/3, 1), 0)
        quality = np.floor(np.clip(quality, 0, 1)*1_000_000).astype(np.int64)
    values = error[known[:, 4], 4]
    summary = {'in_bounds': int(inside[:, 4].sum()), 'known': int(known[:, 4].sum()),
               'geometric': int(geometric.sum()), 'textured': int(textured.sum()), 'good': int(good.sum()),
               'roundtrip_median_pixels': float(np.median(values)) if len(values) else None}
    details = {'reference_known': z[:, 4] > 0, 'in_bounds': inside[:, 4],
               'source_known': known[:, 4], 'back_positive': back[:, 4, 2] > 0,
               'roundtrip_pixels': error[:, 4], 'relative_depth_error': relative[:, 4],
               'angle_degrees': angle, 'patch_inside': inside.all(axis=1) & rgb_inside.all(axis=1),
               'reference_variance': rv, 'source_variance': sv, 'ncc': ncc,
               'geometric': geometric, 'good': good}
    return good, quality, summary, details


def score_sources(views, baseline_rows, cancelled=lambda: False):
    """Keep the entire cache inventory/first nine references; closed rows of 2–6."""
    import numpy as np
    validate_views(views)
    names = [view['name'] for view in views]
    if names != sorted(names) or set(baseline_rows) != set(names):
        raise ValueError('Closed baseline graph in canonical image order required')
    for ref, row in baseline_rows.items():
        if (not isinstance(row, list) or not 2 <= len(row) <= 6 or len(set(row)) != len(row) or
                ref in row or not set(row) <= set(names)):
            raise ValueError('Invalid baseline source row')
    rows, summaries, masks = {}, {}, {}
    for ref, reference in enumerate(views):
        evidence = {}
        for source, view in enumerate(views):
            if source == ref:
                continue
            if cancelled():
                raise InterruptedError('Cached source scoring cancelled')
            evidence[source] = pair_evidence(reference, view)
            summaries[names[ref]+'->'+names[source]] = evidence[source][2]
        counts = np.zeros(768, dtype=np.uint8)
        candidates, selected = list(evidence), []
        while candidates and len(selected) < POLICY['max_sources']:
            def rank(source):
                good, quality, _ = evidence[source]
                return (int(np.count_nonzero(good & (counts == 1))),
                        int(np.count_nonzero(good & (counts == 0))), int(quality.sum()), -source)
            best = max(candidates, key=rank)
            selected.append(best)
            candidates.remove(best)
            counts += evidence[best][0]
        rows[names[ref]] = [names[i] for i in selected]
        masks[ref] = evidence
    comparisons = []
    for ref in range(9):
        if cancelled():
            raise InterruptedError('Cached source scoring cancelled')
        name = names[ref]
        temporal = names[max(0, ref-3):ref]+names[ref+1:min(9, ref+4)]
        cases = {}
        for label, row in [('cached_baseline', baseline_rows[name]), ('temporal', temporal), ('candidate', rows[name])]:
            train = np.zeros(768, dtype=np.uint8)
            heldout = np.zeros(768, dtype=np.uint8)
            for source in row:
                index = names.index(source)
                train += masks[ref][index][0]
                # Same frozen maps/cameras/images for all graph comparisons.
                heldout += pair_evidence(views[ref], views[index], heldout=True)[0]
            cases[label] = {'sources': row, 'training_two_source_samples': int((train >= 2).sum()),
                            'heldout_two_source_samples': int((heldout >= 2).sum())}
        comparisons.append({'image': name, **cases})
    return {'schema_version': 1, 'scope': 'CACHED_SOURCE_DIAGNOSTIC_NOT_GEOMETRY_OR_DENSE_YIELD',
            'policy': copy.deepcopy(POLICY), 'reference_indices': list(range(9)),
            'neighbors': rows, 'pairs': summaries, 'reference_comparison': comparisons,
            'heldout_grid': 'disjoint deterministic pixel grid; same capture/cache, not unseen-scene validation',
            'gpu_recipe_ready': False}
