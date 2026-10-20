# Explicit world-frame alignment — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. [The contract](../protocols/world_frame.md) adds
source-bound distance/up declarations, scene-level Sim(3), atomic epoch changes,
schema-5 persistence and the shared `align-world` command. Synthetic declarations
retain arbitrary scale. Independent declaration admission is not physical proof,
measured geometry or permission to walk. No Q/P/N/E, asset or platform gate is
promoted. The user answered **No** when asked for independent sample_1 distance
and up references. No real metric alignment was produced.

Repository root is the default cwd. Evidence root (`R`) is
`artifacts/validation/m3-world-frame-20261020/`; parent commit is
`e61217b0367dabac50f10d0b26da958dba821896`.
`final-source-identity.json` pins final changed files, binaries and scripts;
`frozen-checks.json`, `finish-profiles.json`, `native-frozen.json` and
`sanitizers.json` pin the final executed checks. Actual command binary hashes
are retained separately from earlier development runs.

## Inputs and local behavior

The synthetic GPU fixture combines an asymmetric 45-splat scene and a native
baked triangle. `R/gpu/world-frame-measurement.json` pins source content and mesh
anchors; declared scale is 2, rotation maps source +Z to +Y and translation maps
the selected origin to zero. Declaration SHA
`999aee080f57fb8a8fdce0e0603003282c0e9bfb2a45d7fe87f517a3204b8950`;
aligned manifest SHA
`22758327bb4d6034dd0369c2ad880a0b1209b8588fb2167427717679af0ae6d4`.
`final-products.json` pins every source/aligned product file. CPU fixtures additionally exercise independently
specified distance/up declarations against synthetic observed-source metadata;
these are contract tests, not physical measurement evidence.

The retained full real mesh is
`artifacts/validation/m3-mesh-job-20261020/full-job/stages/bake-1/output`, manifest
SHA `0b873804a3d5aa2a02c95245a43c550ed4d2ba2240d3af1e4325e23c198c6ee2`.
All **39,373 vertices / 49,213 triangles** remain in the real regression.
Its repeated GLB export is SHA
`2e3a55b213237d48e73975513ff30eb2f114dfbfd61675fbf636cda3b97f5e24`,
identical to the previous output. This remains visibly fragmented captured color
with unknown gaps, arbitrary scale and disabled walking. Screenshots were
inspected locally; no reconstruction, MVS, fusion or completed bake was rerun.

## Commands and checks

`profiles.py`, `frozen_checks.py`, `finish_profiles.py`, `native_frozen.py`,
`native_retry.py`, `local.py`, `sanitizers.py` and `frozen_style.py` in R own exact dated commands, environments, timings and logs.
Successful checks exit 0; expected refusals are recorded separately.

| Check | Observed scope |
|---|---|
| Development, explicit C++23 compatibility and ASan builds | CMake/Ninja, unchanged standard/profile selection; final receipts retain actual binaries |
| `ctest --preset dev`; `ctest --preset compat-cxx23` | **419/419**, **395/395**, **151.9 / 95.5 s**; full CPU/worker suites |
| `we_core_tests '[world-frame],[gltf],[sequence_texture],[mesh_texture],[mesh]'` | **42 cases / 94,640 assertions** on all three profiles; **10 new alignment cases / 234 assertions**; admission, parsing/limits, near-opposite up, transforms, camera/bookmark, replay/resync, persistence and retained appearance regressions |
| Native `we_gpu_mesh R/gpu REAL_BUNDLE` | RTX 4060 Laptop GPU, **79 frames**, zero Vulkan errors; native baked color and anisotropic/local-SH splat Sim(3) parity at maximum **one RGB8 level**; full real mesh retained |
| Native viewer synthetic alignment / real bundle | Ordinary bundle opening, screenshots inspected, walking disabled; final **90 / 947 frames**, zero Vulkan errors |
| `local.py`: shared CLI admission/reload | Synthetic CLI output matches every GPU-produced bundle file; aligned GLB export refused; correctly source-pinned synthetic declaration refused on real data with no output directory |
| Native sanitizer execution | **42 cases / 94,640 assertions**, no reported sanitizer errors; synthetic CLI publication/reload exactly reproduces every development/GPU bundle file; leak detection enabled |
| Pinned C++ style, documentation, whitespace checks | **20 C++ files**, clang-format/tidy **23.1.1**, full pinned Black check, documentation and whitespace checks pass |
| Protected input check | **24,457 files**, zero new differences; pre-existing **3,982** external-header changes retained; four original bakes and prior audits, **152 sequence inputs** and original raw/baked source files preserved |

World-frame changes preserve original chunk/index/provenance bytes and texture
ownership. Scene and renderer fingerprints include declaration identity. Cached
resident splats cannot reuse an old world frame. Aligned snapshots reject chunk
corrections pending newer full-snapshot resync, so old proof/bookmarks cannot
silently follow corrected geometry. No collision producer or walking permission
exists in this slice.

## Failures and limits

Initial CPU alignment run passed 8 cases / 178 assertions; final coverage adds
proof-cache/pose/payload and independent-profile persistence cases.

Initial integration compilation exposed a block inserted into the unrelated PLY
loader, a test map/scene revision-type mismatch and missing test read tokens;
corrected with failures retained. Two development builds briefly overlapped;
both stopped on the same test compilation error. Final builds run independently.
Tidy required an explicit optional check, constant names and a structured command
request; warnings were not weakened. One interim style run reached documentation
checking before this report existed; its final run includes the completed report.

The initial real viewer invocation requested `--source-view` and exited 2 because
this processed bundle has no recorded source camera. Ordinary opening succeeds;
no source metadata was invented. The system Python lacks Pillow and direct PPM
inspection is unsupported, so the retained isolated fusion environment converted
captures to PNG for inspection without installation or changes to that environment.

Independent sample_1 measurements and measured surface ground truth remain
unavailable. Real alignment, supported/unknown/free collision semantics, Jolt
capsule movement, Q5/N2, crash/power-loss qualification and a metric GLB profile
remain open. Existing dependency/runtime/rights gaps stay separate. A0–A6/AP
remain NOT_STARTED; M4 has not begun.
