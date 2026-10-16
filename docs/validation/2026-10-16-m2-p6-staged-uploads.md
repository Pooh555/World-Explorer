# M2 prepared admission and staged uploads — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/P6 IN_PROGRESS**. On the local Linux Wayland/NVIDIA RTX 4060
Laptop development profile, two 100-revision, 1280×720 replays meet the unchanged
P6 enqueue, per-frame upload and no-idle targets. The source is the retained
295,952-splat sample_1 scene (manifest SHA-256
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`,
scene content SHA-256
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`).
Its in-memory 4,096-splat marker replacement/eviction is **synthetic**. Neither
saved scene nor reconstruction stages changed. Parent commit `18844fc`; source
was a dirty implementation tree. Raw reports/logs are ignored under
`artifacts/validation/`. Cwd for every command below: repository root.

`renderer_update_sink::prepare` validates and fingerprints the immutable
snapshot before measured enqueue. A bounded weak-owner cache reuses unchanged
chunk digests; metadata/chunk content still determine duplicate-operation
identity. Failed admission preserves ownership, and budget changes require
fresh preparation. The legacy enqueue path remains. The Vulkan renderer keeps
the old scene visible while copying at most 15 MiB per frame, then submits the
last copy/barrier and publishes the new revision atomically. VMA staging and
replacement allocations are charged until timeline retirement.

| Sequential native Release C++23 replay | Enqueue p95 | Preparation p50/p95 | GPU upload p95/max per frame | Visible rate | Result |
|---|---:|---:|---:|---:|---|
| Prepared, uncached full hash | 0.007 ms | 258.624/338.418 ms | 7.118/7.489 ms | 3.711/s | Fails; work moved outside enqueue only |
| Buffered canonical hash | 0.005 ms | 220.528/237.258 ms | 7.142/8.301 ms | 4.509/s | Fails |
| Cached chunks, whole-scene copy | 0.013 ms | 12.437/23.110 ms | 7.251/7.524 ms | 10.038/s | GPU fails |
| 16 MiB staging | 0.011 ms | 13.001/15.958 ms | 1.906/2.241 ms | 10.008/s | Per-frame maximum fails |
| 14 MiB staging, two runs | 0.028/0.015 ms | 14.256/16.640 and 7.997/8.888 ms | 1.584/1.990 and 1.478/1.742 ms | 9.976/9.994/s | Visible rate slightly below ten |
| **15 MiB staging, two runs** | **0.015/0.010 ms** | **8.079/10.232 and 8.660/10.358 ms** | **1.432/1.795 and 1.420/1.890 ms** | **10.011/10.008/s** | **Local diagnostic passes twice** |

Each final run admitted and displayed all 100 revisions on the full scene at a
scheduled 10 Hz with 500 completed upload-frame timestamps, zero interactive
device-idle waits and zero Vulkan validation errors. Peak tracked owned buffer
bytes were 400,240,832, under the unchanged 536,870,912-byte cap. The final
summary SHA-256 values are
`2f8d0f88ff457a0ff917452b33eb9538549d61f887e62293a269fef63ebc2aeb`
and `4e7379c0e70d59f35e969f7de4c0233689df7c334a5f8efce16e1559c9e30b0b`.
The measured Release replay executable was
`a26046209dddadf1e89e4849dd44505925c4edacde60824c7b8d8277ea51d90e`.
The later rebuild changes only the canonical hash result for invalid map-key
scenes; the validated replay scene has the same retained content hash. A final
run of the rebuilt binary is pending after the lifetime stress.

Actual checks: `cmake --build --preset dev -j 4` and
`cmake --build --preset compat-cxx23 -j 4` exit 0; final
`ctest --preset dev --output-on-failure` passes **270/270** and
`ctest --preset compat-cxx23 --output-on-failure` passes **252/252**.
`build/tsan/we_core_tests '[render_threads]' --reporter compact` passes three
cases/5,346 assertions without reported races. `clang-tidy -p build/dev` on
the changed scene, sink, renderer and diagnostic/test sources exits 0 with no
first-party diagnostics. `cmake --build build/m2-vma-clang -j 4 --target
we_gpu_smoke` exits 0 on the independent Clang 23.1.1 C++23 native profile;
this binary was not run. `cmake --build --preset coverage -j 4 --target
we_coverage_check` exits 0 on isolated Clang C++26: non-GPU core
**90.09% line/81.79% branch**, package/update **93.89% line/90.70% branch**;
the unchanged strict local coverage goals pass. The final native Debug
`build/dev/we_gpu_smoke artifacts/validation/m2-p6-staged15-smoke-20261016.ppm --timings`
exits 0: 51 frames, exact overlap pixel `130,3,68`, replace/delete/resize,
post-create recovery and budget rejection, zero Vulkan errors; the Wayland
compositor denied minimize. Full-scene
`build/dev/we_gpu_viewer_regression artifacts/sample_1/scene.wexplore artifacts/validation/m2-p6-staged15-viewer-20261016.json`
exits 0: windowed/4K/fullscreen/restored all pass at 295,952 splats, maximum
510,820,928 tracked bytes and zero Vulkan errors. Short local frame timings do
not certify P2/P3.

The native 60-second `we_gpu_lifetime_stress` pilot exits 0: 60.538 seconds,
four visible updates, two resizes, two byte-identical readbacks (SHA-256
`107ceeb0c2cb975041af83db2283e0d6f3673c06d8503f1109c9cd3955036613`),
2,783 submitted frames, 377,467,280 peak tracked bytes and zero validation
errors. A fresh 7,200-second stress of this changed ownership path is running
under `artifacts/validation/m2-p6-staged15-full-20261016/`; no result is claimed
until its exit and files are audited.

Reference H_V, other scenes/overdraw and platforms, accepted minimize, physical
device loss, Q6 packing, release-vcpkg and full Q/P/N/E remain unqualified. The
prior two-hour VMA result applies to the earlier single-frame upload path.
