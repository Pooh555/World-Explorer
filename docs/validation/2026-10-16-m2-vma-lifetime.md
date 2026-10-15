# VMA lifetime completion audit — 2026-10-16 Asia/Hong_Kong

Scope/status: VMA update/resize lifetime diagnostic **VERIFIED on the local
Linux NVIDIA development profile**; **M2 IN_PROGRESS**. Implementation/parent
`d26485e`. The run started on 2026-10-15 and completed before this continuation;
this report records the collected exit and independent artifact audit. Cwd:
repository root. Native Linux Wayland, NVIDIA GeForce RTX 4060 Laptop GPU,
driver 615.78.08, GCC 16.2.1 C++26 Debug, Vulkan synchronization validation.
[Implementation evidence](2026-10-15-m2-vma.md) retains the dependency pins,
GCC/Clang C++23 builds, 267 CPU/worker checks, pilot, rejection/recovery smoke
and full-scene 4K/fullscreen checks.

The actual command was:

```sh
build/dev/we_gpu_lifetime_stress artifacts/sample_1/scene.wexplore artifacts/validation/m2-vma-20261015/full 7200 > artifacts/validation/m2-vma-20261015/full.log 2>&1
```

The retained execution session returned **exit 0**. The independently parsed
`summary.json` and flushed `events.jsonl` agree:

| Observation | Actual result |
|---|---|
| Requested / elapsed duration | 7,200 / **7,201.865421505 seconds** |
| Scene | Full retained sample_1, **295,952 splats**, fixed source camera |
| Visible updates / final revision | **480 / 481**; 479 periodic records plus awaited final full-scene revision |
| Requested resizes | **359**, cycling 1024×640, 1600×900, 1920×1080 and 1280×720 |
| Full-scene readbacks | **13** at 1280×720: baseline, 11 periodic, final; all bytes hash identically |
| Submitted frames / minute samples | **298,300 / 119** |
| Vulkan validation errors | **0** in every sample and at completion |
| Peak tracked owned buffer allocations | **369,408,864 bytes**, below 536,870,912-byte cap |
| Final sampled owned bytes | **293,645,136** at the repeated empty-revision phase |
| Process peak / final RSS | **499,757,056 / 295,350,272 bytes** |

The audit rehashed every PPM with Python `hashlib.sha256`, checked capture/event
counts, required elapsed duration, completion event, all sampled validation
counts and the tracked allocation cap. All assertions passed. All 13 readbacks
have SHA-256
`107ceeb0c2cb975041af83db2283e0d6f3673c06d8503f1109c9cd3955036613`,
also matching the preceding direct-allocation diagnostic.

The running binary and libraries were rehashed after completion and still match
the [implementation report](2026-10-15-m2-vma.md): stress executable
`d406fb522e64c3ee7ff26fa9ec618da0f4c602ab4bd2721d328b148e8fce3943`,
renderer archive `4ffe36d8510d407d3a05ae80610f37538ba528cb5aebbf155ab7604fa767c000`,
VMA archive `1f8da99650f50e9070fc87a81aa0d2208cfb41abee614025cb80b300c439ab8f`.
The input manifest also remains
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`;
scene content is
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
No reconstruction, scene publication or frozen-fixture change occurred.

The diagnostic alternates full and synthetic empty revisions every 15 seconds;
it does not exercise the P6 ten-updates/second workload. Process HWM and tracked
buffer bytes do not measure total device VRAM. Other devices/platforms, accepted
minimize/restore, physical device loss, Q6 packing, reference Q/P/N/E and
release-vcpkg remain unqualified. The subsequent
[runtime refresh](2026-10-16-vma-runtime-refresh.md) reobserves the two rebuilt
development executables and restores the full local dependency consistency check.
Raw captures, logs and summary remain ignored at the command's output location.
