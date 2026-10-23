# Conservative calibration preflight — 23 October 2026

Scope: M3, plan §5.1 steps 2–3, Asia/Hong_Kong. **VERIFIED for the local
preflight/admission slice; M3 remains IN_PROGRESS pending its exit review.**
Image-scale alarms are ambiguous; lens identification, automatic calibration
segmentation, fixed-intrinsics certification and full Q/P/N/E gates remain open.

Repository root is the cwd; parent `b0d65ac`. Evidence root
`R=artifacts/validation/m3-calibration-preflight-20261023`.
`final-implementation.json` binds 15 code/header files, SHA-256
`7aafc9a07f4ff4b1a358cf231be77a5d70d80a49f185d8efb9d1f558522623c9`.
The earlier `implementation.json` identifies the real observation. Subsequent
changes protect selector helpers from reconstruction-output overlap and fix the
test optional guard/error wording; the tracking and calibration algorithms are
unchanged. `worker-real-run.py` retains the observed worker bytes.
The public replay header's profile comment is updated after build checks;
its separate style gate passes (`style-header.log`), without another build.

## Behavior and local integration

The explicit v3 candidate profile adds bounded similarity fitting and connected
scale history to v2 correspondences. Native admission checks support, finite
values, references, chain arithmetic and alarm thresholds; both Python helper
hashes bind the implementation. [Protocol](../protocols/frame_selection.md)
owns the thresholds and unsupported cases. Policy `candidate_3_0` is frozen in
`frozen-policy.json` before real processing, SHA-256
`8e47890cb3173d4d9ec1607b1b63b7959b19268ae46896a24bf1e25cbc51b1ae`.
Timestamp sampling stays the default; v1/v2 policy digests and selection behavior
are preserved. Whole-job path validation now protects all selected helpers,
including v3's imported tracker, before any stage writes.

Synthetic zoom-in/out and gradual zoom trigger; translation/in-plane rotation
retain v2 decisions. A pinhole plane fixture supplies identical pixels for focal
change and forward motion, proving why the alarm cannot identify a lens change.
Unsupported intervals explicitly claim no assessment. The actual FFmpeg worker
rejects zoom while preserving the preceding successful timestamp decode,
completion and terminal selection. Native shared decode accepts four synthetic
translation keyframes; fresh/restart exits are 0 with identical completion SHA
`8b1fde1081739b60259eba26a8a21acd064766a72981d3330aca896b409508ff`.
Native zoom exits 2 with `CALIBRATION_REQUIRED`, retained thumbnails/ledger and
no completed decode (`translation-*.json`, `zoom-native.json`).

Real `data/local/sample_1.mp4` retains SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The same native driver selects v3 and calls actual shared inspect/decode. It
**rejects after 59.074 s**: 23 ambiguous-scale observations, 34 insufficient-support
intervals, four assessed and two unassessed (`shared-real.json`,
`real-observation.json`). This is neither confirmed zoom nor a successful
reconstruction. The failed ledger SHA is
`9f97653a47c94b7a77dd1ff30e842c9d439ffc44a509049e34a245345d6fc169`.
All 63 underlying v2 decisions/metrics/tracking records exactly match the
previous completed ledger; all thumbnail bytes match, and 24 selections remain
provisional (`v2-real-parity.json`). No RGB stage, pose recovery, fit, dense,
fusion or bake is published/run. The completed v2 24-pose map is unchanged.
Single-run times are observations under concurrent checks, not performance
qualification. Thresholds are not adjusted to admit this clip.

## Checks and preservation

| Actual command | Result / log under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | GNU 16.2.1 / C++26, 492/492 pass; `build-dev-protection.log`, `ctest-dev-final.log` |
| Same build/test options with `compat-cxx23` | Explicit C++23, 467/467 pass; `build-cxx23-protection.log`, `ctest-cxx23-final.log` |
| `.venv-worker/bin/python -m unittest discover -s tests/integration -p 'worker_frame_*test.py' -v` | Python 3.12.15, 38 cases pass; `frame-py312-final.log`; host Python cases also pass in CTest |
| `build/asan/we_core_tests '[decoded_admission][quality],[decoded_admission][tracking],[decoded_admission][calibration],fresh reconstruction requests*'` | Native ASan/UBSan/LSan, seven cases / 18,567 assertions pass; `asan-final.log`; leak detection enabled |
| Exact LLVM 23.1.1 `python3 tools/check_cpp_style.py` on the eight changed C++ files | Format/tidy/boundaries/docs/whitespace pass; `style-protection-final.log`; no warning changes |

Development failures remain in `worker-red.log`, `build-dev.log`,
`worker-final-first.log` and `style-first.log`: missing profile integration, a JSON
comparison compile error, a test's incorrect incomplete-stage glob and an
unchecked optional in a test. Each is corrected and checked. Pinned Black uses
the required per-file invocation; finish logs own documentation/diff checks.
All **378 protected media/result files** remain unchanged, including every v2
product; four protected instruction/legacy files are unchanged. The v2 helper's
only change is an optional read-only observer hook, covered by exact real-ledger
parity. No environment, source video, saved job or retained bake is modified.

## Current selected runtime evidence

`R/inventory-final` captures the final binaries after the output-protection fix;
the preceding `inventory` run is retained as historical evidence. Only viewer/CLI
change among fifteen selected roots. Fresh loader capture resolves 213 ELF files;
the empty RTX 4060 Laptop viewer runs 900 frames with zero Vulkan errors and 47
mapped ELF files. Four source-identical import observations are reused from the
[earlier refresh](2026-10-23-m3-dependency-refresh.md) and rechecked, not rerun.
The combined mapping has 208 files/137 beyond the baseline/zero changed overlaps;
fresh ownership associates those 137 files with 16 groups/30 notices.

Commands are retained in `capture_loader.py`, `observe_viewer.py`,
`prepare_summaries.py`, `prepare_inventory.py` and their receipts. Owner capture
uses `tools/audit_binding_owners.py --mapping
third_party/backend-bindings-m3-preflight-20261023.json --selection
third_party/runtime-selection-m3-preflight-20261023.json --output
R/inventory-final/owners.json`. Default and `--local-evidence` manifest checks
exit 0; `--local-evidence --require-closure` exits 3, retaining all **257 gaps /
86 components / seven profiles**, `rights_qualified: false`. Comparisons,
profiles and remaining boundaries are unchanged. This supplies current selected
runtime consistency, not redistribution or platform qualification.
