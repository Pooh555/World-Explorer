"""Bounded, synthetic frame-quality and real FFmpeg selection contracts."""

import importlib.util
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "workers"))

import frame_quality as quality

specification = importlib.util.spec_from_file_location(
    "quality_worker", ROOT / "workers/worker.py"
)
worker = importlib.util.module_from_spec(specification)
specification.loader.exec_module(worker)


def textured_frame(offset: int = 0) -> bytes:
    """Translate a deterministic textured field, without wrapping its content."""
    generator = random.Random(1907)
    field = bytes(generator.randrange(30, 226) for _ in range(128 * 64))
    return b"".join(field[y * 128 + offset : y * 128 + offset + 96] for y in range(64))


class FrameQualityTests(unittest.TestCase):
    def test_tracks_distinguish_static_motion_and_repeated_frames(self):
        frames = [textured_frame(offset) for offset in (0, 2, 2, 4, 6)]
        decisions = quality.analyze(frames, lambda: None)
        self.assertEqual(
            [entry["reason"] for entry in decisions],
            [
                "seed",
                "motion_coverage",
                "repeated",
                "motion_coverage",
                "motion_coverage",
            ],
        )
        for entry in (decisions[1], decisions[3], decisions[4]):
            self.assertGreaterEqual(entry["metrics"]["tracked_cells"], 4)
            self.assertAlmostEqual(entry["metrics"]["displacement_px"], 2.0)

    def test_blur_and_exposure_are_explicit_rejections(self):
        decisions = quality.analyze(
            [textured_frame(), bytes([127]) * 6144, bytes([255]) * 6144],
            lambda: None,
        )
        self.assertEqual(decisions[1]["reason"], "blur_or_texture")
        self.assertEqual(decisions[2]["reason"], "exposure")
        self.assertFalse(decisions[1]["selected"])
        self.assertFalse(decisions[2]["selected"])

    def test_untrackable_cut_is_reported_without_a_new_calibration(self):
        frames = [textured_frame(), bytes(255 - value for value in textured_frame())]
        decisions = quality.analyze(frames, lambda: None)
        self.assertEqual(decisions[1]["reason"], "tracking_break")
        self.assertFalse(decisions[1]["selected"])

    def test_bounds_and_cancellation_precede_analysis(self):
        for frames in ([b"short"], [textured_frame()] * 481):
            with self.assertRaises(ValueError):
                quality.analyze(frames, lambda: None)

        def cancel() -> None:
            raise InterruptedError("synthetic cancellation")

        with self.assertRaises(InterruptedError):
            quality.analyze([textured_frame()], cancel)


@unittest.skipUnless(
    shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg required"
)
class QualityDecodeTests(unittest.TestCase):
    @staticmethod
    def selection_policy() -> dict:
        return quality.policy()

    def setUp(self):
        worker.CANCELLED.clear()
        self.addCleanup(worker.CANCELLED.clear)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for index, offset in enumerate((0, 0, 2, 2, 4, 4, 6, 6)):
            (self.root / f"{index:03d}.pgm").write_bytes(
                b"P5\n96 64\n255\n" + textured_frame(offset)
            )
        self.source = self.root / "translated texture.mkv"
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-framerate",
                "2",
                "-i",
                str(self.root / "%03d.pgm"),
                "-c:v",
                "ffv1",
                str(self.source),
            ],
            check=True,
        )
        self.request = {
            "schema_version": 1,
            "source": str(self.source),
            "job_dir": str(self.root / "quality"),
            "stop_after": "decode",
            "sample_fps": 2,
            "frame_selection": quality.PROFILE,
        }

    def test_real_decode_ledger_resume_and_legacy_preservation(self):
        legacy_request = self.request | {"job_dir": str(self.root / "legacy")}
        del legacy_request["frame_selection"]
        legacy = Path(worker.Job(legacy_request).run()["output"])
        retained = {
            path: path.read_bytes() for path in legacy.rglob("*") if path.is_file()
        }
        result = worker.Job(self.request).run()
        stage = Path(result["output"])
        decoded = json.loads((stage / "result.json").read_text())
        ledger = json.loads((stage / "quality.json").read_text())
        self.assertEqual(decoded["selection"], self.request["frame_selection"])
        self.assertEqual(
            [frame["source_index"] for frame in decoded["frames"]], [0, 2, 4, 6]
        )
        self.assertEqual(
            decoded["quality_ledger_sha256"], worker.sha256(stage / "quality.json")
        )
        self.assertEqual(
            ledger["policy_sha256"], worker.canonical_hash(self.selection_policy())
        )
        self.assertEqual(
            [entry["frame"] for entry in ledger["decisions"] if entry["selected"]],
            decoded["frames"],
        )
        self.assertEqual(ledger["thumbnails"]["bytes"], 8 * 6144)
        original = (stage / "completion.json").read_bytes()
        self.assertEqual(worker.Job(self.request).run(), result)
        self.assertEqual((stage / "completion.json").read_bytes(), original)
        for path, data in retained.items():
            self.assertEqual(path.read_bytes(), data)
        (stage / "candidates.gray").write_bytes(b"corrupted")
        with self.assertRaises(worker.WorkerError) as caught:
            worker.Job(self.request).run()
        self.assertEqual(caught.exception.code, "CORRUPT_ASSET")

    def test_candidate_limit_and_cancel_leave_no_committed_decode(self):
        request = self.request | {"limits": {"max_frames": 1}}
        with self.assertRaises(worker.WorkerError) as caught:
            worker.Job(request).run()
        self.assertEqual(caught.exception.code, "RESOURCE_LIMIT")
        self.assertFalse((self.root / "quality/decode.json").exists())
        worker.CANCELLED.set()
        self.addCleanup(worker.CANCELLED.clear)
        with self.assertRaises(worker.WorkerError) as caught:
            worker.Job(self.request).run()
        self.assertEqual(caught.exception.code, "CANCELLED")

    def test_unknown_or_undersampled_policy_is_rejected(self):
        for patch in ({"frame_selection": "future"}, {"sample_fps": 1}):
            with self.assertRaises(worker.WorkerError) as caught:
                worker.validate_request(self.request | patch)
            self.assertEqual(caught.exception.code, "BAD_INPUT")

    def test_real_tracking_break_retains_ledger_and_previous_result(self):
        for index in range(8):
            pixels = (
                textured_frame(2 * index)
                if index < 2
                else bytes(255 - value for value in textured_frame(2 * index))
            )
            (self.root / f"cut-{index:03d}.pgm").write_bytes(
                b"P5\n96 64\n255\n" + pixels
            )
        source = self.root / "cut.mkv"
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-framerate",
                "2",
                "-i",
                str(self.root / "cut-%03d.pgm"),
                "-c:v",
                "ffv1",
                str(source),
            ],
            check=True,
        )
        request = self.request | {"source": str(source)}
        directory = self.root / "quality"
        directory.mkdir()
        (directory / "result.json").write_bytes(b"previous terminal result")
        with self.assertRaises(worker.WorkerError) as caught:
            worker.Job(request).run()
        self.assertEqual(caught.exception.code, "INSUFFICIENT_PARALLAX")
        self.assertFalse((directory / "decode.json").exists())
        self.assertEqual(
            (directory / "result.json").read_bytes(), b"previous terminal result"
        )
        ledgers = list((directory / "stages").glob(".decode-*/quality.json"))
        self.assertEqual(len(ledgers), 1)
        ledger = json.loads(ledgers[0].read_text())
        self.assertTrue(
            any(row["reason"] == "tracking_break" for row in ledger["decisions"])
        )
        self.assertFalse((ledgers[0].parent / "completion.json").exists())

    def test_analysis_cancellation_preserves_partial_thumbnails(self):
        command = worker.Job.command

        def cancel_after_thumbnails(job, args, directory, **options):
            result = command(job, args, directory, **options)
            if args[-1].endswith("candidates.gray"):
                worker.CANCELLED.set()
            return result

        with mock.patch.object(worker.Job, "command", cancel_after_thumbnails):
            with self.assertRaises(worker.WorkerError) as caught:
                worker.Job(self.request).run()
        self.assertEqual(caught.exception.code, "CANCELLED")
        directory = self.root / "quality"
        self.assertFalse((directory / "decode.json").exists())
        partials = list((directory / "stages").glob(".decode-*/candidates.gray"))
        self.assertEqual(len(partials), 1)
        self.assertEqual(partials[0].stat().st_size, 8 * 6144)
        self.assertFalse((partials[0].parent / "completion.json").exists())

    def test_changed_source_cannot_publish_a_misbound_quality_ledger(self):
        command = worker.Job.command
        original = self.source.read_bytes()

        def change_after_thumbnails(job, args, directory, **options):
            result = command(job, args, directory, **options)
            if args[-1].endswith("candidates.gray"):
                self.source.write_bytes(original + b"synthetic caller edit")
            return result

        with mock.patch.object(worker.Job, "command", change_after_thumbnails):
            with self.assertRaises(worker.WorkerError) as caught:
                worker.Job(self.request).run()
        self.assertEqual(caught.exception.code, "BAD_INPUT")
        self.assertEqual(self.source.read_bytes(), original + b"synthetic caller edit")
        directory = self.root / "quality"
        self.assertFalse((directory / "decode.json").exists())
        self.assertFalse(list((directory / "stages").glob(".decode-*/quality.json")))


if __name__ == "__main__":
    unittest.main()
