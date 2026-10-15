# M1 development exit review — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1 VERIFIED on the available Linux/NVIDIA development profile**
against [plan v1.1 §8.1](../technical_plan.md#81-m1-development-exit-and-hardware-qualification).
This is a review of retained real outputs, the frozen synthetic viewer fixture,
current-commit hosted CPU results and selected dependency evidence. It does not
certify Q/P/N/E gates, reference machines, physical Windows/macOS viewers or
redistribution rights. Cwd: repository root; reviewed source commit
`788db850ea7240d36b9dbfd36842457b2839831e`. Ignored raw review root **R**:
`artifacts/validation/m1-exit-review-20261015/`.

| §8.1 item | Reviewed evidence and result |
|---|---|
| 1. Real chain | The user-authorized `sample_1` source is the original consented 20.948333-second H.264 3840×2160/30 room capture, SHA-256 `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`. Current `ffprobe` and SHA check agree. [Saved recovery](2026-10-13-model-selection.md) selects the full 63/63-frame COLMAP model; [saved fit/viewer](2026-10-13-service-orientation.md) records 7,000 steps, 295,952 splats, validated publication, native display and zero Vulkan errors. `artifacts/sample_1/logs/reconstruction.log` records the actual CLI decode/pose/fit/publication sequence and preserved earlier rejection. Current CLI bundle validation exits 0. The inspected local viewer PNG visibly contains the reconstructed room, with arbitrary-scale/walk-unavailable labels. R/selected-inputs.json binds current request, pose result, fit result, scene manifest, terminal result and PNG bytes. |
| 2. Fixture, controls and contracts | [Frozen fixture](2026-10-15-viewer-fixture.md) loads all 250,000 SH3 splats at 1280×720 on the Linux RTX 4060 Laptop, with readback, native GUI loading and zero Vulkan validation errors. [Native input](2026-10-14-native-input.md) exercises delivered fly controls on the full real scene. [Scene replay](2026-10-11-capture.md), [viewer regression](2026-10-13-viewer-regression.md) and current CPU contracts cover replacement/deletion, admission, retries, wrong base/epoch and resync. The frame, pose, map/scene and update interfaces are implemented and documented in their project protocols. Synthetic fixture/replay evidence remains labeled synthetic. |
| 3. Hosted CPU portability | Public [native-builds run 38682707360](https://github.com/Pooh555/World-Explorer/actions/runs/38682707360) completed successfully for exact reviewed commit `788db85`, attempt 1. Its Linux GCC, Linux Clang, macOS and Windows jobs each report successful explicit C++23 configure/build/CPU-test/boundary/documentation steps. R/runs.json and R/jobs.json retain the public API responses and SHA-256 identities. The workflow sets `WE_BUILD_VIEWER=OFF`; this is CPU portability evidence only. Individual CTest counts, interpreter skips, macOS architecture and installed compiler versions were not retrieved. |
| 4. Identities, selected rights, observations | [Training/pose identities](2026-10-13-worker-identity.md), [separate metric lock](2026-10-14-metric-identity.md), [dependency inventory](2026-10-14-dependency-closure.md) and [current runtime refresh](2026-10-15-runtime-refresh.md) pin selected versions/bytes and audit selected notices. The current local manifest check finds 82 components, 249 open source/rights gaps and `rights_qualified: false`; full distribution clearance remains M6 work. [Fresh owned chain](2026-10-13-fresh-chain.md) records local decode/solve/fit/publication times and non-certified Q1/P1 observations; [local renderer timing](2026-10-13-tile-plans.md) records short full-scene rates. They are development observations, not benchmark passes. |

## Checks performed for this review

`R/runs.json` and `R/jobs.json` came from anonymous bounded GETs of the public
GitHub Actions run-list and run-job API endpoints, both curl exit 0. Their SHA-256
values are `2abc1f5ccde0966b5df6451aaedac0be3b27b9d7375fc48d0fb83fd9c3ff2f49`
and `95c79df3599d2e4fe59d9d4ccc27cb72993e4ac8bdfcf565cd7e719c0134244f`.
Read-only assertions checked exact head SHA, four complete successful jobs and
the required successful steps. The sandbox DNS attempt failed first; the
approved read-only retry succeeded. No remote workflow was started.

`cmake --preset dev` and `cmake --build --preset dev -j 4` succeeded; the build
had no work. `ctest --preset dev --output-on-failure` reports **267/267 passed**
in **124.51 s** in R/ctest-dev.log. The wrapper then exited 1 because its zsh
variable `status` is read-only; this occurred after CTest completed and did not
alter the CTest result. `build/dev/we_process validate` exited 0 for both saved
`sample_1` and `sample_3` scenes, each revision 1/one chunk. `ffprobe` exited 0
for the current `sample_1` bytes. The current dependency audit prints local
evidence checked and rights false in R/dependency-local.log; its first wrapper
hit the same post-command zsh-variable mistake. The final direct audit exit is
**0**, recorded separately in R/dependency-local-final.log. After the handoff
edits, `python3 tools/check_docs.py` exits 0 (**563 local targets / 118 files**),
`python3 tools/check_boundaries.py` exits 0, and `git diff --check` exits 0.

The reviewed native host in the linked reports is Arch Linux / RTX 4060 Laptop,
driver 615.78.08, GCC 16.2.1 C++26 and explicit C++23/Clang checks. Its results
are not H_R, H_V or H_M measurements. No source, fit, scene or fixture was
rewritten or rerun for this review. Physical Windows/macOS and reference-device
viewer/FPS checks remain assigned to M4; P1 and Q3 qualification remain later
milestones. The disconnected `sample_2` attempt remains a recorded failure.
