"""Persistent candidate tracking against known synthetic image motion."""

import math
import unittest

import worker_frame_quality_test as fixtures

import frame_tracking as tracking


def smooth_frame(
    offset: float, *, occlusion: bool = False, angle: float = 0, scale: float = 1
) -> bytes:
    """Sample a continuous, asymmetric texture at a known subpixel translation."""
    pixels = bytearray()
    cosine, sine = math.cos(angle), math.sin(angle)
    for y in range(64):
        for x in range(96):
            shifted = ((x - 48) * cosine - (y - 32) * sine) / scale + 48 + offset
            warped_y = ((x - 48) * sine + (y - 32) * cosine) / scale + 32
            value = (
                128
                + 32 * math.sin(0.47 * shifted + 0.2 * warped_y)
                + 27 * math.cos(0.23 * shifted - 0.69 * warped_y)
                + 18 * math.sin(1.1 * shifted + 0.31 * warped_y)
                + 10 * math.sin(0.43 * shifted - 0.8 * warped_y)
            )
            pixels.append(127 if occlusion and x < 24 and y < 42 else round(value))
    return bytes(pixels)


class PersistentTrackingTests(unittest.TestCase):
    def test_spatially_varying_camera_motion_is_not_mistaken_for_a_cut(self):
        rows = tracking.analyze(
            [smooth_frame(0, angle=math.radians(index * 4)) for index in range(8)],
            lambda: None,
        )
        self.assertNotIn("tracking_break", [row["reason"] for row in rows])
        self.assertGreaterEqual(sum(row["selected"] for row in rows), 3)

    def test_connected_narrow_views_can_renew_points_without_selecting_them(self):
        frames = []
        for index in range(40):
            base = smooth_frame(index / 4)
            pixels = bytearray(base)
            if index:
                for y in range(64):
                    for x in range(96):
                        cell = min(2, y * 3 // 64) * 4 + x // 24
                        if cell not in (0, 3, 8):
                            pixels[y * 96 + x] = (
                                127 if index < 7 else 255 - base[y * 96 + x]
                            )
            frames.append(bytes(pixels))
        rows = tracking.analyze(frames, lambda: None)
        self.assertTrue(all(not row["selected"] for row in rows[1:7]))
        self.assertEqual(rows[6]["tracking"]["reference"], 5)
        self.assertGreaterEqual(sum(row["selected"] for row in rows), 3)
        self.assertNotIn("tracking_break", [row["reason"] for row in rows])

    def test_all_features_can_turn_over_before_the_next_keyframe(self):
        frames = []
        for index in range(56):
            offset = index * 0.03 if index < 24 else 0.72 + (index - 24) / 4
            lower = fixtures.textured_frame(math.floor(offset))
            upper = fixtures.textured_frame(math.floor(offset) + 1)
            fraction = offset % 1
            pixels = bytearray(
                round(a * (1 - fraction) + b * fraction) for a, b in zip(lower, upper)
            )
            for y in range(64):
                for x in range(96):
                    cell = min(2, y * 3 // 64) * 4 + x // 24
                    if cell < min(12, index // 2):
                        pixels[y * 96 + x] = 255 - pixels[y * 96 + x]
            frames.append(bytes(pixels))
        rows = tracking.analyze(frames, lambda: None)
        self.assertNotIn("tracking_break", [row["reason"] for row in rows])
        self.assertGreaterEqual(sum(row["selected"] for row in rows), 3)
        self.assertTrue(
            any(
                row["tracking"]["birth_last"] is not None
                and row["tracking"]["birth_last"] > row["tracking"]["anchor"]
                for row in rows
                if row["tracking"]["anchor"] is not None
            )
        )

    def test_reversal_and_exposure_flicker_do_not_accumulate_fake_distance(self):
        offsets = (0, 0.4, 0.8, 0.4, 0, 0.4, 0.8, 0.4, 0)
        frames = [
            bytes(min(255, value + index % 3) for value in smooth_frame(offset))
            for index, offset in enumerate(offsets)
        ]
        rows = tracking.analyze(frames, lambda: None)
        self.assertEqual(sum(row["selected"] for row in rows), 1)
        self.assertLess(rows[-1]["metrics"]["displacement_px"], 0.4)

    def test_subpixel_motion_and_turnover_preserve_real_correspondences(self):
        frames = [
            smooth_frame(index / 4, occlusion=4 <= index < 13) for index in range(32)
        ]
        decisions = tracking.analyze(frames, lambda: None)
        selected = [index for index, row in enumerate(decisions) if row["selected"]]
        self.assertGreaterEqual(len(selected), 3)
        self.assertNotIn("tracking_break", [row["reason"] for row in decisions])
        for index in selected[1:]:
            row = decisions[index]
            anchor = row["tracking"]["anchor"]
            self.assertGreaterEqual(row["metrics"]["tracked_cells"], 4)
            self.assertAlmostEqual(
                row["metrics"]["displacement_px"], (index - anchor) / 4, delta=0.4
            )
            self.assertGreaterEqual(row["tracking"]["reference"], anchor)
            self.assertLess(row["tracking"]["reference"], index)

    def test_short_blur_interval_does_not_invent_a_new_seed(self):
        frames = [smooth_frame(index / 4) for index in range(24)]
        frames[4:6] = [bytes([127]) * 6144] * 2
        rows = tracking.analyze(frames, lambda: None)
        self.assertEqual(
            [rows[index]["reason"] for index in (4, 5)], ["blur_or_texture"] * 2
        )
        self.assertEqual(sum(row["reason"] == "seed" for row in rows), 1)
        self.assertGreaterEqual(sum(row["selected"] for row in rows), 3)
        self.assertEqual(rows[6]["tracking"]["reference"], 3)

    def test_seed_window_prefers_spatially_distributed_features(self):
        frames = [
            smooth_frame(0, occlusion=True),
            smooth_frame(0.25),
            smooth_frame(0.5),
        ]
        rows = tracking.analyze(frames, lambda: None)
        self.assertEqual(rows[0]["reason"], "seed_candidate")
        self.assertEqual(rows[1]["reason"], "seed")
        self.assertIsNone(rows[1]["tracking"]["anchor"])

    def test_unrelated_texture_is_not_reseeded_after_a_cut(self):
        frame = fixtures.textured_frame()
        rows = tracking.analyze(
            [frame, bytes(255 - value for value in frame)], lambda: None
        )
        self.assertIn(rows[1]["reason"], ("tracking_break", "suspected_cut"))
        self.assertFalse(rows[1]["selected"])

    def test_bounds_and_cancel_are_enforced(self):
        for frames in ([b"short"], [smooth_frame(0)] * 481):
            with self.assertRaises(ValueError):
                tracking.analyze(frames, lambda: None)

        def cancel() -> None:
            raise InterruptedError("synthetic cancellation")

        with self.assertRaises(InterruptedError):
            tracking.analyze([smooth_frame(0)], cancel)


class TrackingDecodeTests(fixtures.QualityDecodeTests):
    @staticmethod
    def selection_policy() -> dict:
        return tracking.policy()

    def setUp(self):
        super().setUp()
        self.request["frame_selection"] = tracking.PROFILE


if __name__ == "__main__":
    unittest.main()
