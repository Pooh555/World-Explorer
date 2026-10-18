#!/usr/bin/env python3
"""Train/freeze/check stratified private-cache diagnostics; no GPU recipe or solve."""

import argparse
import copy
import json
from pathlib import Path
import signal
import sys

from cached_sources import load_cache, new_output
from pilot import digest, parse, publish, read_pinned
from source_selection import report_digest
from visibility import POLICY as SUPPORT_POLICY
from visibility import pixel_evidence, validate_views


POLICY = {
    'algorithm': 'DENSE_GRID_GREEDY_STRATIFIED_DIAGNOSTIC_V1',
    'grid': [128, 96],
    'tile_grid': [8, 6],
    'training_phase': .25,
    'evaluation_phase': .75,
    'margin_pixels': 6,
    'texture_variance_boundaries': [1e-4, 1e-3],
    'support': {key: copy.deepcopy(value) for key, value in SUPPORT_POLICY.items() if key not in ('algorithm', 'grid')},
    'objective': 'second_support_then_first_support_then_quantized_quality_then_canonical_index',
}
FAILURES = ('reference_unknown', 'projection_outside', 'source_unknown', 'back_behind',
            'roundtrip_disagreement', 'depth_disagreement', 'low_parallax',
            'patch_unknown_or_outside', 'reference_flat', 'source_flat', 'ncc_disagreement', 'supported')
TEXTURES = ('FLAT', 'LOW', 'HIGH')
MAX_REPORT_BYTES = 16*1024**2


def implementation_digest():
    root = Path(__file__).parent
    return report_digest({name: digest((root/name).read_bytes()) for name in
                          ('reliability.py', 'visibility.py', 'cached_sources.py', 'pilot.py',
                           'source_selection.py', 'colmap_io.py')})


def grid(shape, evaluation=False):
    import numpy as np
    height, width = shape
    # Half-cell phase separation must exceed one pixel on both axes. Never
    # silently deduplicate, shrink the grid or evaluate the training pixels.
    if height < 204 or width < 268:
        raise ValueError('Dense disjoint grids require at least 268 x 204 pixels')
    phase = POLICY['evaluation_phase'] if evaluation else POLICY['training_phase']
    x = np.floor(6+(np.arange(128)+phase)*(width-12)/128).astype(np.int64)
    y = np.floor(6+(np.arange(96)+phase)*(height-12)/96).astype(np.int64)
    x, y = np.meshgrid(x, y)
    return x.ravel(), y.ravel()


def validate_rows(views, rows):
    names = [view['name'] for view in views]
    if names != sorted(names) or set(rows) != set(names):
        raise ValueError('Closed source graph in canonical image order required')
    for name, row in rows.items():
        if (not isinstance(row, list) or not 2 <= len(row) <= 6 or len(set(row)) != len(row) or
                name in row or not set(row) <= set(names)):
            raise ValueError('Invalid bounded source graph')


def strata(reference, x, y):
    import numpy as np
    patch = np.stack([reference['gray'][y+dy, x+dx] for dy in (-2, 0, 2) for dx in (-2, 0, 2)]).astype(np.float64)
    patch -= patch.mean(axis=0)
    variance = (patch**2).mean(axis=0)
    texture = np.searchsorted(POLICY['texture_variance_boundaries'], variance, side='right')
    tiles = np.repeat(np.arange(96)//16, 128)*8+np.tile(np.arange(128)//16, 96)
    return texture, tiles


def failures(details):
    import numpy as np
    policy = SUPPORT_POLICY
    conditions = (details['reference_known'], details['in_bounds'], details['source_known'],
                  details['back_positive'], details['roundtrip_pixels'] <= policy['roundtrip_pixels'],
                  details['relative_depth_error'] <= policy['relative_depth_error'],
                  details['angle_degrees'] >= policy['min_angle_degrees'], details['patch_inside'],
                  details['reference_variance'] >= policy['min_patch_variance'],
                  details['source_variance'] >= policy['min_patch_variance'], details['ncc'] >= policy['min_ncc'])
    reasons = np.full(len(details['good']), len(FAILURES)-1, dtype=np.uint8)
    remaining = np.ones(len(reasons), dtype=bool)
    for index, condition in enumerate(conditions):
        reasons[remaining & ~condition] = index
        remaining &= condition
    if not np.array_equal(remaining, details['good']):
        raise ValueError('Diagnostic failure accounting disagrees with support mask')
    return reasons


def pair_summary(details, texture):
    import numpy as np
    reasons = failures(details)
    def summarize(mask):
        known = mask & details['source_known'] & details['back_positive']
        relative = details['relative_depth_error'][known]
        errors = details['roundtrip_pixels'][known]
        counts = np.bincount(reasons[mask], minlength=len(FAILURES))
        return {'samples': int(mask.sum()), 'known_positive_back_samples': int(known.sum()),
                'relative_depth_p90': float(np.quantile(relative, .9)) if len(relative) else None,
                'roundtrip_median_pixels': float(np.median(errors)) if len(errors) else None,
                'first_failure_counts': dict(zip(FAILURES, map(int, counts)))}
    return {'all': summarize(np.ones(len(texture), dtype=bool)),
            'texture': {label: summarize(texture == index) for index, label in enumerate(TEXTURES)}}


def graph_summary(reference, rows, names, evidence, texture, tiles, x, y):
    import numpy as np
    counts = {stage: np.zeros(len(x), dtype=np.uint8) for stage in ('in_bounds', 'source_known', 'geometric', 'good')}
    for source in rows:
        details = evidence[names.index(source)][3]
        for stage in counts:
            counts[stage] += details[stage]
    def summarize(mask):
        total = int(mask.sum())
        good = int(np.count_nonzero(mask & (counts['good'] >= 2)))
        return {'samples': total, 'reference_known': int(np.count_nonzero(mask & (reference['depth'][y, x] > 0))),
                'two_in_bounds': int(np.count_nonzero(mask & (counts['in_bounds'] >= 2))),
                'two_known': int(np.count_nonzero(mask & (counts['source_known'] >= 2))),
                'two_geometric': int(np.count_nonzero(mask & (counts['geometric'] >= 2))),
                'two_supported': good, 'unsupported': total-good,
                'supported_fraction': good/total if total else None}
    return {'sources': rows, 'all': summarize(np.ones(len(x), dtype=bool)),
            'texture': {label: summarize(texture == index) for index, label in enumerate(TEXTURES)},
            'tiles': [{'index': tile, **summarize(tiles == tile)} for tile in range(48)],
            'tile_texture': [{'tile': tile, 'texture': label, **summarize((tiles == tile) & (texture == index))}
                             for tile in range(48) for index, label in enumerate(TEXTURES)]}


def inspect(views, baseline, candidate=None, evaluation=False, cancelled=lambda: False):
    import numpy as np
    validate_views(views)
    validate_rows(views, baseline)
    if evaluation:
        validate_rows(views, candidate)
    names = [view['name'] for view in views]
    rows, references, pairs = {}, [], {}
    # Training closes all auxiliary rows. Evaluation never ranks/selects sources.
    for ref, reference in enumerate(views[:9] if evaluation else views):
        x, y = grid(reference['depth'].shape, evaluation)
        texture, tiles = strata(reference, x, y)
        evidence = {}
        for source, view in enumerate(views):
            if source == ref:
                continue
            if cancelled():
                raise InterruptedError('Stratified source diagnostic cancelled')
            evidence[source] = pixel_evidence(reference, view, x, y)
            pairs[reference['name']+'->'+view['name']] = pair_summary(evidence[source][3], texture)
        if not evaluation:
            counts = np.zeros(len(x), dtype=np.uint8)
            candidates, selected = list(evidence), []
            while candidates and len(selected) < SUPPORT_POLICY['max_sources']:
                def rank(source):
                    good, quality = evidence[source][:2]
                    return (int(np.count_nonzero(good & (counts == 1))),
                            int(np.count_nonzero(good & (counts == 0))), int(quality.sum()), -source)
                best = max(candidates, key=rank)
                selected.append(best)
                candidates.remove(best)
                counts += evidence[best][0]
            rows[names[ref]] = [names[index] for index in selected]
        if ref < 9:
            temporal = names[max(0, ref-3):ref]+names[ref+1:min(9, ref+4)]
            graphs = {'cached_baseline': baseline[names[ref]], 'temporal': temporal,
                      'candidate': candidate[names[ref]] if evaluation else rows[names[ref]]}
            references.append({'image': names[ref], **{label: graph_summary(reference, sources, names, evidence,
                                                                            texture, tiles, x, y)
                                                       for label, sources in graphs.items()}})
    return {'neighbors': candidate if evaluation else rows, 'references': references, 'pairs': pairs}


def train(views, baseline, cache_sha256, cancelled=lambda: False):
    result = inspect(views, baseline, cancelled=cancelled)
    return {'schema_version': 1, 'kind': 'FROZEN_TRAINING_DIAGNOSTIC_NOT_GPU_RECIPE',
            'policy': copy.deepcopy(POLICY), 'cache_sha256': cache_sha256,
            'implementation_sha256': implementation_digest(), 'reference_indices': list(range(9)),
            'training': result, 'gpu_recipe_ready': False}


def evaluate(views, baseline, plan, cache_sha256, cancelled=lambda: False):
    if (plan.get('policy') != POLICY or plan.get('cache_sha256') != cache_sha256 or
            plan.get('implementation_sha256') != implementation_digest()):
        raise ValueError('Frozen training policy/cache/implementation differs')
    # Check actual training evidence/rows before reading any evaluation samples;
    # a self-consistently rehashed row or statistic cannot bypass recomputation.
    expected = train(views, baseline, cache_sha256, cancelled)
    if report_digest(plan) != report_digest(expected):
        raise ValueError('Frozen plan differs from actual training evidence')
    result = inspect(views, baseline, plan['training']['neighbors'], evaluation=True, cancelled=cancelled)
    return {'schema_version': 1, 'scope': 'STRATIFIED_PRIVATE_CACHE_NOT_GEOMETRY_OR_DENSE_YIELD',
            'policy': copy.deepcopy(POLICY), 'cache_sha256': cache_sha256,
            'implementation_sha256': implementation_digest(), 'reference_indices': list(range(9)),
            'evaluation': result, 'gpu_recipe_ready': False}


def run(args, cancelled=lambda: False):
    inputs = [args.workspace, args.cache]
    if args.command == 'check':
        inputs.append(args.plan)
    output = new_output(args.output, inputs)
    descriptor = parse(read_pinned(args.cache, args.cache_sha256, 4*1024**2))
    plan = parse(read_pinned(args.plan, args.plan_sha256, MAX_REPORT_BYTES)) if args.command == 'check' else None
    views, baseline = load_cache(args.workspace.resolve(strict=True), descriptor, cancelled)
    if args.command == 'plan':
        report = train(views, baseline, args.cache_sha256, cancelled)
    else:
        report = evaluate(views, baseline, plan, args.cache_sha256, cancelled)
        report['training_plan_sha256'] = args.plan_sha256
    encoded = (json.dumps(report, sort_keys=True, indent=2, allow_nan=False)+'\n').encode()
    if len(encoded) > MAX_REPORT_BYTES:
        raise ValueError('Stratified report byte budget exceeded')
    if cancelled():
        raise InterruptedError('Stratified report cancelled')
    publish(output, encoded)
    print('Stratified diagnostic SHA-256 '+digest(encoded))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('plan', 'check'):
        command = commands.add_parser(name)
        for option in ('workspace', 'cache', 'output'):
            command.add_argument('--'+option, type=Path, required=True)
        command.add_argument('--cache-sha256', required=True)
        if name == 'check':
            command.add_argument('--plan', type=Path, required=True)
            command.add_argument('--plan-sha256', required=True)
    try:
        args = parser.parse_args()
        import socket
        def no_network(*_args, **_kwargs):
            raise RuntimeError('Network is disabled in stratified diagnostics')
        socket.socket = no_network
        cancelled = [False]
        def cancel(_signum, _frame):
            cancelled[0] = True
        signal.signal(signal.SIGINT, cancel)
        signal.signal(signal.SIGTERM, cancel)
        return run(args, lambda: cancelled[0])
    except InterruptedError:
        return 130
    except Exception as error:
        print('Stratified source diagnostic failed: '+str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
