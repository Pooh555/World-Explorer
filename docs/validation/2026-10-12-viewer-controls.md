# Viewer controls and snapshot handoff — 2026-10-12, Asia/Hong_Kong

Parent: `12c764f`; implementation commit contains this report. Cwd: repository
root. M1/N1 remain IN_PROGRESS; no remote push/upload or reconstruction rerun.
[Viewer contract](../protocols/viewer.md) records settings and completion semantics.

## Change and identity

New/reset profiles now use specified Space/Ctrl vertical movement and Shift
acceleration (§4.8/runbook §6). Existing schema-1 bindings load exactly; the old
heuristic incorrectly rewrote specification defaults to the reversed mapping.
The overlay displays active scancode names. Exactly seven bounded integer keys
are required: excess/fractional/overflow values previously truncated into valid
bindings. Invalid files retain current in-memory settings atomically.

Native inspection also exposed a completion race: the loader published a snapshot
and cleared `busy` after that frame's UI poll; automatic exit treated the scene as
missing. `has_pending_work()` now checks busy/published snapshot under the mutex.
Startup reconstruction, screenshot and bounded exit wait for this handoff and
existing renderer visibility acknowledgement.

`identity.json`: 130 sorted `CMakeLists.txt` and `.cpp`/`.hpp`/`.fbs`/`.py` files
under `apps/include/src/tests/schemas/workers`, relative UTF-8 path + NUL + binary
file SHA-256 → `a63848c2c3e178ebd86fbe8a157845a087926107f8dcbf9fc69649832ac91240`.
Viewer SHA: `68331c9bcfd88caaf9bb79acd1d4951dc69cdca3f1ab783f719f777c18f67211`.

## Checks

Arch development host/toolchain as in [calibration evidence](2026-10-12-fit-calibration.md).
Ignored logs/settings/images/identity: `artifacts/validation/fly-bindings-20261012/`.

| Actual command/check | Result |
|---|---|
| `build/dev/we_viewer_tests` before fixes | exit 42; two preference cases expose mapping/narrowing failures; `tests-before.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure` | exit 0; **87/87** (84 contracts/worker + three viewer cases); `build-final.log`, `tests-dev-final.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 -N` | exit 0; 84-test inventory, viewer tests omitted; no repeated CPU test pass claimed |
| `clang-format --dry-run --Werror` on six changed C++ files | pass |
| `clang-tidy -p build/dev apps/we_viewer/preferences.cpp apps/we_viewer/main.cpp apps/we_viewer/job_state.cpp tests/ui/preferences_tests.cpp tests/ui/job_state_tests.cpp --warnings-as-errors='*'` | exit 0; no unsuppressed diagnostics; `tidy-final.log` |
| `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | exit 0 |

Viewer cases require SDL/ImGui only in viewer-enabled builds, with no GPU execution:
exact specified/old/custom save/load, malformed schema/count/type/range preserving
caller state, and a published snapshot remaining pending after busy clears.

## Native inspection and retained failures

Before the handoff fix, bounded scene runs at 60 and 120 frames exited 2 with
“Scene validated; waiting for renderer admission.” Those logs remain retained as
`default-view.log`, `default-view-120.log`, `custom-view.log`. Final commands:

```sh
build/dev/we_viewer --scene artifacts/sample2-fit-boundary-20261012/original.wexplore --source-view --max-splats 500000 --frames 1 --screenshot artifacts/validation/fly-bindings-20261012/default-view.ppm --settings artifacts/validation/fly-bindings-20261012/default-settings.json
build/dev/we_viewer --scene artifacts/sample2-fit-boundary-20261012/original.wexplore --source-view --max-splats 500000 --frames 1 --screenshot artifacts/validation/fly-bindings-20261012/custom-view.ppm --settings artifacts/validation/fly-bindings-20261012/custom-settings.json
```

Both exit 0 with 304,867 splats, 220,342,032 renderer-owned allocation bytes,
zero Vulkan/sync/shutdown errors on RTX 4060 Laptop / driver 615.78.08. They submit
181/156 frames: the one-frame minimum waits for visible admission/screenshot.
Fresh keys are `[26,22,4,7,44,224,225]`; custom `[82,22,4,7,44,225,224]` and layout
survive save/reload unchanged. Lossless PNG inspection confirms correct default
and custom overlays at 1428×900 / 1428×1776; these differing viewport sizes are
not a rendering/performance comparison. Private scene assets were only read.

No manual keyboard/mouse, focus/modal, HiDPI or latency qualification is claimed.
Native Windows/macOS viewer, sanitizer/TSan UI paths, reference parity/hardware
and ≤250k fixture remain unqualified. Existing CPU sanitizer results retain the
preceding slice's scope; M7–M8 remain deferred.
