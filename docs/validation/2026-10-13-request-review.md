# Import request review — 2026-10-13 Asia/Hong_Kong

Scope: **M1/E1 IN_PROGRESS**, checked request presentation and normal reviewed
scene handoff. No complete milestone or Q/P/N/E certification. Cwd: repository
root; parent `db40a53`; implementation is the local commit containing this report.
Raw ignored evidence `R = artifacts/validation/request-review-20261013`.
Worker sources/owners, recipes, limits and specification are preserved.

Import now shows actual pose/appearance actions, video/scene locations, selected
frame/CPU/storage caps and fit step/image/splat limits. Reuse labels fit values as
compatibility requirements; legacy requests do not invent inspection/frame data.
Backend details/manual worker commands stay advanced. Review uses the shared
reader off the UI thread, keeps appearance active and stores at most 32 rows /
128 KiB text, with 2,048-byte error bodies. It never starts a worker or decoder.
Canonical path and SHA-256 bind the exact parsed bytes at launch; editing/deleting/
retargeting or invalidating a review blocks stale launch. Media/stages/storage/
capability checks still run in the coordinator. Startup review does not execute
reconstruction, and queued reviews participate in automatic exit/readback gating.

## Actual checks and discovered failures

GCC 16.2.1 C++26; Clang/format/tidy 23.1.1 explicit C++23/libstdc++.
Linux/Wayland native RTX 4060 Laptop, not H_R/H_V or another native platform.

| Actual command | Exit / result; evidence in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | 0; dev/Clang build logs, final native/layout rebuilds |
| `build/dev/we_viewer_tests '[review]'`; `build/dev/we_core_tests '[review]'` | 0; six synthetic viewer cases / one reader case; focused logs |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | 0; **114/114 each**, final CTest logs |
| `clang-tidy -p build/dev` on seven changed translation units with `--warnings-as-errors='*'` | Initial three optional-access diagnostics in `tidy.log`; explicit guards fixed them; corrected model/tests and final UI/handoff checks exit 0 in `tidy-fixed/final.log`, no first-party diagnostics |
| `clang-format --dry-run --Werror` on changed C++; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | 0; exact argv/results in `checks.json`, scoped logs |
| `python3 $R/run_native.py`; `python3 $R/run_native_final.py` | Initial supervisor exit 1; corrected exit 0. Each GUI invalid-request case intentionally exits **2**. Versioned arguments/binary hashes in `native-commands*.json` |
| `python3 $R/run_compact.py` | 0; targets only the diagnostic PID's own Hyprland window for floating/1024×640 resize; exact argv/readback in compact records |

Cases cover start/resume/reuse, fresh frame/pose/resource choices, legacy omissions,
invalid/oversized/missing/cancelled requests, changed bytes/deletion/in-flight
invalidation and successful owned-thread publication. Fixtures remain explicitly
synthetic; no solver/training or capture-quality gate is inferred.

Native inspection catches a table-width bug despite clean functional tests:
zero inferred stretch width wraps values one character per line. Explicit stretch
weight plus fixed label width repairs it; failing and final readbacks are retained.
The initial native diagnostic also crashes (**SIGSEGV**, -11): its new digest read
was added without first invoking review. GDB confirms a null summary at the report
assignment. The repair initializes/waits for actual review and explicitly checks
its lifetime; the final diagnostic passes. Original logs, GDB trace and incomplete
captures stay local/ignored. No failing run counts as native verification.

## Real native integration and preservation

Final `--review-request` opens the existing **304,867-splat** scene and the unchanged
fresh-job `reuse.json` summary, without executing reconstruction. The inspected
**1428×868** and **1024×640** readbacks show readable actions/limits, scrolling
details and visible Run/Close controls. Both report zero Vulkan errors. The compact
run uses only its own window; no desktop capture or unrelated settings mutation.

The final native handoff uses worker-independent `adopt.json`: actual background
review → pose-boundary Cancel → checksum-pinned reviewed reopen → renderer-visible
**297,489 splats**, zero Vulkan errors. Before/after Cancel pixels remain identical
(SHA-256 `8d35ccd868f09d5525efef9d8d8a643a29e41c474ce98de44ac94dc730becf57`).
The checked request digest is
`da49bd24eeef30f2b623891a04e6009f2f012fe5f1708a4b5bc046d6c4732710`.
Final owned buffers **246,352,000 bytes**, framebuffer **29,491,200 bytes** and
1,685 submitted frames are diagnostic counters, not peak/aggregate VRAM or FPS.
CPU builds/tests/tidy and the desktop compete; no benchmark qualification.

Before/after audits preserve **670 files** covering original/fresh media, RGB,
stages/map/fit/checkpoints/requests/scenes and AGENTS/runbook/specification.
Mutable fresh `capabilities.json` remains excluded. Existing fit/scene identities
retain [fresh-chain scope](2026-10-13-fresh-chain.md). `identity.json` binds current
source, binaries, requests, runners and raw evidence. No solver/optimizer rerun,
installation, implicit download, upload or remote push; generated products ignored.

Remaining: checked metadata/current-storage presentation, manual native input/HiDPI,
broader failure/latency stress, prescribed ≤250k fixture/reference hardware,
independent corpus/quality, other platforms and complete gates. Appearance remains
arbitrary-scale captured radiance; geometry/walking are unavailable.
