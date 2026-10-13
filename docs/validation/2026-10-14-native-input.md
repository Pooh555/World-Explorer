# Native input and live HiDPI — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/N1/E1 IN_PROGRESS**. Native compositor-delivered control and
scale checks on the retained full real scene; no complete gate certification.
Cwd: repository root. Parent `65deec0367939dd85bfe7b8fac0fcf22d5a21a34`;
implementation is the local commit containing this report. Ignored raw evidence:
`R = artifacts/validation/native-input-20261014`; `identity.json` binds changed
code, both binaries, request, runners, protocol, traces, logs and PPM readbacks.

## Repairs and observations

A delivered native bookmark sequence (Source → Save → Fit → Return) reproduced
correct restored pose but wrong FOV: **1.2181299927062113 → 1.0471975511965976**.
Session bookmarks now restore FOV with the scene/epoch-validated camera.
`pointer-result.json` retains the failing comparison; `complete-result.json`
restores **1.2181299927062113** exactly.

At SDL display scale/pixel density **2/2**, startup-only display scaling doubled
logical layout and stretched the default font. Layout now uses display scale /
pixel density each frame; absolute theme scaling and an immutable 13-size atlas
avoid accumulating padding or rebuilding GPU font textures. Decision and primary
platform rationale: [ADR 010](../adr/010-window-and-framebuffer-scale.md).

Live resize also exposed Import's appearing-only size: Close remained at logical
Y **667–692** after shrinking to **1024×640** (`dpi-final.jsonl`). Import is now
centered and constrained to the current viewport; its details remain scrollable.
Final live **2→1.5→1→2** transitions preserve layout scale **1** and visible
Review/Close. Readbacks are **2048×1280, 1536×960, 1024×640, 2048×1280**;
`dpi-fixed-state.json` and inspected `dpi-fixed-*.png` retain the actual views.

## Actual commands/results

GCC 16.2.1 C++26 and Clang 23.1.1 explicit C++23/libstdc++; existing development
profiles. Native Linux/Hyprland 0.56.2, SDL3, RTX 4060 Laptop Vulkan validation.
Physical eDP-1: 2880×1800, scale 1. Temporary owned Wayland headless output checks
compositor scale delivery, not a physical HiDPI monitor or another OS.
Every successful command below exits 0; R is expanded to the path above.

| Command | Actual result / raw evidence |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | Both final builds pass; `build-resize.log`, `build-clang-resize.log` |
| `python3 R/run_checks.py dev`; `python3 R/run_checks.py clang` | **121/121 each**; exact CTest argv/exits in `*-commands.json`, counts in `*-0.log` |
| `python3 R/run_checks.py focused` | `[dpi]`: **2 cases / 50 assertions**, invalid scales, physical/logical font invariants and nonaccumulating theme; `focused-0.log` |
| `python3 R/run_checks.py tidy` | Three changed translation units pass clang-tidy 23.1.1 `--warnings-as-errors=*`; final resize-only main rerun also passes (`tidy-main-resize.log`) |
| `python3 R/run_checks.py static` | clang-format 23.1.1, boundaries, documentation and diff checks; exact argv/exits retained |
| `python3 R/run_input.py complete` | **18/18** native input assertions, exit 0; `complete-result.json`, `complete-commands.json`, trace/readbacks/log |
| `python3 R/run_dpi.py dpi-fixed` | Four live scale transitions, bounds/visibility assertions, exit 0; `dpi-fixed-commands.json`, state/trace/readbacks/log |

The native input driver uses targeted Hyprland keyboard events and an ignored
Wayland virtual-pointer helper. The small official [MIT protocol XML](https://raw.githubusercontent.com/swaywm/wlr-protocols/master/unstable/wlr-virtual-pointer-unstable-v1.xml)
was fetched explicitly, copyright retained, bindings generated with installed
wayland-scanner 1.26.0. The helper builds with C17 `-Wall -Wextra -Werror` and
wayland-client 1.26.0; production C++ standards remain unchanged. Drivers restore
prior desktop focus/cursor and remove their temporary output.
Initial `before`/`before2` attempts could not engage capture: compositor cursor
warps did not deliver usable SDL mouse motion. Those are runner failures, retained
without claiming viewer failure. Virtual-pointer delivery resolved this. The
initial driver reported failed assertions in JSON but returned 0; the final
driver exits nonzero on any failed assertion. `dpi-final` exits 1 on clipping;
final repaired runs exit 0.

Final input checks cover Import blocking movement, W movement, Space/Ctrl along
viewing up, Shift acceleration, wheel speed, relative mouse look, focus-loss and
Escape release, source reset, bookmark pose/FOV, remap acceptance/old-key blocking/
new-key movement, compact review/Close visibility and clean exit. Native input
and DPI logs each retain **295,952 resident splats / zero Vulkan errors**.
Final input owned buffers **218,896,832 bytes**, 1,653 frames; DPI owned buffers
**434,233,920 bytes**, 121 frames. These are final counters, not peak memory/FPS.

SHA-256/size comparison against the preceding worker audit preserves **2,393
files**, including all videos, fit/checkpoints/scenes, failed poses, requests/logs,
AGENTS/runbook/specification. No decoder, solve, fit, scene publication, upload or push.
Review remains read-only; all diagnostic settings/output are isolated under R.

Remaining: human/photodiode input-to-display latency, event-to-state frame timing,
reference hardware, physical/native Windows/macOS HiDPI, full contracts/stress,
prescribed ≤250k fixture, metric/backbone/recipe locks and independent quality
corpus. Flushed traces/readbacks change timing; headless scaling and injected
input cannot certify N1 or another platform. No scene threshold was changed.
