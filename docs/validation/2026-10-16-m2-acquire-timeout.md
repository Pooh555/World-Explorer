# Interrupted packed stress and acquire retry — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/P4/E1 IN_PROGRESS**. The interrupted packed run retains valid
partial evidence; **two-hour packed qualification is IMPLEMENTED_UNVERIFIED**.
The user explicitly requested no two-hour rerun; none was started. A short native
regression reproduces and repairs the observed acquisition-status handling bug.
Parent `a188606`; implementation is the commit containing this report. Cwd:
repository root. Ignored audit/check root **R**:
`artifacts/validation/m2-acquire-timeout-20261016`. Original run root **S**:
`artifacts/validation/m2-packed-lifetime-20261016`.

## Retained run

`S/full-exit.json` records **exit 2**, no supervisor timeout, **4,019.936 s**
process elapsed, ending **19:41:11 Asia/Hong_Kong**. The log ends with
`acquire swapchain image failed (VkResult 2)`, which is `VK_TIMEOUT`, not a
recorded device-loss error or OS kill. The user's unrelated-program crash may
have affected presentation, but these logs establish no causal link.

`python3 R/audit_interrupted.py` exits **0** before code/build mutation. It checks
the original launch/exit/log hashes and every bound source/binary/shader/scene
file, the complete newline-terminated journal and all available PPM bytes. It
does not alter the original completed-run auditor to accept an early exit.
Original executable/archive bytes are retained under `R/original-binaries/`.

| Audited observation | Result / scope |
|---|---|
| Real scene | Full sample_1, **295,952 splats**, FP32 geometry / binary16 SH, **160-byte stride**; synthetic full/full/empty revision sequence |
| Visible revisions | **267 updates**, consecutive revisions 2–268; last event **4,008.716 s** |
| Resize/capture | **200 resizes**, correct extent cycle; **seven identical 1280×720 captures**, last periodic capture at **3,603.503 s** |
| Retained samples | **66**, each zero Vulkan errors, valid owned-byte budget and exact host/resident/sentinel totals for its visible revision |
| Last sample | **3,960.806 s**, revision 264, **175,293 submitted frames**; later final frame count unavailable |
| Sampled high-water values | Cumulative tracked peak **435,702,080 bytes**, sampled process HWM **414,646,272 bytes**; neither is asserted as the unrecorded final peak |
| Last sampled transfers | **4,167,005,568 host / 4,167,004,160 resident bytes**; final totals unavailable |
| Missing completion | No `summary.json`, final capture or completion event; duration is below 7,200 seconds |

All captures match
`eb000612a21f9fbe894b1c09be9cf6e2c0dc6a5a380ecde1e8b594f93897a98b`.
`R/interrupted-audit.json` SHA-256:
`995d7f78ac71fc5d17ddd678d35c980db7ef6bb6c5823359e4986352b0b14dcf`.
Original exit/journal hashes:
`f23c3f0941accfa25d6c37029f377b073ffe931e4dbf01242617b2151dcabb65` /
`44138aa5447ca02c838ad99487d878ff906f363c6d91308dd4b491ea8848fbf4`.
The launch remains the [pilot report's](2026-10-16-m2-packed-lifetime-pilot.md)
bound `12aa4452…` receipt, with no changed files at termination or initial audit.

## Repair and short checks

The renderer previously sent `VK_TIMEOUT` through its generic failure check.
It now skips acquisition-only deferrals (`VK_TIMEOUT`/`VK_NOT_READY`) and retries
on a later draw, preserving the frame slot, accepted scene update and screenshot.
No commands are recorded or submitted using a missing image index. The existing
finite five-second wait is unchanged; presentation-stall input latency is not
qualified. This follows the [Vulkan WSI acquisition contract](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html):
unsuccessful acquisition leaves its synchronization objects unaffected.

Debug-only one-shot injection bypasses the driver call, preserving an unsignaled
acquire semaphore. Before the repair, the new regression exits expected **2** in
**2.971 s** with the original error (`reproducer-command.json/log`). After repair,
four alternating timeout/not-ready draws preserve the queued revision and pending
capture; the next successful acquisition publishes revision 2 and produces exact
baseline pixels with zero validation errors. Real error/device-loss handling and
release behavior remain checked by the existing smoke path; hooks are absent in
Release. No appearance, packing, schema, recipe or numerical threshold changes.

Actual checks, all following commands exit **0**:

- `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure`:
  GCC **16.2.1 C++26**, **273/273**, **67.95 s**; `build-dev.log`, `ctest-dev.log`.
- `cmake --build build/m2-vma-clang -j 4 --target we_gpu_smoke` and
  `cmake --build build/m2-no-fault -j 4 --target we_gpu_smoke`: independent
  Clang **23.1.1 C++23 Debug** / GCC **C++23 Release**, no warning weakening.
- `python3 R/run_native.py`: bound native argv/cwd/source/binary/shader identities
  and actual exits in `native-launch.json` / `native-exit.json`. GCC/Clang Debug
  each pass the four deferred draws and exact recovery, plus normal **51-frame**
  smoke checks; Release passes normal smoke without fault hooks. The full real
  scene passes windowed/4K/fullscreen/restore, zero Vulkan errors, maximum sampled
  tracked bytes **505,030,208** (`viewer.json`). Native NVIDIA RTX 4060 Laptop,
  driver **615.78.08**; no performance/platform gate is claimed. Compositor
  minimization is denied in ordinary smoke; prior controlled-X11 scope remains.
- Format/tidy **23.1.1**, boundaries, docs and diff checks pass; no new sanitizer
  or full quality/performance qualification is inferred.

The current dependency check exits **2** with an installed ownership/notices
mismatch. A fresh owner capture succeeds; comparison finds only the selected
package-query-tool hash changed, with identical owner groups, ELF and notice
bindings (`owners-current.json`, `owners-current-diff.json`). Rebuilt renderer
executables also need fresh runtime observations; old receipts remain historical.
All **1,382** protected files remain unchanged. No solve/fit/checkpoint load,
dependency provisioning, upload or push occurred. Full gates remain open;
[progress](../progress.md) records the next executable task and no-rerun constraint.
