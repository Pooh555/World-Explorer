"""Synthetic calibration ambiguity and the actual FFmpeg/worker boundary."""

import math
import json
from pathlib import Path
import subprocess
import unittest

import worker_frame_quality_test as decode_fixtures
import worker_frame_tracking_test as tracking_fixtures

import frame_calibration as calibration
import frame_tracking as tracking


class CalibrationPreflightTests(unittest.TestCase):
    def test_zoom_and_forward_motion_on_a_plane_are_indistinguishable(self):
        # Pinhole image coordinates depend on f/Z. These physical explanations
        # produce identical pixels, so the preflight must report ambiguity.
        zoom = [
            tracking_fixtures.smooth_frame(0, scale=focal / 1)
            for focal in (1, 1.08, 1.16)
        ]
        forward = [
            tracking_fixtures.smooth_frame(0, scale=1 / distance)
            for distance in (1, 1 / 1.08, 1 / 1.16)
        ]
        self.assertEqual(zoom, forward)
        rows = calibration.analyze(zoom, lambda: None)
        self.assertIn(
            "ambiguous_scale_change", [row["calibration"]["status"] for row in rows]
        )

    def test_gradual_zoom_accumulates_across_connected_observations(self):
        rows = calibration.analyze(
            [tracking_fixtures.smooth_frame(0, scale=1.02**i) for i in range(9)],
            lambda: None,
        )
        alarm = next(
            row["calibration"]
            for row in rows
            if row["calibration"]["status"] == "ambiguous_scale_change"
        )
        self.assertLess(abs(math.log(alarm["scale"])), math.log(1.04))
        self.assertGreaterEqual(abs(alarm["cumulative_log_scale"]), math.log(1.1))

    def test_translation_and_in_plane_rotation_keep_v2_decisions(self):
        images = [
            tracking_fixtures.smooth_frame(i / 2, angle=math.radians(i * 2))
            for i in range(8)
        ]
        original = tracking.analyze(images, lambda: None)
        rows = calibration.analyze(images, lambda: None)
        self.assertEqual(
            [{k: v for k, v in row.items() if k != "calibration"} for row in rows],
            original,
        )
        self.assertNotIn(
            "ambiguous_scale_change", [row["calibration"]["status"] for row in rows]
        )
        self.assertTrue(any(row["calibration"]["status"] == "assessed" for row in rows))

    def test_unsupported_intervals_do_not_claim_fixed_intrinsics(self):
        rows = calibration.analyze(
            [tracking_fixtures.smooth_frame(0), bytes([127]) * 6144], lambda: None
        )
        self.assertEqual(rows[0]["calibration"]["status"], "unassessed")
        self.assertEqual(rows[1]["calibration"]["status"], "insufficient_support")
        self.assertIsNone(rows[1]["calibration"]["cumulative_log_scale"])
        self.assertIsNone(rows[1]["calibration"]["chain_start"])

    def test_similarity_fit_has_known_scale_and_rejects_shear_support(self):
        previous = [(0, x, y, x, y) for x in (12, 48, 84) for y in (10, 32, 54)]
        moved = [
            (birth, x, y, 1.08 * x + 2, 1.08 * y - 3) for birth, x, y, _, _ in previous
        ]
        measurement = calibration._measure(0, previous, moved)
        self.assertAlmostEqual(measurement["scale"], 1.08)
        self.assertLess(measurement["rms_px"], 1e-12)
        sheared = [(birth, x, y, x + 0.2 * y, y) for birth, x, y, _, _ in previous]
        self.assertGreater(calibration._measure(0, previous, sheared)["rms_px"], 0.5)

    def test_zoom_out_is_also_ambiguous(self):
        rows = calibration.analyze(
            [tracking_fixtures.smooth_frame(0, scale=s) for s in (1, 0.93, 0.86)],
            lambda: None,
        )
        self.assertIn(
            "ambiguous_scale_change", [row["calibration"]["status"] for row in rows]
        )


class CalibrationDecodeTests(decode_fixtures.QualityDecodeTests):
    @staticmethod
    def selection_policy() -> dict:
        return calibration.policy()

    def setUp(self):
        super().setUp()
        self.request["frame_selection"] = calibration.PROFILE

    def test_zoom_rejection_preserves_prior_decode_and_diagnostics(self):
        for index, scale in enumerate((1, 1.08, 1.16)):
            (self.root / f"zoom-{index:03d}.pgm").write_bytes(
                b"P5\n96 64\n255\n" + tracking_fixtures.smooth_frame(0, scale=scale)
            )
        source = self.root / "synthetic zoom.mkv"
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-framerate",
                "2",
                "-i",
                str(self.root / "zoom-%03d.pgm"),
                "-c:v",
                "ffv1",
                str(source),
            ],
            check=True,
        )
        request = self.request | {"source": str(source)}
        worker = decode_fixtures.worker
        legacy = worker.Job(
            request | {"frame_selection": "timestamp_sampling_v1"}
        ).run()
        stage = Path(legacy["output"])
        protected = {
            path: path.read_bytes() for path in stage.rglob("*") if path.is_file()
        }
        terminal = (self.root / "quality/result.json").read_bytes()
        selected = (self.root / "quality/decode.json").read_bytes()
        with self.assertRaises(worker.WorkerError) as caught:
            worker.Job(request).run()
        self.assertEqual(caught.exception.code, "CALIBRATION_REQUIRED")
        self.assertEqual((self.root / "quality/result.json").read_bytes(), terminal)
        self.assertEqual((self.root / "quality/decode.json").read_bytes(), selected)
        for path, original in protected.items():
            self.assertEqual(path.read_bytes(), original)
        failed = list((self.root / "quality").glob("stages/.decode-*/quality.json"))
        self.assertEqual(len(failed), 1)
        ledger = json.loads(failed[0].read_text())
        self.assertIn(
            "ambiguous_scale_change",
            [r["calibration"]["status"] for r in ledger["decisions"]],
        )
        self.assertFalse((failed[0].parent / "completion.json").exists())
        self.assertTrue((failed[0].parent / "candidates.gray").is_file())


if __name__ == "__main__":
    unittest.main()
