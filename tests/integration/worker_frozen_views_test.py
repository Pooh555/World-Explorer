"""Synthetic role-bound input preparation; no pose or fit substitutes."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "we_frozen_worker", ROOT / "workers/worker.py"
)
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)
frozen = worker._frozen_view_helper()


def png(value: int) -> bytes:
    def chunk(kind, body):
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body))
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">II5B", 2, 1, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"\0" + bytes([value] * 6)))
        + chunk(b"IEND", b"")
    )


class FrozenViewTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.addCleanup(worker.CANCELLED.clear)
        self.root = Path(self.temporary.name).resolve()
        self.source = self.root / "source.mp4"
        self.source.write_bytes(b"synthetic source identity; no camera/pose claim")
        self.images = self.root / "images"
        self.images.mkdir()
        self.plan = dict(
            schema_version=1,
            source_sha256=worker.sha256(self.source),
            source_frame_count=4,
            width=2,
            height=1,
            time_base="1/30",
            role_blocks=[
                dict(first=0, end=3, role="INFERENCE"),
                dict(first=3, end=4, role="HELD_OUT"),
            ],
            frames=[],
        )
        for index in range(4):
            body = png(index)
            name = f"frame-{index}.png"
            (self.images / name).write_bytes(body)
            self.plan["frames"].append(
                dict(
                    source_index=index,
                    pts=index,
                    role="INFERENCE" if index < 3 else "HELD_OUT",
                    file=name,
                    sha256=hashlib.sha256(body).hexdigest(),
                    bytes=len(body),
                )
            )
        self.manifest = self.root / "selection.json"
        self.output = self.root / "new-job"
        self.configure()

    def configure(self):
        self.manifest.write_text(json.dumps(self.plan))
        self.configuration = dict(
            manifest=str(self.manifest),
            sha256=worker.sha256(self.manifest),
            image_directory=str(self.images),
        )
        self.request = dict(
            schema_version=1,
            source=str(self.source),
            job_dir=str(self.output),
            stop_after="decode",
            frozen_views=self.configuration,
        )

    def prepare(self):
        self.output.mkdir()
        job = SimpleNamespace(
            request=worker.validate_request(self.request),
            source_hash=self.plan["source_sha256"],
            source=self.source,
            limits=worker.LIMITS,
        )
        inspection = dict(
            rotation_degrees=0, stream=dict(width=2, height=1, time_base="1/30")
        )
        return frozen.decode(job, self.output, inspection, worker)

    def test_copies_only_inference_with_exact_original_identity(self):
        before = {p: p.read_bytes() for p in self.images.iterdir()}
        result = self.prepare()
        self.assertEqual([f["source_index"] for f in result["frames"]], [0, 1, 2])
        self.assertEqual(
            [f["capture_time_ns"] for f in result["frames"]], [0, 33333333, 66666666]
        )
        self.assertEqual(
            result["selection_manifest_sha256"], self.configuration["sha256"]
        )
        self.assertEqual(
            (self.output / "selection.json").read_bytes(), self.manifest.read_bytes()
        )
        self.assertEqual(len(list((self.output / "images").iterdir())), 3)
        for frame, original in zip(result["frames"], self.plan["frames"]):
            self.assertEqual(
                (self.output / frame["file"]).read_bytes(),
                (self.images / original["file"]).read_bytes(),
            )
        self.assertEqual(before, {p: p.read_bytes() for p in self.images.iterdir()})

    def test_bad_frozen_roles_paths_and_sizes_refuse_before_job_creation(self):
        mutations = [
            lambda p: p["frames"][0].update(role="HELD_OUT"),
            lambda p: p["role_blocks"][1].update(first=2),
            lambda p: p["role_blocks"][1].update(end=3),
            lambda p: p["frames"][1].update(pts=0),
            lambda p: p["frames"][0].update(file="../frame.png"),
            lambda p: p["frames"][0].update(bytes=True),
            lambda p: p["frames"][0].update(bytes=16 * 1024**2 + 1),
            lambda p: p["frames"][3].update(sha256=p["frames"][0]["sha256"]),
            lambda p: p.update(unrecognized=True),
        ]
        original = copy.deepcopy(self.plan)
        for mutation in mutations:
            with self.subTest(mutation=mutations.index(mutation)):
                self.plan = copy.deepcopy(original)
                mutation(self.plan)
                self.configure()
                with self.assertRaises(worker.WorkerError):
                    worker.validate_request(self.request)
                self.assertFalse(self.output.exists())

    def test_hash_and_duplicate_keys_rejected(self):
        self.configuration["sha256"] = "0" * 64
        with self.assertRaises(worker.WorkerError):
            worker.validate_request(self.request)
        self.manifest.write_text('{"schema_version":1,"schema_version":1}')
        self.configuration["sha256"] = worker.sha256(self.manifest)
        with self.assertRaises(worker.WorkerError):
            worker.validate_request(self.request)

    def test_non_decode_request_and_input_output_overlap_rejected(self):
        for change in [
            dict(stop_after="poses"),
            dict(job_dir=str(self.images)),
            dict(frame_selection="quality_keyframes_candidate_v1"),
        ]:
            with self.subTest(change=change), self.assertRaises(worker.WorkerError):
                worker.validate_request(self.request | change)

    def test_changed_held_out_input_is_not_silently_ignored(self):
        (self.images / "frame-3.png").write_bytes(b"changed withheld bytes")
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse((self.output / "images").exists())

    def test_cancelled_copy_never_returns_a_complete_result(self):
        worker.CANCELLED.set()
        with self.assertRaises(worker.WorkerError) as caught:
            self.prepare()
        self.assertEqual(caught.exception.code, "CANCELLED")
        self.assertFalse((self.output / "selection.json").exists())

    def test_configured_disk_or_frame_budget_is_enforced(self):
        for limits in [dict(max_output_bytes=1), dict(max_frames=2)]:
            with self.subTest(limits=limits):
                self.output.mkdir(exist_ok=True)
                job = SimpleNamespace(
                    request=worker.validate_request(self.request),
                    source_hash=self.plan["source_sha256"],
                    source=self.source,
                    limits=worker.LIMITS | limits,
                )
                with self.assertRaises(ValueError):
                    frozen.decode(
                        job,
                        self.output,
                        dict(
                            rotation_degrees=0,
                            stream=dict(width=2, height=1, time_base="1/30"),
                        ),
                        worker,
                    )
                self.assertFalse((self.output / "images").exists())


if __name__ == "__main__":
    unittest.main()
