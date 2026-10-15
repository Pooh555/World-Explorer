# M2 P6 update replay diagnostic — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2 IN_PROGRESS; P6 fails this local diagnostic**. Parent commit
`8f2400c`; implementation was the dirty tree following that commit. Cwd for all
commands: repository root. The diagnostic reads the retained sample_1 bundle,
whose manifest SHA-256 remains
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`.
The loaded scene hash remains
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
The scene contains all **295,952 real splats**; an in-memory synthetic 4,096-splat
marker chunk is created/replaced/evicted in revision order. No saved scene or
reconstruction stage was changed. The marker edits are synthetic and do not
certify a real live reconstruction workload.

`we_gpu_update_stream` runs one producer thread against the same bounded update
sink used by the viewer while the render owner draws at 1280×720. It schedules
100 revisions at 100 ms intervals after eight warmup frames, checks each visible
revision's resident count, collects one completed GPU upload timestamp per
accepted update, and records enqueue and schedule lag samples. Its upload timer
brackets the Vulkan copy and transfer-to-compute barrier. The device-idle counter
counts interactive swapchain refresh calls; the explicit final drain is excluded.
Raw summaries and logs are ignored under
`artifacts/validation/m2-p6-20261016/`. Optimized runs used the existing
`build/m2-no-fault` **GCC 16.2.1 C++23 Release system-library** build, Wayland,
NVIDIA GeForce RTX 4060 Laptop GPU, Vulkan validation and GPU timestamps. Its
final diagnostic executable SHA-256 is
`659aec8b47ed015956bb1e9ddb1062beff698491413e8ac170b569f37dc92a6a`.
The final `summary.json` SHA-256 is
`662291885eb4eb65224880b8ce0bece4e4c1962b5e2d4257f4362b466b62f77d`.
This is a local development GPU, not H_V qualification. No training or
reconstruction job competed with the diagnostic; it used the saved source camera,
all requested splats, no LOD reduction and a concurrent update producer. Tile
overdraw was not separately characterized.

| Sequential run / source state | Updates / elapsed | Enqueue p95 | GPU upload p95 | Draw p95 | Idle / validation errors |
|---|---:|---:|---:|---:|---:|
| `release`, full validation/hash under sink lock | 100 / 23.457 s | 214.889 ms | 23.550 ms | 258.979 ms | 0 / 0 |
| `release-unlocked`, validation/hash outside sink mutex | 100 / 18.966 s | 223.152 ms | 6.179 ms | 47.281 ms | 0 / 0 |
| `release-buffered-hash`, 16 KiB exact SHA updates | 100 / 16.948 s | 196.937 ms | 6.512 ms | 47.727 ms | 0 / 0 |
| **`final-budget-recheck`, concurrent budget change guarded** | **100 / 18.048 s** | **196.268 ms** | **6.494 ms** | **47.379 ms** | **0 / 0** |

The final report contains 100 per-update admission rows and 100 per-upload GPU
samples; p50/p95/p99 enqueue are 189.432/196.268/198.003 ms. GPU upload
p50/p95/p99/max are 6.093/6.494/6.543/6.685 ms. Schedule lag p95 is
7,448.279 ms; visible throughput is 5.541 updates/s. Peak tracked owned GPU
buffer bytes are 246,616,256, below the configured 512 MiB cap. These are
exploratory runs; `final-budget-recheck` repeats the buffered implementation
with the invalid-size admission error and concurrent budget recheck preserved.
They lack the P6 reference hardware,
repeated measurements and full room corpus. The thresholds remain **≤0.5 ms
p95 enqueue, ≤2 ms GPU upload per frame and no interactive GPU-idle waits at
10 updates/s**; no
threshold was changed. Only the idle-wait observation meets its local target.

The first Debug attempt timed out at revision 16 while the sink repeatedly
validated/hashed a queue-full retry under its mutex. A fast capacity rejection
advanced the Debug replay farther, but its recorded `partial` run admitted only
29 updates in 33.585 s, with 1,157.621 ms enqueue p95 and 45.983 ms GPU upload
p95. The optimizer profile was therefore measured separately. The final
implementation preserves duplicate content-hash checks, failed caller ownership,
bounded queue/revision semantics and atomic visible snapshots. Moving heavy
validation/hash outside the mutex lets rendering continue while producers work;
it does not yet make enqueue or full-scene upload budgeted. Buffered SHA-256
updates preserve the unchanged retained scene hash and have a 65,537-byte
small-update/boundary regression against the one-shot digest.

Actual checks and outcomes:

```sh
cmake --preset dev                                      # exit 0, GCC C++26 Debug
cmake --build --preset dev -j 4 > artifacts/validation/m2-p6-20261016/build-dev-final2.log 2>&1 # exit 0
ctest --preset dev --output-on-failure > artifacts/validation/m2-p6-20261016/ctest-dev-final2.log 2>&1 # exit 0, 269/269
cmake --build --preset compat-cxx23 -j 4 > artifacts/validation/m2-p6-20261016/build-cxx23-final2.log 2>&1 # exit 0, GCC C++23 CPU
ctest --preset compat-cxx23 --output-on-failure > artifacts/validation/m2-p6-20261016/ctest-cxx23-final2.log 2>&1 # exit 0, 251/251
cmake --build build/m2-no-fault -j 4 --target we_gpu_update_stream # exit 0
build/m2-no-fault/we_gpu_update_stream artifacts/sample_1/scene.wexplore artifacts/validation/m2-p6-20261016/final-budget-recheck 10 > artifacts/validation/m2-p6-20261016/final-budget-recheck.log 2>&1 # exit 0; local P6 thresholds false
build/dev/we_gpu_smoke artifacts/validation/m2-p6-20261016/smoke-retry.ppm --timings > artifacts/validation/m2-p6-20261016/smoke-retry.log 2>&1 # exit 0, 51 frames, zero validation errors
cmake --preset tsan                                     # exit 0, independent GCC C++26 profile
cmake --build --preset tsan -j 4 --target we_core_tests > artifacts/validation/m2-p6-20261016/build-tsan-final2.log 2>&1 # exit 0
build/tsan/we_core_tests '[render_threads]' --reporter compact > artifacts/validation/m2-p6-20261016/render-tsan-final2.log 2>&1 # exit 0, 3 cases / 5,346 assertions, no reported races
clang-tidy -p build/dev src/render_core/update_sink.cpp tests/unit/render_tests.cpp tests/gpu/update_stream.cpp # exit 0, no first-party diagnostics
```

The first timed smoke exited 2 because its compositor did not observe the
requested resize; the single retry passed its overlap pixel, replace/delete,
resize, post-create recovery and budget rejections. Minimize/restore was again
denied by the compositor. This diagnostic did not rerun the two-hour VMA stress;
the changed renderer path needs another lifetime check only after upload
ownership/scheduling changes. The next work is bounded producer-side preparation
with cheap sink admission and multi-frame upload scheduling while preserving
atomic revision visibility and timeline retirement, followed by a fresh P6 run.
