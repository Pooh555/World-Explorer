# Spatial and texture reliability — 2026-10-18 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. Denser CPU sampling improves aggregate cached
support but regresses two references and high-texture support. No new GPU recipe,
solve, admitted geometry, fusion/scale/walk, Q5/N2, asset or platform qualification.
Parent **c298262**; implementation is the commit containing this report. Cwd:
repository root. Ignored evidence **R**:
`artifacts/validation/m3-dense-reliability-20261018`.

## Frozen diagnostic

The [implemented contract](../../workers/dense/README.md#spatial-and-texture-reliability-diagnostic)
owns CLI, sampling, failure accounting and limits. `reliability.py plan` freezes
training evidence and closed six-source rows for every retained cache vertex;
`check` verifies the pinned receipt and recomputes training before evaluating the
same rows. All first nine references remain. Rehashed rows/statistics cannot
bypass recomputation; evaluation does not rank/select sources.

The **128×96** grids have disjoint reference centers at quarter/three-quarter-cell
phases, **8×6** interior tiles and reference variance classes `FLAT` <1e-4,
`LOW` [1e-4,1e-3), `HIGH` ≥1e-3. Every sample, including unknown/unsupported
ones, remains in its denominator. Six border pixels are unsampled; patches/source
warps may overlap across phases. This is the same capture/cache, not independent
held-out validation. The previous support gates and greedy objective remain;
there is no tile/texture reweighting, backend option or acceptance-threshold change.
`freeze.json` predates training/evaluation; `training-pin.json` predates evaluation.

| Identity | SHA-256 |
|---|---|
| Existing 18-view private photometric receipt | `86d027a96ced2abb84d64a851075112b7acfd5b56218105cd588fa2a9573bab3` |
| Current six-file diagnostic implementation | `225695647aefe42978d9c8af39e964cebd06413cbf5d10e106e8f45c1b62dd07` |
| Frozen training receipt | `54911c05bf34044d55818efcbaa792a844dd2e1c8938e497da87e1816e1601d5` |
| Evaluation report | `a6f8db8f4760b0f7d88dda6e5be3459a25c81945c114a738d6d5e42ac1449e45` |

Cache metadata embeds the exact native/resized/model/image/recipe identities;
those inputs and the prior seven-file GPU worker bundle remain unchanged.
Private photometric maps are not admitted geometry. Their new receipts pin
current bytes; the original producer terminal did not hash those maps.

## Results and decision

Support is a sampled diagnostic intersection, not measured reconstruction yield:

| Reference | Prior graph: evaluation support | Denser candidate: evaluation support |
|---|---:|---:|
| 1 | 109 | 78 |
| 2 | 121 | 112 |
| 3 | 113 | 116 |
| 4 | 118 | 136 |
| 5 | 148 | 178 |
| 6 | 208 | 209 |
| 7 | 205 | 216 |
| 8 | 231 | 263 |
| 9 | 248 | 271 |
| Total / 110,592 sampled centers | **1,501** | **1,579** |

Training support rises **1,565→1,675**; evaluation rises **1,501→1,579**. The
first two references regress. High-texture evaluation support falls **958→930**;
low-texture support rises **543→649**. **96,397/110,592** evaluation centers are
`FLAT`, supplying zero support **by policy**, not proving those surfaces impossible
to reconstruct. Zero-support tiles fall **337→317 of 432**; most remain unsupported.
The temporal graph replayed on this parallax cache yields 110 training/130
evaluation support samples; these are not results of a new temporal MVS solve.
No policy was selected or retuned on evaluation. The candidate remains
`gpu_recipe_ready: false`; the aggregate gain does not justify a new solve.

`compare_caches.py` freezes the retained temporal cache and checks identical
RGB/pose/calibration/frame identities for the first nine views. It evaluates
**the same 42 temporal camera pairs**, **12,288 centers/pair**, in both caches.
Per-pair relative axial-depth p90 is conditional on known source depth and
positive returned Z; the table is the **median of those 42 per-pair p90s**,
not a pooled p90 or error against ground truth:

| Reference texture | Samples/cache | Known-return samples: temporal / parallax | Median pair p90: temporal / parallax |
|---|---:|---:|---:|
| FLAT | 450,672 | 389,252 / 394,726 | 8.222 / 8.702 |
| LOW | 39,499 | 36,538 / 35,650 | 1.052 / 5.083 |
| HIGH | 25,925 | 24,853 / 24,361 | 0.578 / 3.506 |

Relative differences are unitless (1 = 100%). Both caches have large disagreement
tails; the parallax cache also worsens in textured regions. This does not isolate
occlusion, bad depth or another cause, and does not certify geometry accuracy.
First-failure accounting is ordered rather than causal. Full per-pair, tile,
class and joint tile/class results remain in `training.json`, `evaluation.json`
and `cache-comparison.json`; `summary.json` checks all denominator partitions.

## Actual checks

Linux x86-64; isolated Python **3.12.15**, NumPy **2.2.6**, Pillow **11.3.0**,
COLMAP **4.2.1**. A fresh installed audit matches **six packages / 1,188 listed
files** and exact requirements. No installs/downloads or native viewer/GPU solve.
Elapsed CPU runs below are development observations, not P1 or performance gates;
test profiles/observers also ran concurrently.

| Actual command (repository root) | Result / logs under R |
|---|---|
| `python3 artifacts/validation/m3-dense-reliability-20261018/run_cpu.py` | Exact argv/exits in `commands.json`: plan **9.403 s**, check including recomputation **13.153 s**, both 0; `training.log`, `evaluation.log` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-reliability-20261018/compare_caches.py` | Retained-cache comparison above; exact freeze argv/exit in `temporal-freeze-command-retry.json`; `cache-comparison-retry.log` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-reliability-20261018/check_results.py` | Exit 0: all overall/tile/class/joint denominators and failure partitions conserve counts; frozen source rows unchanged, `results.log`, `summary.json` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-reliability-20261018/check_oracle.py` | Independent scalar optical projection/interpolation/NCC checks: **16,114 samples**, **4,621 positive**, zero mismatches; `oracle.json`, `oracle.log` |
| `python3 artifacts/validation/m3-dense-reliability-20261018/check_entry.py` | Nine checksum/policy/code/output/rehashed-plan probes reject with exit 2 and preserve outputs; exact argv/exits in `entry-commands.json`. SIGINT during planning exits **130** after **0.1644 s** signal-to-reap, no forced kill/result, `cancellation.json`. Legacy 768-center numerical report matches exactly excluding changed implementation digest, `legacy.log` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | **47/47**, no skips, **40.229 s**; `unit-complete.log`. Seven new cases cover disjoint dense grids, complete unknown/flat/unsupported accounting, failure populations, immutable/deterministic closure, training-only selection, pre-evaluation forgery rejection, bounded explicit pixels and cancellation |
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | **308/308**, **60.20 s**; `build-dev.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **285/285**, **54.45 s**; `build-cxx23.log`, `ctest-cxx23.log` |
| `.venv-dense/bin/python tools/audit_worker_environment.py --requirements workers/dense/environment.txt --output artifacts/validation/m3-dense-reliability-20261018/environment.json` | Exit 0, exact installed-byte match above; `environment.log`; no redistribution-rights qualification |
| `python3 tools/check_dependency_manifest.py --local-evidence` | Existing exit **2**, `Changed evidence: cmake/dependencies.cmake`; `dependency-current.log`; runtime/mapping/owner refresh remains due |

The comparison observer initially resolved the virtualenv interpreter symlink to
the host interpreter; the child exited 2 (`No module named numpy`) and the owner
exited 1. Keeping the virtualenv executable path fixes it without installation.
Initial command/logs remain as `temporal-freeze-command.json`,
`temporal-freeze.log`, `cache-comparison.log`; successful retry files are separate.
No product parser/filter expectation was weakened. CPU cancellation does not
qualify GPU cooperative cancellation or production control/resume.

`python3 tools/check_docs.py` passes **797 local targets / 168 files**;
`git diff --check` passes. `python3 artifacts/validation/m3-dense-reliability-20261018/audit.py`
checks **22,999 protected files**, zero changed/missing, current source/observer
identities and unchanged GPU bundle (`audit.json`). Saved videos/jobs/failed
stages/frozen fixtures, prior outputs and pose/training environments are preserved.
No C++ changes, new sanitizer/coverage/native platform claims, pose retry, completed
refit, stress rerun, push or upload. [Progress](../progress.md#next-executable-step-and-protection-rules)
owns the next task: a native pose-admitted geometric-depth visibility receipt,
with unknown coverage explicit, before another source-ranking objective or real
fusion. Arbitrary scale and partial geometry cannot certify Q5/N2 or walking.
