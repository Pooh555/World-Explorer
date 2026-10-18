# Bounded parallax-aware MVS sources — 2026-10-18 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. CPU source planning is checked locally; the changed
partial recipe reduces fixed-reference coverage and remains experimental; no full reconstruction, fusion, measured scale,
Q5/N2, scene or walking gate. Parent **04d1351**; implementation is the commit
containing this report. Cwd: repository root. Ignored evidence **R**:
`artifacts/validation/m3-dense-parallax-20261018`.

## Selection and preserved scope

The [source-policy contract](../../workers/dense/README.md#parallax-aware-source-experiment)
keeps the **same nine reference views**, selects at most **18 total views**, and
closes all source rows over views that receive their own geometric depth. Each
pair requires **30 distinct visible shared landmarks at 3°–60°**; each row has
2–6 sources. Reference-normalized greedy gains prioritize two-source landmark
support. Unsupported auxiliary vertices are pruned; references cannot be dropped.
The worker recomputes the report from actual owned native model bytes before
solve and checks its digest and exact rows against the frozen recipe.

The retained model contains **4,056 repeated same-view landmark references**,
with explicit reciprocal track entries. The initial planner incorrectly assumed
one feature per landmark per image and rejects (`plan-real.log`). The corrected
planner validates both directions of every track and counts each visible landmark
once per view, preserving all original features/tracks. A dedicated synthetic
regression verifies this rule and rejects duplicate exact/missing back-references.
No input was repaired or discarded to obtain the result.

The selected manifest indices are **0–8, 11–15, 18, 25, 26, 30**. An independent
set-intersection/acos oracle checks every row's co-visibility, parallax and
closure; reference two-source sparse support is **24.78–29.51%**
(`independent-plan.json`, `plan.json`). This is a planning proxy, not dense
coverage or ground truth. The complete **63-view / 441,769-keypoint /
29,846-landmark** resized model preserves poses, tracks, positions and colors;
independent reprojection scaling error remains at most **4.54747e-13 pixels**
(`independent-model.json`). All 63 resized PNG hashes and camera intrinsics match
the preceding experiment exactly; only recipe/dependent metadata change.

| Selected identity | SHA-256 |
|---|---|
| Native manifest | `495084082f7e1f5e272c700aa836d1143f2352320a1e85d8b8221fd7276e48f8` |
| Resized manifest | `c3472aec25d65f8565021e3c793f184890fb167e95e3a7dd89ae5235c4d7954f` |
| Full recipe | `ddf53ea6222e079abc6c023f5470f486ed29364991797d525d4e38a04bf125b7` |
| Canonical selection report | `0afeef21ed2ac15971b92230dd63ba6b9d9227c09c7219e032b9c08153236e01` |
| Seven-file worker bundle | `ead07a16e0bacc6db394af6ca6bcb27bda8401612b33d1b4542786839e97d98f` |

PatchMatch settings, native poses and reference RGB/grid remain unchanged:
**2048×1102**, five iterations, geometric consistency/filtering, at least two
consistent sources. Confidence remains consistent-source fraction, not
probability or uncertainty; support includes the reference. New dependencies
are prepared/admitted before solve. Prior recipes/results retain their original
bytes and scope; neither the temporal nor native three-view run is retried.

## Actual checks

Local Linux x86-64 / RTX 4060 Laptop GPU, **8188 MiB**, driver **615.78.08**;
isolated COLMAP CUDA **4.2.1**, NumPy **2.2.6**, Pillow **11.3.0**. A fresh
installed audit checks **six packages / 1,188 listed files** with matching exact
requirements. No installation/model download or release-rights qualification.
Native GPU owner timeout is **900 s**, with six-second cancellation grace and
kill/reap fallback. Raw GPU/owner observations stay in `real-gpu.log` /
`real-execution.json`; this subset is not whole-video P1 or reference hardware.

| Actual command (cwd: repository root) | Result / logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | Final **308/308**, **24.85 s**; `build-dev.log`, `ctest-dev-final.log`; initial **18.08 s** precedes the final repeated-landmark regression |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **285/285**, **20.00 s**; `build-cxx23.log`, `ctest-cxx23.log` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | **30/30**, no skips; `unit-complete.log`. Seven new cases exercise graph closure/quorum, deterministic ties, scale/rotation invariance, degenerate/invalid inputs, resource bounds/cancellation, same-view deduplication and model identity |
| `python3 artifacts/validation/m3-dense-parallax-20261018/plan_real.py` | CPU planner; actual argv/exit in `plan-command.json`; successful **2.653 s**, `plan-real-retry.log` |
| `python3 artifacts/validation/m3-dense-parallax-20261018/setup_real.py` | New full recipe, 63 new bound PNGs, native admission and recomputed prepare-only selection; exact argv/exits in `setup-commands.json` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-parallax-20261018/check_plan.py`; `.venv-dense/bin/python artifacts/validation/m3-dense-parallax-20261018/check_model.py` | Independent graph/model checks above; `independent-plan.log`, `independent-model.log` |

Ten entry probes reject wrong native/resized/recipe hashes, overlap, occupied or
dangling outputs, open/self source graphs, over-budget selection and a lowered
policy angle (`entry-checks.json`). Two additional rehashed forgeries pass structural
admission but fail actual model recomputation **before solve**, publishing no
prepared/result marker (`selection-guard.json`). CPU preparation cancellation
returns **130** in **0.1143 s** after SIGINT, with no marker and no forced kill
(`cancel-preparation.json`). This does not qualify GPU cooperative cancellation,
production protocol control or resume. No new sanitizer/native viewer/platform
run is claimed; no production C++ changes occur.

## GPU result: reference coverage regressed

The owned worker completes in **449.453 s**, without timeout/forced kill. Its
**149 samples** observe maximum **582 MiB global GPU use** and **1,532,712 KiB
process VmHWM** with the desktop active; these are sampled development observations,
not isolated peaks or budget qualification. All **18 assets** pass native C++
pose admission at default limits; all 18 wrong-frame expectations reject.
Independent whole-plane checks confirm exact depth/validity/support/confidence
export and zero-normalized unknowns (`independent-depth.json`,
`admit-result.stdout`). Across all 18 views there are **2,806,753 valid pixel
observations**, coverage **1.28–25.38%**. That aggregate is not compared with nine
views to claim improvement.

The fixed-reference comparison preserves every reference RGB hash, camera grid,
intrinsics and pose, with unchanged PatchMatch options. **Every reference loses
valid coverage** (`reference-comparison.json`):

| Reference | Temporal coverage | Parallax coverage |
|---|---:|---:|
| 1 | 3.34% | 2.82% |
| 2 | 5.02% | 2.60% |
| 3 | 7.34% | 3.07% |
| 4 | 9.76% | 3.41% |
| 5 | 9.89% | 3.57% |
| 6 | 8.92% | 4.42% |
| 7 | 7.43% | 4.84% |
| 8 | 7.65% | 4.93% |
| 9 | 8.75% | 5.36% |

Fixed-reference valid observations fall **1,537,092 → 790,453**. Sparse-track
quorum gains do not predict this recipe's dense yield. The candidate is **not
promoted as a quality improvement**; neither result has geometry ground truth,
so mask density alone does not rank reconstruction accuracy. Both results remain
available, and no threshold/source configuration was changed after this result.

An independent diagnostic samples **768 reference positions per source pair**
and projects unfiltered photometric depth to the source and back, using the actual
serialized cameras and bilinear source depth (`roundtrip.json`). Mean pairwise
in-bounds fraction falls **86.40% → 48.23%**; median round-trip error rises
**17.48 → 75.02 pixels**; the fraction within one pixel falls **17.70% → 5.62%**.
This is consistent with weaker dense visibility/consistency for the farther views;
it does not isolate the cause, reproduce COLMAP filtering or establish geometry
accuracy. The first observer incorrectly applied the strict geometric-depth
reader to private unfiltered maps with negative invalid sentinels
(`roundtrip-initial.log`); the corrected diagnostic checks bounded raw maps,
requires finite values and treats nonpositive values as unknown. Production
geometric-depth admission remains unchanged.

Additional actual commands (repository root): native
`python3 artifacts/validation/m3-dense-parallax-20261018/run_gpu.py`;
`.venv-dense/bin/python artifacts/validation/m3-dense-parallax-20261018/check_depth.py`;
`python3 artifacts/validation/m3-dense-parallax-20261018/admit.py`;
`python3 artifacts/validation/m3-dense-parallax-20261018/compare_references.py`;
`.venv-dense/bin/python artifacts/validation/m3-dense-parallax-20261018/check_roundtrip.py`.
All final commands exit **0**. Exact GPU/admission argv and cwd remain in
`real-execution.json` / `admit-commands.json`. The ignored C++ harness passes
repository format/style checks and separate clang-tidy **23.1.1**, warnings as
errors (`harness-style.log`, `harness-tidy-command.json`).

Preservation `audit.json`: **22,574 distinct protected entries**, zero missing or
changed, combining 2,210 pre-task entries with the earlier dense baseline.
Saved sources/runs, frozen fixtures, prior dense/resize experiments and pose/
training environments remain intact. Final worker bytes match the frozen recipe.
Documentation/whitespace checks pass. The dependency inventory still exits **2**:
`Changed evidence: cmake/dependencies.cmake` (`dependency-current.log`); stale
runtime/mapping/owner receipts and redistribution rights remain open.

Next: add spatial visibility/photometric consistency evidence to source ranking
using the retained maps before another changed recipe. Do not repeat this run or
lower filters. Production job/control/resume, full-scene depth, fusion/compression,
measured scale, collision/walking and mesh export remain open. A0–A6/AP gates stay
unstarted; M4 has not begun. No upload, remote push, appearance refit, pose retry
or long-stress rerun occurs.
