# M3 recovery admission and retained graph — 2026-10-16 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS; Q1 remains BLOCKED**. New read-only recovery
inspection works on the retained sample_2 failure. No pose solve, training,
geometry, walking or full gate pass is claimed. Parent `c5f50f6`; implementation
is the commit containing this report. Cwd: repository root. Ignored evidence
root **R**: `artifacts/validation/m3-recovery-analysis-20261016`.

[The diagnostic contract](../protocols/poses.md) binds the committed decode,
original solver command, database and sparse models before/after inspection.
It validates bounded image/pair/model identities, preserves strict-superset model
selection and the integer-ceiling 90% requirement, and publishes an exclusive,
atomic report outside the retained job. SQLite uses a private bounded copy;
sidecars require review. Pair-count components are explicitly distinct from
PnP correspondences, triangulation, parallax or registration success. Viewer,
reconstruction recipes, environments and thresholds are unchanged.

## Real retained observations

Source SHA remains
`10a6405980025eadfbe12e4c68d86bbab8d41af24e0fa48f2d12ca829f0e1de3`;
decode key `dc76ef01edd9b2ac878ad5666fdf359ec020cde39a5b9bbf8b20d2894be98b5d`.
The four-image model is a strict subset of model 1's **79/90** registrations.
The unchanged gate requires **81**; missing images are **000050–000060.png**.
No disconnected models are combined.

The database retains **4,005 pairs**, **1,621** with positive inlier counts.
Configurations: **1,479 UNCALIBRATED / 142 PLANAR_OR_PANORAMIC / 2,384 DEGENERATE**.
Positive-count components have **89/1** images; at the installed mapper count
thresholds **30/100**, component sizes are **82/7/1** and **79/4/3/2/1/1**.
Image 59 has no positive-inlier neighbors. Image 50 reaches registered image 49
with **84** pair inliers; image 60 reaches registered images 62/63 with **32/27**.
All three boundary pairs are PLANAR_OR_PANORAMIC. These counts do not prove a
usable 3D baseline or a recovery capable of passing registration.

A separate private-copy observer uses the saved model's feature-to-point IDs and
native uint32 pair correspondences. Image 50 references only **seven** distinct
existing map points; image 60 references **12**; the other missing frames reference
none. Each is below the installed **30** pose-inlier minimum. This does not rule
out changed triangulation or new observations, but gives no support for simply
continuing the unchanged map. A retained 20-thumbnail contact sheet shows the
close door/wall interval; this qualitative observation is not a parallax metric.
No recovery allowance has been consumed on this source during this slice.

## Actual checks and failures

```sh
.venv-worker/bin/python tools/inspect_pose_recovery.py --request artifacts/sample_2/poses/batch-request.json --failed-stage artifacts/sample_2/poses/stages/.poses-1_k2am3l --output artifacts/validation/m3-recovery-analysis-20261016/sample_2-final.json
.venv-worker/bin/python artifacts/validation/m3-recovery-analysis-20261016/count_existing_points.py
ctest --preset dev --output-on-failure -j 4
.venv-worker/bin/python -m unittest discover -s tests/integration -p 'worker*_test.py' -v
.venv-train/bin/python -m unittest discover -s tests/integration -p 'worker*_test.py' -v
python3 artifacts/validation/m3-recovery-analysis-20261016/preserve_checked.py
```

All exit **0**. CTest: **280/280**, **18.22 s**, existing GCC C++26 dev profile,
including **166 worker cases** on Python **3.14.8**. Separate Python **3.12.15 /
3.10.22** worker runs: **166/166 each**. The **17 new synthetic cases** cover exact
registration counts, ambiguous/subset models, planar/zero edges, malformed IDs,
budgets, WAL-directory preservation, closed SQLite handles, input binding/mutation,
rehashed unsafe metadata and exclusive/failed report publication. Initial focused
runs cover earlier 13/16-case versions; final complete suites cover current bytes.
No C++ or CMake changes require a rebuild or new format/tidy/sanitizer run.
Boundaries, documentation and whitespace checks pass. CPU runs overlap; their
durations are command observations, not performance evidence.

The initial ad hoc `mode=ro` SQLite query created a zero-byte WAL and 32 KiB
shared-memory index beside the original database. The first diagnostic refused
those sidecars (exit **2**). Inspection confirmed no active coordinator and the
unchanged database SHA
`ec4dfa49e1fd7a6759353951b3300f5035b3f75251fdb13dbeca17f7304716f9`.
Only these two files created by this turn were removed; later reads use private
copies. The WAL-mode synthetic regression and final full inventory confirm no
sidecars or original-file changes. Reusing the real report returns expected
**2**, preserving its bytes (`R/refuse-existing.json`).

Final preservation checks retain **1,509 protected non-document files** unchanged,
including all saved runs/sources, instructions and both frozen fixtures. The first
preservation observer included its still-open stdout in its identity list; the
checked observer excludes its own active log and retains the earlier receipt.

| Final local receipt | SHA-256 |
|---|---|
| `R/sample_2-final.json` | `611823666166fe6d591430e931945dbe13236a00ac2e3bbae31bb47e35d36b32` |
| `R/existing-point-support-checked.json` | `0a30f8119c7239c452da7c540174e8594331934f9d3fccceacf62ca546a365fc` |
| `R/preservation-checked.json` | `190be736cac6fe1d48662ef642b9ce4d1804117e784f3a285f8b1b64de58bc36` |

An installed-capability probe records pycolmap **4.2.1**, `has_cuda=false`, dense
stereo/fusion/Poisson API presence and no standalone `colmap` executable
(`R/dense-capabilities.json`). API presence is not dense-geometry execution or
qualification; the host GPU remains available independently of this CPU pose
package. Geometry/depth confidence, independent scale, collision and mesh export
remain unimplemented. Next M3 work is the versioned depth/confidence admission
contract and a separately qualified dense worker; sample_2 needs a changed,
evidence-based recovery recipe. No provisioning, model download, upload or push.
