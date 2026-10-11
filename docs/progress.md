# Current progress

Updated 2026-10-11 (Asia/Hong_Kong). Active milestone: **M1, IN_PROGRESS**.

The native foundation builds and runs on Arch Linux. GCC 16.2.1 and Clang 23.1.1
C++26 builds, contract/worker tests, address/undefined/leak sanitizers, formatting,
include boundaries and targeted static analysis pass. Native GPU tests cover
two-splat compositing, replacement, deletion and resize with zero Vulkan and
synchronization validation errors. No M1–M6 milestone has passed its complete gate.

Implemented and integrated foundation: native SDL3/ImGui application; Vulkan
anisotropic splat projection/radix sort/quad renderer; bounded update sink;
transactional scene/map snapshots and checked scene bundles; native cancellable
worker launch; real FFmpeg/COLMAP/gsplat worker adapters; fly navigation.

The original video failed connected poses, including one explicit recovery.
The user replaced it and authorized moving the replacement to ignored
`data/local/sample.mp4`. It is 20.948333 s, 3840×2160 H.264, 30 FPS; SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
`artifacts/sample2-job` successfully registered **63/63 frames in one COLMAP
model**. Inspect/decode/pose times: 0.159/92.066/265.764 s. The explicit local
gsplat development recipe completed **7,000 steps and 304,867 Gaussians** after
native cancellation at step 4,323 and compatible optimizer/RNG resume. Its
checksummed output is published at `artifacts/sample2.wexplore` and displayed
in the native viewer with zero Vulkan/synchronization errors. Scale is arbitrary;
geometry/walking remain unavailable. Imagery stays local. Earlier failed fits
and the corrected uninterrupted fit are retained separately.

Local environment: Arch Linux, GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4,
SDL 3.4.18, Vulkan headers/loader 1.4.363, OpenSSL 3.6.5, Python 3.14.8.
An escalated `vulkaninfo --summary` detected NVIDIA RTX 4060 Laptop GPU,
Vulkan 1.4.351, driver 615.78.08. This is not reference fixture H_R/H_V.
Sandbox-only GPU probes fail to load the ICD; do not misreport that as missing hardware.

Working inspection command:

```sh
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000
```

Viewer jobs, preferences/theme and scene import/source-view conversion now have
separate files. Loading honors renderer budgets. Source view is an explicit
upright spectator reset, not physical-camera following or calibrated reference
parity. Settings replace atomically on Windows as well as POSIX. Snapshot/device
ownership cleanup was fixed and rechecked through shutdown.

Next executable task: integrate explicit `local_gsplat_v1` recipe selection and
`compile-fit` publication into the shared GUI/CLI job coordinator, using the
existing pose output referenced by `artifacts/sample2-job/poses.json`; do not
restart this successful reconstruction. Then finish the required FlatBuffers
wire/native frame/pose adapter integration. The current staged worker JSONL and
separate `we_process fit`/`compile-fit` commands are transitional, not a waiver.

Outstanding gates include the prescribed ≤250k fixture, reference renderer parity,
FlatBuffers wire protocol, durable map storage, native Windows/Mac runs, prescribed
corpus/performance and geometry/collision work. No Phase 1 milestone is complete.
M7–M8 remain deferred. User edits to `docs/execution_prompt.md` and
`docs/technical_plan.md` remain untouched and excluded from implementation commits.
See [local evidence](validation/2026-10-11-foundation.md) for check scope and commands.
See [capture and current checks](validation/2026-10-11-capture.md) for the real fit,
failure/resume evidence and exact current source/fixture identities. All 20 CPU
checks pass in GCC/Clang C++26, explicit C++23, ASan/UBSan/LSan and separate TSan.
The development resize (1280 px), 304,867-splat scene and this laptop do not
certify original benchmark/platform gates. No milestone was promoted to VERIFIED.
Local commits: foundation `ea289fc`; subsequent capture slice is the commit
containing the newer validation report. No remote push.
