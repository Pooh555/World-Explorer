# Reconstruction processing status — 2026-10-13 Asia/Hong_Kong

Scope: **M1/E1 IN_PROGRESS**, local processing-status behavior and P6 handoff;
no full milestone or Q/P/N/E certification. Cwd: repository root. Parent
`deb607d`; implementation is the local commit containing this report.
Raw evidence: `R = artifacts/validation/progress-status-20261013` (ignored).
Existing jobs, workers, recipe limits and acceptance thresholds are preserved.

The shared coordinator now supplies typed native stages and checked worker
progress to the viewer, including retained adoption/completed reuse. The UI holds
one bounded value, shows stage, actual fit step/request total when available,
whole-job elapsed time and separately labeled backend-event elapsed time, Cancel
and logs on demand. ETA remains unknown. Native stage changes clear previous
step/timing; terminal failure/cancellation names the active stage and retained
output. Publication/load/admission remain distinct; late progress cannot overwrite
terminal status. This does not add a job-wide percentage or predict completion.

Verified wire events are forwarded through inspection/decode/batch-pose/fit
observers. Backend stage names must match the active native stage. Optional fit
metadata is limited to 64 KiB/eight nesting levels and an unsigned integer step
within the checked request limit; unknown or malformed metadata supplies no count.
Finite nonnegative duration is independently checked. Diagnostic text from an
unframed process never becomes typed progress. Wire/worker source/owner identities
are unchanged; no solver or optimizer was rerun.

## Checks

GCC 16.2.1 C++26, Clang/format/tidy 23.1.1 explicit C++23/libstdc++;
Linux/Hyprland, native RTX 4060 Laptop / driver 615.78.08 (**not H_R/H_V**).

| Actual command | Exit / result; raw evidence in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | 0; `build-dev-verified.log`, `build-clang-verified.log` |
| `build/dev/we_core_tests '[progress]'`; `build/dev/we_viewer_tests` | 0; focused verified logs, **105 assertions/four core cases**, **123/six viewer cases**; synthetic contract checks |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | 0; **105/105 each**, verified CTest logs |
| `clang-tidy -p build/dev` on 11 changed C++ translation units, `--warnings-as-errors='*'` | Initial failures retained in `tidy.log`; unaffected units clean, four corrected units rechecked in `tidy-verified.log`, exit 0; no first-party diagnostics |
| `clang-format --dry-run --Werror` on changed first-party C++; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | 0; format/boundary/docs/diff logs |
| `python3 $R/run_viewer.py`; `python3 $R/run_viewer_final.py` | 0; exact argv/results in `native.json` / `native-final.json`, own renderer readback in `ready*.ppm` |

Four core cases cover step bounds/types, invalid durations/stages, retained-stage
ordering, cancellation/failure boundaries, verified synthetic fitter steps and
inspection/decode/pose roundtrips. Six viewer cases include earlier provenance/
pending checks plus terminal success/cancel/failure/published-load failure, stale
progress suppression and an actual owned-thread bad-request reset. These are
contract fixtures, not real training/capture quality evidence. Static analysis
caught empty fallback catches, a single-character string append and optional
access after Catch assertions; explicit fallback returns/character append/guards
correct them without weakening checks. Initial/fixed failing logs are retained.

## Real retained integration and preservation

Both native runs use the unchanged schema-2
`artifacts/sample2-fresh-chain-20261013/reuse.json`, fresh ignored settings and
captures. The first run shows **Preparing camera poses** and **Compiling and
publishing scene**, unknown ETA, elapsed time and Cancel. The second desktop capture
is partial after a window-layout change; three later geometry captures miss the
selected surface after a workspace move and are discarded, not status evidence.
Final screenshots come from the viewer's own GPU readback and show the actual room,
arbitrary scale and walking unavailable. Final stdout records the full **297,489
splats** resident and **zero Vulkan validation errors**. First run submits
2,400 frames; final rebuilt run submits **8,506 frames**, native command exit 0. Final owned GPU buffers are **282,432,512 bytes**, framebuffer
**39,664,128 bytes**. These are final allocations, not peak process/device VRAM;
no FPS, latency or quality qualification is inferred. CPU builds/checks/tidy and
an uncontrolled desktop compete during this diagnostic.

The before/after audit following both runs retains **670 identical files**, including the original
329 protected files, fresh completed stage inventories/RGB/map/fit/checkpoint/
scene/request data and AGENTS/runbook/specification. Mutable fresh worker
`capabilities.json` files are excluded because checked cached workers legitimately
refresh them. Scene/fit/canonical identities remain those recorded in the
[fresh-chain report](2026-10-13-fresh-chain.md). `identity.json` hashes changed
sources, binaries, runners, requests and local evidence. Nothing was uploaded or
pushed; media/generated products remain ignored.

Remaining: actual viewer-thread Cancel/reopen stress, native input/HiDPI and other
platforms, prescribed ≤250k fixture/reference hardware, corpus/independent quality,
full camera parity, geometry/walk and complete gates. Native fit-step display is
checked through synthetic verified transport here; the retained run skips training.
