# Candidate frame selection — 23 October 2026

Scope: M3 / plan §5.1 step 2, Asia/Hong_Kong. **IN_PROGRESS** for product
selection; verified bounds/provenance and synthetic integration do not qualify
Q1, camera cuts/lens stability, 3D coverage or the M3 exit. Parent `edd6e1a`;
implementation is the accompanying slice. `R/implementation.json` pins the 12
changed code files (SHA-256
`78b25d3522da4c6a466a1b96bd81ad0eb437d9f47888936a405094a05cee1206`).
Repository root is the command cwd.
Ignored evidence root `R=artifacts/validation/m3-frame-quality-20261023`.

## Implementation and checks

The explicit `quality_keyframes_candidate_v1` profile adds standard-library
thumbnail quality/feature matching, a frozen unqualified policy, retained
candidate diagnostics, checked native replay and shared decode ownership.
The viewer request review identifies the candidate profile as unqualified.
Legacy sampling stays the default; existing requests/manifests are not migrated.
[Selection protocol](../protocols/frame_selection.md) owns exact semantics.

Commands/results (Linux, GNU 16.2.1; system Python 3.14.8):

| Command | Actual result / log under R |
|---|---|
| `python3 -m unittest discover -s tests/integration -p 'worker_frame_quality_test.py' -v` | Initial missing-module failure retained in `red.log`; final 10 cases pass, `quality-final.log` |
| Same discovery with `-p 'worker_pipeline_test.py'` | 9 cases pass, `pipeline-final.log`; legacy timestamp selection, corruption/resume and failure preservation |
| `.venv-worker/bin/python -m unittest discover -s tests/integration -p 'worker_frame_quality_test.py' -v` | Python 3.12.15 profile, 10 cases pass, `quality-py312-final.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | Build exit 0; 488/488 pass, `build.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | Build exit 0; 463/463 pass, `build-cxx23.log`, `ctest-cxx23.log` |
| `build/dev/we_core_tests '[decoded_admission][quality]'` | 2 cases / 8,075 assertions pass, `native-admission-grid.log`; includes 29 rehashed malformed-ledger variants and preserved caller leases/files |
| Exact LLVM 23.1.1 `python3 tools/check_cpp_style.py` on nine changed C++ paths | Format/tidy/whitespace exit 0 in `style-final.log` (seven paths) and `style-ui.log` (two UI paths), with final admission updates in `style-grid-binding.log`; no warning changes |

The final UI label and additional candidate-rate parser section pass the two
focused development cases in `ctest-final-focused.log`; the C++23 parser case
also passes (`ctest-cxx23-final-focused.log`). Final size-literal conformance and analysis-grid binding are checked by
`style-grid-binding.log` and the rebuilt admission executable. The latter rejects
fractional grid dimensions and a self-consistent transform for the wrong PNG
grid; both were added during final review.

`cmake --build --preset asan -j 4 --target we_core_tests` initially fails during
sandboxed Catch discovery because LeakSanitizer cannot run under ptrace
(`build-asan.log`). Native build retry exits 0 (`build-asan-native.log`);
`build/asan/we_core_tests '[decoded_admission][quality]'` then passes **2 cases /
8,075 assertions**, without sanitizer reports (`asan-admission-grid.log`, final
build `build-asan-grid.log`). The same two cases also pass explicit C++23
(`cxx23-admission-grid.log`). One native
run approval review timed out; its single authorized retry succeeded. ASan,
UBSan and leak detection remain enabled (GNU 16.2.1 / C++26). The prescribed pinned Black tracked-file check and explicit new-file checks
pass; `python3 tools/check_docs.py` passes 1,187 targets in 234 files, and
`git diff --check` passes. Multi-file
Black stalled and was terminated; the prescribed per-file pinned Black command
is used. Initial tidy errors (enum storage, test macro grouping and optional
access) were corrected; failing logs remain. No dependency/runtime or worker
package environment is changed.

## Real local validation and preservation

`sample_1` source SHA
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`
was processed only into fresh `R/sample1-candidate` and `R/shared-sample1` jobs.
`run_candidate.py sample1-request.json` fails exit 2 after **51.266 s**.
The compiled `decode_driver.cpp` calls actual `inspect_video` / `decode_video`
with the framed worker protocol; the same full source also fails exit 2. Both
retain **63 candidates: 1 seed, 1 low-motion, 2 blur/texture, 59 tracking breaks**.
Only source index 0 is tentatively selected; no decode completion/scene exists.
The shared-path ledger SHA is
`5c2addaee89fc8212204cef756a310e72ba82a178ec8532d395938f941ca648b`;
`shared-real-analysis.json` records the exact implementation and policy hashes.
A tracking failure is not proof of a cut. No threshold is tuned to hide it.

A synthetic translated-texture FFV1 video passes the actual shared command:
8 candidates → original indices 0/2/4/6, four full-resolution PNGs and native
catalog observations. Its completion SHA is
`693ea200fd85293dcb78bb84ddfbf286343d11a1be1f274ffa5d6a940b250e5f`.
The final CLI also replays this retained stage successfully
(`replay-synthetic.json`); `commands.json` records standalone driver compilation
and shared invocations. This validates publication/admission, not real reconstruction. The source-change
and cancellation fixtures retain partial thumbnails without publishing a ledger
bound to changed data or replacing the previous result.

`preservation-before.json` / `preservation-after.json` check **155 files, zero
changes**: all three local videos, saved job JSON and retained real baked output.
No solve, fit, depth, fusion or bake runs. Default dependency inventory exits 0;
`--local-evidence` exits **2**, `Current runtime payload differs`, because the
viewer/CLI were rebuilt. Prior receipts and all 257 rights/source gaps remain;
this slice does not rebind them to new bytes.

## Remaining

The last-selected-frame patch reference loses support during ordinary real
motion. Develop a separately versioned persistent tracker with gradual motion,
feature turnover and low-texture fixtures before another fresh real run.
Visual lens-change detection, useful spatial coverage and threshold qualification
remain open. Preserve candidate v1 and every retained failure. M3 remains
IN_PROGRESS; A0–A6 and M4 have not begun.
