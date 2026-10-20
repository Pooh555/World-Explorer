"""Synthetic ownership/recovery tests; no CUDA or geometry qualification."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
if sys.platform == "linux":
    import dense_job


@unittest.skipUnless(sys.platform == "linux", "Linux owned CUDA-depth profile")
class DenseJobTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.request = self.root / "request.json"
        self.request.write_text("{}")
        self.job = self.root / "job"
        self.configuration = {"manifest_sha256": "a" * 64, "recipe_sha256": "b" * 64}
        self.manifest = {"frames": [{"frame": 7, "image": "training/images/one.png"}]}
        self.recipe = {"selection_indices": [0], "worker_sha256": "c" * 64}
        self.owner = {
            "schema_version": 1,
            "kind": "SYNTHETIC_TEST",
            "request_sha256": "d" * 64,
        }
        self.args = argparse.Namespace(
            request=str(self.request),
            sha256="d" * 64,
            output=str(self.job),
            parent_supervised=False,
        )

    def complete(self, arguments, stage, parent_supervised):
        stage.mkdir()
        (stage / "assets").mkdir()
        asset = stage / "assets/one.png.wedepth"
        asset.write_bytes(b"synthetic bytes, native admission still required")
        prepared = stage / "prepared.json"
        prepared.write_text("{}")
        result = {
            "schema_version": 1,
            "status": "PARTIAL_NEIGHBOR_PILOT_COMPLETE",
            **self.configuration,
            "worker_sha256": self.recipe["worker_sha256"],
            "prepared_sha256": hashlib.sha256(prepared.read_bytes()).hexdigest(),
            "frames": [
                {
                    "frame": 7,
                    "asset": "assets/one.png.wedepth",
                    "sha256": hashlib.sha256(asset.read_bytes()).hexdigest(),
                }
            ],
        }
        (stage / "result.json").write_text(json.dumps(result))

    def admission(self, *args):
        return self.configuration, object(), self.manifest, self.recipe, self.owner

    def test_partial_attempt_survives_resume_and_terminal_replay_skips_child(self):
        def cancel(arguments, stage, parent_supervised):
            stage.mkdir()
            (stage / "partial").write_bytes(b"keep this partial output")
            raise InterruptedError("synthetic cancellation")

        with (
            patch.object(dense_job, "admit", side_effect=self.admission),
            patch.object(dense_job, "run_child", side_effect=cancel),
        ):
            with self.assertRaises(InterruptedError):
                dense_job.execute(self.args)
        partial = next((self.job / "stages").glob("*/partial"))
        self.assertFalse((self.job / "manifest.json").exists())
        with (
            patch.object(dense_job, "admit", side_effect=self.admission),
            patch.object(dense_job, "run_child", side_effect=self.complete) as child,
        ):
            dense_job.execute(self.args)
            terminal = (self.job / "manifest.json").read_bytes()
            dense_job.execute(self.args)
            self.assertEqual(child.call_count, 1)
            self.assertEqual((self.job / "manifest.json").read_bytes(), terminal)
        self.assertEqual(partial.read_bytes(), b"keep this partial output")
        self.assertEqual(len(list((self.job / "stages").iterdir())), 2)

    def test_foreign_output_rejects_before_creating_a_lock(self):
        self.job.mkdir()
        (self.job / "caller").write_bytes(b"caller data")
        with patch.object(dense_job, "admit", side_effect=self.admission):
            with self.assertRaises(ValueError):
                dense_job.execute(self.args)
        self.assertEqual([p.name for p in self.job.iterdir()], ["caller"])

    def test_committed_corruption_or_linked_assets_reject_without_repair(self):
        with (
            patch.object(dense_job, "admit", side_effect=self.admission),
            patch.object(dense_job, "run_child", side_effect=self.complete),
        ):
            dense_job.execute(self.args)
            terminal = (self.job / "manifest.json").read_bytes()
            asset = next((self.job / "stages").glob("*/assets/*.wedepth"))
            original = asset.read_bytes()
            asset.write_bytes(b"corruption")
            with self.assertRaises(ValueError):
                dense_job.execute(self.args)
            self.assertEqual(asset.read_bytes(), b"corruption")
            asset.unlink()
            target = self.root / "caller"
            target.write_bytes(original)
            asset.symlink_to(target)
            with self.assertRaises(ValueError):
                dense_job.execute(self.args)
            self.assertEqual(target.read_bytes(), original)
            self.assertEqual((self.job / "manifest.json").read_bytes(), terminal)

    def test_result_document_budget_and_frame_traversal_reject(self):
        stage = self.root / "stage"
        self.complete(None, stage, False)
        result = stage / "result.json"
        value = json.loads(result.read_bytes())
        value["frames"][0]["asset"] = "../caller"
        result.write_text(json.dumps(value))
        with self.assertRaises(ValueError):
            dense_job.checked_result(
                stage, self.configuration, self.manifest, self.recipe
            )
        result.write_bytes(b" " * (1024**2 + 1))
        with self.assertRaises(ValueError):
            dense_job.checked_result(
                stage, self.configuration, self.manifest, self.recipe
            )

    def test_source_or_environment_change_after_child_retains_output_without_terminal(
        self,
    ):
        with (
            patch.object(
                dense_job,
                "admit",
                side_effect=[
                    self.admission(),
                    (*self.admission()[:4], {"different": True}),
                ],
            ),
            patch.object(dense_job, "run_child", side_effect=self.complete),
        ):
            with self.assertRaises(ValueError):
                dense_job.execute(self.args)
        self.assertFalse((self.job / "manifest.json").exists())
        self.assertTrue(list((self.job / "stages").glob("*/assets/*.wedepth")))

    def test_sequence_partition_has_complete_references_and_temporal_support(self):
        for count in (19, 20, 63, 119, 120):
            recipe = dict(
                scope=dense_job.SEQUENCE_SCOPE,
                selection_indices=list(range(count)),
                neighbor_radius=3,
            )
            references = []
            for batch in range((count + 8) // 9):
                selected, refs = dense_job.batch_selection(recipe, count, batch)
                self.assertLessEqual(len(selected), 15)
                references.extend(refs)
                for ref in refs:
                    self.assertTrue(
                        set(range(max(0, ref - 3), min(ref + 4, count)))
                        <= set(selected)
                    )
            self.assertEqual(references, list(range(count)))
        for indices, count, batch in (
            (list(range(18)), 18, 0),
            (list(range(121)), 121, 0),
            (list(range(19)), 19, True),
            (list(range(19)), 19, 3),
            ([True] + list(range(1, 19)), 19, 0),
            (list(range(18)) + [17], 19, 0),
        ):
            with self.assertRaises(ValueError):
                dense_job.batch_selection(
                    dict(
                        scope=dense_job.SEQUENCE_SCOPE,
                        selection_indices=indices,
                        neighbor_radius=3,
                    ),
                    count,
                    batch,
                )

    def test_sequence_resume_reuses_completed_batches_and_retains_partial_batch(self):
        self.manifest = {
            "frames": [
                dict(frame=i + 1, image=f"training/images/{i}.png") for i in range(19)
            ]
        }
        self.recipe.update(
            scope=dense_job.SEQUENCE_SCOPE,
            selection_indices=list(range(19)),
            neighbor_radius=3,
        )
        calls = []
        fail = True

        def complete(arguments, stage, supervised):
            index = arguments.batch_index
            calls.append(index)
            stage.mkdir()
            if index == 1 and fail:
                (stage / "partial").write_bytes(b"preserve cancelled batch")
                raise InterruptedError("synthetic batch cancellation")
            selected, refs = dense_job.batch_selection(self.recipe, 19, index)
            prepared = dict(
                batch_index=index, reference_indices=refs, selection_indices=selected
            )
            (stage / "prepared.json").write_text(json.dumps(prepared))
            (stage / "assets").mkdir()
            frames = []
            for i in selected:
                name = f"assets/{i}.png.wedepth"
                (stage / name).write_bytes(f"synthetic {i}".encode())
                frames.append(
                    dict(
                        frame=i + 1,
                        asset=name,
                        sha256=hashlib.sha256((stage / name).read_bytes()).hexdigest(),
                    )
                )
            result = dict(
                schema_version=1,
                status="PARTIAL_NEIGHBOR_PILOT_COMPLETE",
                **self.configuration,
                worker_sha256=self.recipe["worker_sha256"],
                prepared_sha256=hashlib.sha256(
                    (stage / "prepared.json").read_bytes()
                ).hexdigest(),
                reference_indices=refs,
                frames=frames,
            )
            (stage / "result.json").write_text(json.dumps(result))

        def admit(*args):
            return (
                self.configuration,
                argparse.Namespace(batch_index=0),
                self.manifest,
                self.recipe,
                self.owner,
            )

        with (
            patch.object(dense_job, "admit", side_effect=admit),
            patch.object(dense_job, "run_child", side_effect=complete),
        ):
            with self.assertRaises(InterruptedError):
                dense_job.execute(self.args)
            self.assertFalse((self.job / "manifest.json").exists())
            partial = next((self.job / "stages").glob("*/partial"))
            fail = False
            dense_job.execute(self.args)
            terminal = (self.job / "manifest.json").read_bytes()
            dense_job.execute(self.args)
            self.assertEqual(calls, [0, 1, 1, 2])
            self.assertEqual((self.job / "manifest.json").read_bytes(), terminal)
            value = json.loads(terminal)
            self.assertEqual(value["kind"], "OWNED_RESIZED_DEPTH_SEQUENCE_TERMINAL_V1")
            self.assertEqual(
                [i for b in value["batches"] for i in b["reference_indices"]],
                list(range(19)),
            )
            self.assertEqual(partial.read_bytes(), b"preserve cancelled batch")
            result = next((self.job / "stages").glob("*batch-2-*/result.json"))
            value = json.loads(result.read_bytes())
            value["reference_indices"] = [0]
            result.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                dense_job.execute(self.args)
            self.assertEqual(calls, [0, 1, 1, 2])

    @unittest.skipUnless(sys.platform == "linux", "Linux process-group ownership")
    def test_ignored_child_termination_is_killed_and_reaped(self):
        script = self.root / "workers/dense/neighbors.py"
        script.parent.mkdir(parents=True)
        marker = self.root / "child.pid"
        script.write_text(
            "import os,pathlib,signal,time\n"
            "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
            f"pathlib.Path({str(marker)!r}).write_text(str(os.getpid()))\n"
            "time.sleep(30)\n"
        )
        arguments = argparse.Namespace(
            **{
                key: "unused"
                for key in (
                    "manifest",
                    "manifest_sha256",
                    "native_manifest",
                    "native_manifest_sha256",
                    "recipe",
                    "recipe_sha256",
                    "stage",
                )
            }
        )
        with (
            patch.object(dense_job, "ROOT", self.root),
            patch.object(dense_job, "cancelled", side_effect=lambda: marker.exists()),
        ):
            with self.assertRaises(InterruptedError):
                dense_job.run_child(arguments, self.root / "output", False)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(marker.read_text()), 0)


if __name__ == "__main__":
    unittest.main()
