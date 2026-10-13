# Checked Import metadata/storage — 2026-10-13 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Read-only retained metadata and current
storage review; no milestone or Q/P/N/E certification. Cwd: repository root.
Parent `6250f25`; implementation is the local commit containing this report.
Raw ignored evidence: `R = artifacts/validation/import-metadata-20261013`.
`identity.json` binds changed source, binaries, requests and verification runners.

Import now calls shared preflight for an explicitly selected completed inspection:
bounded inventory/schema/hash checks and source-byte/SHA-256 identity, then codec,
dimensions, duration, original time base/rotation and captured color/scale display.
Automatic inspection leaves metadata pending; review does not search for a cached
stage or run a worker. Shared storage reporting uses the nearest existing directory
for each map/fit/scene and fresh inspection/decode/pose destination. Unknown or
insufficient space and file ancestors reject review. Available bytes are snapshots,
not reservations or whole-job peak estimates; coordinator checks still run at
launch. Request path/digest is rechecked after review before publishing eligibility.
The original 32-row/128-KiB presentation limits remain unchanged.

## Actual checks

GCC 16.2.1 C++26 and Clang 23.1.1 explicit C++23/libstdc++; native Linux/Wayland
RTX 4060 Laptop, not H_R/H_V/H_M. Commands below exited 0 unless stated.

| Commands (cwd repository root) | Observed result / logs in R |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | Both builds pass; `configure-dev.log`, `build-*-final.log` |
| `python3 R/run_checks.py focused` (R expanded to the path above) | Viewer `[review]`: **8 cases / 187 assertions**; core `[inspection],[preflight]`: **11 cases / 650 assertions**; focused logs/argv |
| `python3 R/run_checks.py dev`; `python3 R/run_checks.py clang` | Actual `ctest --preset dev` and Clang CTest: **120/120 each**; `*-commands.json`, `*-0.log` |
| `python3 R/run_checks.py tidy` | Five changed translation units, clang-tidy 23.1.1 `--warnings-as-errors=*`: final pass |
| `python3 R/run_checks.py static` | Clang-format 23.1.1, boundaries, documentation and diff checks pass; exact argv/results retained |
| `.venv-train/bin/python R/run_native.py` | Three native checks pass; exact viewer/handoff argv in `native-commands.json` |

Synthetic tests cover corrupt/missing metadata, same-size changed source, invalid
storage ancestors, inadequate space on the actual `/dev/shm` volume, cancellation,
legacy omissions, automatic metadata pending, separate job destinations and the
fully populated retained/resumed 32-row request. Source mutation after review is
rejected by launch preflight with prior scene bytes preserved. No reconstruction
quality is inferred from these fixtures.

Initial tidy rejects four optional accesses in a test: Catch2 `REQUIRE` does not
establish guards for the analyzer. Explicit guards repair them; initial/final logs
remain. Initial native runner exits 1 before launching a viewer because system
Python lacks Pillow. The existing training interpreter has Pillow 11.3.0; switching
the runner interpreter resolves it without installing packages or starting training.

## Real integration and preservation

Inspected **1428×868** and **1024×640** readbacks show the unchanged full
**295,952-splat** sample_1 scene, checked H.264 **3840×2160 / 20.933 s** metadata,
time base **1/30**, rotation **0°**, source **63,214,749 bytes** and available space
for all three output destinations. Both report **zero Vulkan errors**. Compact
resizing targets only this diagnostic process's own compositor window.

Native reviewed Cancel/reopen uses `R/adopt.json`, explicitly reusing completed
poses/fit with new map/scene destinations under R. Normal owned-thread admission
restores all 295,952 splats, zero Vulkan errors, with identical scene content.
Before/after Cancel pixels share SHA-256
`107ceeb0c2cb975041af83db2283e0d6f3673c06d8503f1109c9cd3955036613`.
Reviewed request digest:
`7a13d2a71c10d2f4955054cbe928506b6251ec367ee701057ea5a97ff79b1ea0`.
Final owned buffers **244,611,392 bytes**, framebuffer **29,491,200 bytes** and
1,472 submitted frames are diagnostic counters, not aggregate peak VRAM or FPS.

Before/after SHA-256 audits preserve **2,393 files**: all three videos/run folders,
including completed fit/checkpoints/scenes, failed poses, requests, logs, plus
AGENTS/runbook/specification. No solver/optimizer rerun, duplicate reconstruction,
download, upload or push. Source identity remains
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.

Remaining: manual native input/HiDPI, broader failure/latency stress, complete
recipe/metric identity locks, prescribed ≤250k fixture/reference hardware,
independent quality corpus and other native platforms. Metadata checks do not
certify decoding, frame selection, measured gravity, geometry or walking.
