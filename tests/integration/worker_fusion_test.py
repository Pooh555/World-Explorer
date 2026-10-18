"""Synthetic fusion invariants; analytic surfaces are not Q5 ground truth."""

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'workers/fusion'))
from contract import selected_recipe, load_request, digest
from geometry import robust_extent, consistent_masks, integrate, observed_arrays, world_points


class fusion_contract_test(unittest.TestCase):
    def fixture(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        recipe = selected_recipe()
        (root/'recipe.json').write_text(json.dumps(recipe))
        request = {'schema_version': 1, 'recipe': 'recipe.json',
                   'recipe_sha256': digest((root/'recipe.json').read_bytes()), 'cache': 'cache', 'receipt_sha256': 'a'*64}
        (root/'request.json').write_text(json.dumps(request))
        return root, recipe, request

    def test_fixed_recipe_and_changed_code_identity_rejected(self):
        root, recipe, request = self.fixture()
        path = root/'request.json'
        self.assertEqual(load_request(path, digest(path.read_bytes()))[1], recipe)
        recipe['implementation_sha256'] = 'b'*64
        (root/'recipe.json').write_text(json.dumps(recipe))
        request['recipe_sha256'] = digest((root/'recipe.json').read_bytes())
        path.write_text(json.dumps(request))
        with self.assertRaisesRegex(ValueError, 'fixed pilot'):
            load_request(path, digest(path.read_bytes()))

    def test_rehashed_threshold_change_boolean_schema_and_unknown_key_reject(self):
        root, recipe, request = self.fixture()
        path = root/'request.json'
        recipe['voxel_divisor'] = 256
        (root/'recipe.json').write_text(json.dumps(recipe))
        request['recipe_sha256'] = digest((root/'recipe.json').read_bytes())
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError): load_request(path, digest(path.read_bytes()))
        request['schema_version'] = True
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError): load_request(path, digest(path.read_bytes()))
        request['schema_version'] = 1
        request['extra'] = 1
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError): load_request(path, digest(path.read_bytes()))


@unittest.skipUnless(importlib.util.find_spec('numpy'), 'Requires isolated NumPy')
class fusion_geometry_test(unittest.TestCase):
    def fixture(self):
        import numpy as np
        # Identical optical views of an asymmetric bounded plane. Translation and
        # a 90 degree rotation make world X=7 the analytic surface, not world Z.
        transform = np.array([[0., 0., -1., 3.], [0., 1., 0., -2.], [1., 0., 0., -5.]])
        view = {'name': 'synthetic', 'intrinsics': [45., 55., 29.25, 20.75], 'cam_from_world': transform,
                'depth': np.full((48, 64), 2., dtype=np.float32), 'valid': np.ones((48, 64), dtype=np.uint8),
                'confidence': np.full((48, 64), .25, dtype=np.float32),
                'support': np.full((48, 64), 3, dtype=np.uint32), 'sigma': None}
        view['valid'][:, 28:37] = 0
        view['depth'][:, 28:37] = 0
        view['confidence'][:, 28:37] = 0
        view['support'][:, 28:37] = 0
        return [copy.deepcopy(view) for _ in range(3)]

    def test_analytic_world_transform_extent_unknowns_and_weight_denominators(self):
        import numpy as np
        views = self.fixture()
        points = world_points(views[0], np.array([4, 40]), np.array([5, 30]), np.array([2., 2.]))
        self.assertTrue(np.allclose(points[:, 0], 7))
        self.assertAlmostEqual(points[0, 1], 2+(5-20.75)*2/55)
        self.assertAlmostEqual(points[0, 2], 3-(4-29.25)*2/45)
        views[0]['confidence'][3, 3] = 0
        masks, summary = consistent_masks(views, selected_recipe())
        self.assertFalse(masks[0][3, 3])
        for mask, row in zip(masks, summary):
            self.assertFalse(mask[:, 28:37].any())
            self.assertEqual(row['valid']+row['unknown'], row['pixels'])
            self.assertEqual(row['accepted']+row['rejected_positive_weight']+row['zero_confidence_valid'], row['valid'])
        extent, _, count = robust_extent(views)
        self.assertGreater(extent, 2)
        self.assertEqual(count, sum(int((v['confidence'] > 0).sum()) for v in views))

    def test_unknown_source_and_known_occluder_never_certify_support(self):
        views = self.fixture()
        views[1]['depth'][:] = 1
        masks, summary = consistent_masks(views, selected_recipe())
        self.assertFalse(masks[0].any())
        self.assertGreater(summary[0]['known_occlusion'], 0)
        views = self.fixture()
        views[2]['valid'][:] = 0
        masks, _ = consistent_masks(views, selected_recipe())
        self.assertFalse(masks[0].any())

    def test_cancellation_and_empty_extent_reject(self):
        views = self.fixture()
        with self.assertRaises(InterruptedError): consistent_masks(views, selected_recipe(), lambda: True)
        for view in views: view['confidence'][:] = 0
        with self.assertRaises(ValueError): robust_extent(views)

    @unittest.skipUnless(importlib.util.find_spec('open3d'), 'Requires isolated CPU Open3D')
    def test_large_world_offset_rejects_before_sparse_narrowing(self):
        views = self.fixture()
        masks = [view['valid'].astype(bool) for view in views]
        for view in views: view['cam_from_world'][2, 3] = -1e9
        with self.assertRaisesRegex(ValueError, 'coordinate range/precision'):
            integrate(views, masks, .04, selected_recipe())

    @unittest.skipUnless(importlib.util.find_spec('open3d'), 'Requires isolated CPU Open3D')
    def test_weighted_sparse_tsdf_surface_unknown_gap_sigma_and_block_bound(self):
        import numpy as np
        views = self.fixture()
        views[1]['sigma'] = np.full((48, 64), .1, dtype=np.float32)
        recipe = selected_recipe()
        masks, _ = consistent_masks(views, recipe)
        with self.assertRaisesRegex(ValueError, 'block budget'):
            integrate(views, masks, .04, dict(recipe, max_blocks=1))
        grid, indices, blocks = integrate(views, masks, .04, recipe)
        arrays = observed_arrays(grid, indices, .04, recipe)
        self.assertGreater(len(arrays['weight']), 0)
        self.assertTrue(np.allclose(arrays['weight'], .75))
        self.assertTrue((arrays['observation_count'] == 3).all())
        self.assertTrue((arrays['support_sum'] == 9).all())
        self.assertTrue(np.allclose(arrays['sigma_weight'], .25))
        self.assertTrue(np.allclose(arrays['sigma2_sum']/arrays['sigma_weight'], .01))
        self.assertLess(len(arrays['weight']), blocks*8**3)
        self.assertTrue((arrays['front_count'] > 0).any())
        self.assertTrue((arrays['front_count'] == 0).any())
        mesh = grid.extract_triangle_mesh(weight_threshold=1e-6)
        positions = mesh.vertex.positions.numpy()
        self.assertGreater(len(positions), 0)
        self.assertLess(float(np.max(np.abs(positions[:, 0]-7))), 1e-4)
        # The missing central stripe must stay open; marching cubes may not bridge it.
        camera = positions@views[0]['cam_from_world'][:, :3].T+views[0]['cam_from_world'][:, 3]
        u = camera[:, 0]*45/camera[:, 2]+29.25
        self.assertFalse(((u > 29) & (u < 35)).any())
        for triangle in mesh.triangle.indices.numpy():
            self.assertFalse(u[triangle].min() < 29 and u[triangle].max() > 35)

    @unittest.skipUnless(importlib.util.find_spec('open3d'), 'Requires isolated CPU Open3D')
    def test_confidence_changes_zero_crossing_and_missing_sigma_stays_unavailable(self):
        import numpy as np
        # Independent two-plane weighted zero: (.25*2+.75*2.04)/1 = 2.03.
        # Test integration itself with explicit masks, not the cross-view selector.
        views = self.fixture()[:2]
        views[1]['depth'][views[1]['valid'] == 1] = 2.04
        views[1]['confidence'][views[1]['valid'] == 1] = .75
        masks = [view['valid'].astype(bool) for view in views]
        recipe = selected_recipe()
        grid, indices, _ = integrate(views, masks, .02, recipe)
        arrays = observed_arrays(grid, indices, .02, recipe)
        self.assertFalse(arrays['sigma_weight'].any())
        self.assertFalse(arrays['sigma2_sum'].any())
        shared = arrays['observation_count'] == 2
        world_x = arrays['lattice'][shared, 0]*.02
        expected = (7.03-world_x)/(.02*4)
        self.assertTrue(np.allclose(arrays['tsdf'][shared], expected, atol=2e-5))


if __name__ == '__main__':
    unittest.main()
