# Cached visibility and photometric scoring — 2026-10-18 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. The pinned CPU diagnostic works locally;
its candidate does not improve separate-grid support, so no GPU recipe is
promoted or executed. No geometry/fusion/scale/walk, Q5/N2 or asset/platform gate.
Parent **e3fd665**; implementation is the commit containing this report. Cwd:
repository root. Ignored evidence **R**:
`artifacts/validation/m3-dense-visibility-20261018`.

## Implemented diagnostic and identities

The [cache/scoring contract](../../workers/dense/README.md#cached-visibility-and-photometric-diagnostic)
owns commands, bounds, interpolation, ranking and failure semantics.
`cached_sources.py freeze` verifies the selected terminal/preparation/input/recipe
chain and pins serialized model, RGB, config and current private photometric map
bytes. The old terminal did **not** hash those photometric maps; this new receipt
records current bytes rather than asserting historical producer-time identity.
`score` rechecks the receipt, parses owned model copies, checks optical cameras
against the manifest, and uses exact bounded finite private map payloads.
Nonpositive depths stay unknown; public geometric-depth admission is unchanged.

The fixed policy combines projected overlap, one-pixel depth round trips,
relative axial depth agreement, parallax and local normalized image correlation.
It ranks complementary two-source support on 768 pixels/view and evaluates a
separate disjoint 768-pixel grid afterward. All 18 cached vertices and the same
nine references remain; source rows are closed and contain six sources.
The same raw maps serve all graph comparisons. A cache depends on its original
source graph; this is neither an independent capture nor a COLMAP filter replay.
Reports explicitly set `gpu_recipe_ready: false`.

| Selected identity | SHA-256 |
|---|---|
| Native manifest, unchanged | `495084082f7e1f5e272c700aa836d1143f2352320a1e85d8b8221fd7276e48f8` |
| Resized manifest, unchanged | `c3472aec25d65f8565021e3c793f184890fb167e95e3a7dd89ae5235c4d7954f` |
| Prior GPU recipe, unchanged | `ddf53ea6222e079abc6c023f5470f486ed29364991797d525d4e38a04bf125b7` |
| Selected prior terminal | `64f7971aa7767ee31dfb0cfc106517622ffacbca933087048453b3a7cb7f6ab7` |
| New private cache receipt | `86d027a96ced2abb84d64a851075112b7acfd5b56218105cd588fa2a9573bab3` |
| Diagnostic implementation bundle | `9283038f7fb9aa8c81adfac04e11a2e811ee6de09bb60b2e74faf4420baac351` |
| Final score report | `35f90c03f30f1d521944f1a7b69c11c23de5dcade6e1a7ae4486045cc6f66c66` |

The implementation digest covers the new CLI/scorer and their local helpers;
the prior seven-file GPU worker bundle remains unchanged. No new backend
option, filter, resized RGB/grid, pose, scene or acceptance threshold is selected.

## Real result: no supported promotion

Two-source support counts are diagnostic pixel observations, **not reconstructed
depth coverage, geometry accuracy or independent validation**:

| Reference | Prior parallax graph: separate grid | Candidate: separate grid |
|---|---:|---:|
| 1 | 7 | 3 |
| 2 | 5 | 4 |
| 3 | 4 | 3 |
| 4 | 1 | 1 |
| 5 | 4 | 8 |
| 6 | 14 | 13 |
| 7 | 23 | 22 |
| 8 | 12 | 13 |
| 9 | 14 | 14 |
| Total / 6,912 samples | **84** | **81** |

Ranking-grid support increases **107→126**, but separate-grid support falls
**84→81**; five references worsen, two tie, two improve. The retained temporal
graph evaluated on this same parallax cache yields 14 ranking/9 separate-grid
samples. That is not a rerun or measurement of the temporal recipe's own output.
These low counts do not establish a reliable generalization gain. Initial
`scores.json` and final `scores-final.json` retain the same numerical results;
the latter binds the final safety code. A second final run is byte-identical.
No GPU work, failed-reference exclusion or threshold relaxation follows.

A supplementary internal sparse-track check (`sparse-depth.json`) samples axial
depth at projected distinct landmarks, collapsing repeated same-view landmark
references only for statistics. Parallax photometric p90 relative Z disagreement
is **64.85–407.57%** per reference, versus **25.01–50.91%** temporally. Median
disagreement remains **1.32–2.75%** versus **1.61–2.30%**. Positive geometric-depth
interpolation covers **361–719** landmarks/reference versus **78–182** temporally,
despite the prior overall dense-coverage regression. This shows why agreement at
textured sparse tracks cannot stand in for spatial completeness. It does not
isolate a cause or certify accuracy: the comparison shares the reconstructed
poses/model, and visibility differs. Raw per-view results retain unknown fractions
and a separate ≤1-pixel feature-reprojection stratum.

## Actual checks

Local Linux x86-64; existing isolated Python **3.12.15**, NumPy **2.2.6**, Pillow **11.3.0**, COLMAP
**4.2.1**. Fresh installed-byte audit matches **six packages / 1,188 listed files**
and exact requirements. No installs, downloads, GPU solve, new native viewer,
sanitizer, platform or performance qualification. Elapsed CPU observations below
are single development invocations, not benchmark distributions or whole-video P1.

| Actual command (repository root) | Result / evidence under R |
|---|---|
| `python3 artifacts/validation/m3-dense-visibility-20261018/run_cpu.py` | Exact freeze/score argv in `cpu-commands.json`; exits 0 in **1.768 / 2.071 s**, `freeze.log`, `score.log` |
| `python3 artifacts/validation/m3-dense-visibility-20261018/final_cpu.py` | Exact final/repeat/five reject argv in `final-commands-checked.json`; exits 0/0 and five expected 2s. `summary-checked.json`: byte-identical final repeats; SIGINT scoring exit **130**, **0.0636 s** signal-to-reap, no forced kill/report |
| `.venv-dense/bin/python artifacts/validation/m3-dense-visibility-20261018/check_oracle.py` | Independent scalar optical projection/interpolation/angle/NCC oracle: **14,357 samples**, including **2,866 positive** samples, zero mismatches; `oracle.json`, `oracle.log` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-visibility-20261018/check_sparse_depth.py` | Both retained caches inspected without solve; `sparse-depth.json`, `sparse-depth.log` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | Final **40/40**, no skips, **1.694 s**; `unit-final.log`; ten new cases cover analytic asymmetric-plane projection, unknown/occluded/behind-camera depth, inverted/textureless/exposure-varied images, arbitrary scale/rotation, disjoint grids, deterministic closure, cancellation and cache safety |
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | **308/308**, **26.69 s**; `build-dev.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **285/285**, **26.19 s**; `build-cxx23.log`, `ctest-cxx23.log` |
| `ctest --preset dev -R worker_integration --output-on-failure` | Final safety/test guard changes rechecked: **1/1**, **3.07 s**, `ctest-worker-final.log`; C++ unchanged |
| `.venv-dense/bin/python tools/audit_worker_environment.py --requirements workers/dense/environment.txt --output artifacts/validation/m3-dense-visibility-20261018/environment.json` | Exit 0; exact installed match above, `environment.log`; redistribution rights remain open |
| `python3 tools/check_dependency_manifest.py --local-evidence` | Existing exit **2**, `Changed evidence: cmake/dependencies.cmake`; `dependency-current.log`; loader/mapping/owner refresh remains due |

Rehashed synthetic metadata cannot bypass actual model pose/calibration checks.
Changed map/image/model bytes, parent symlinks, nonfinite/trailing/wrong-grid
maps, wrong dependency hashes, extra/escaping inventory, resource limits and
occupied/overlapping outputs reject. Failed publication/cancellation preserves
caller bytes. The analytic plane and real scalar oracle pass; the candidate's
separate-grid regression is retained as a negative result, without retuning on
the evaluation grid. CPU cancellation does not qualify GPU/job cancellation.

The first observer command collector retained a mutable argv list, so its first
recorded output path changed when the repeat command was built. The collector
now snapshots argv; all final/repeat/reject/cancel probes were replayed into fresh
paths. `final-commands-checked.json` owns the corrected commands; the initial
collector output and logs remain. Both versions produce the same score-report hash.

`python3 tools/check_docs.py` passes **791 local targets / 167 files**;
`git diff --check` passes. `python3 artifacts/validation/m3-dense-visibility-20261018/audit.py`
checks **22,938 protected files**, with zero changed/missing, current diagnostic
source/observer hashes and the unchanged GPU bundle (`audit.json`). Saved
videos/jobs/failed stages/frozen fixtures, earlier outputs and pose/training
environments are unchanged; no push/upload, refit or old solve/stress retry.
The exact next implementation task lives in [progress](../progress.md#next-executable-step-and-protection-rules):
spatial/texture-stratified reliability and denser independent pixel grids before
another justified recipe. Production control/resume, real fusion, compressed
storage, measured scale, collision/walk and mesh export remain open.
