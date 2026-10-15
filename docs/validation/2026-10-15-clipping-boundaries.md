# Synthetic clipping boundaries — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. A separate six-splat diagnostic verifies
near/far equality and immediate float32 neighbors on native Vulkan and pinned
gsplat. **78.833068 dB / 0.999988377 SSIM / maximum one RGB8 level**; all twelve
independent presence checks pass. This is synthetic renderer correctness evidence,
not a real reconstruction or Q/P/N/E gate. Cwd: repository root; parent
**0bcca3b**. Raw ignored/local root **R**:
`artifacts/validation/clipping-boundaries-20261015/`.

## Predicate, fixture and implementation

Both existing implementations reject mean depth with `z < near || z > far`;
equality is included. Inspection used the installed packed CUDA projection,
`gsplat/cuda/csrc/ProjectionEWA3DGSPacked.cu`, SHA
`73ebbcc4be4bee817a9dede00aa9937415f241501559976934a02372fe6e57b2`,
and unchanged native projection shader SHA
`de5f224795dc8788763901e55391c5ab5daa53874b9173aefc284c71f489ab96`.
No production renderer, shader or installed dependency repair was needed.

The [new diagnostic contract](../protocols/viewer.md#synthetic-clipping-boundaries)
adds matching `--clipping` modes, preserving all existing modes. The fixed camera
is at the origin, 1280×720, intrinsics `(900,840,639.5,359.5)`, near **1**, far **8**.
Six finite SH3 splats use identity transforms, opacity 0.8 and isolated footprints.
Power-of-two spatial/depth ratios keep the expected image regions independent
of the one-ULP depth changes. This checks mean culling, not truncated volumes.

| Slot / plane relation | Float32 depth bits | Expected image contribution |
|---|---|---|
| 0, before near | `3f7fffff` | Background |
| 1, on near | `3f800000` | Visible |
| 2, after near | `3f800001` | Visible |
| 3, before far | `40ffffff` | Visible |
| 4, on far | `41000000` | Visible |
| 5, after far | `41000001` | Background |

Comparator admission pins the camera/profile, exact float32 payload and six
ordered contributions, rejecting changed planes, transforms, payloads, nonfinite
or boolean metadata and missing/reordered splats. It checks 16×16 regions around
canonical centers `(189.5,639.5,1089.5)` at rows `254.5/464.5` in **both** readbacks.
Outside regions allow ≤1 RGB8 level from background; on/inside regions require
>32 levels. Observed deltas are **0** outside and **160–165** on/inside. These
structural assertions augment unchanged **≥45 dB / ≤8-level** comparison limits.
CPU negative controls prove that all-background output, missing visible regions
and leaking excluded pixels fail. No exclusion, fitted correction or mask is used.

## Identity and actual checks

Fixture SHA **`c90a098f797f1f02b0078492222609748f5d672e1697b4433002fa30ec1e2d6b`**;
comparison SHA **`c2699ae01ffede52a640e18d84178db9c669cf408e16c9c597b3e4e8daca5fdb`**.
`R/implementation-identity.json` pins all changed diagnostic/test files, native
and core binaries, SPIR-V, shader and inspected CUDA source. The native diagnostic
binary SHA is `30bc1d89e20640d2939fea347c419c4fc2b5bd3a83264314302b8d38d9ec9293`.

`R/run_checks.py` SHA
`e2d84a5dc45dd517af21cc55af7a472c9334b0588b2feb417554b2567c5f7e22`
records exact argv/cwd/exit/elapsed time, executable and selected source hashes
in `LABEL-command.json` / `LABEL.log`; native receipts also record isolated
home/cache and display variables. `R/run_matrix.py` SHA
`27d3e3a89e42cbb606f8b4f729954a6ffbdd39766f33400880f2d715164edbe2`
defines all seven fresh native/reference cases below; no worker/optimizer runs.

| Commands / receipt label | Actual result |
|---|---|
| `cmake --build --preset dev -j 4`, `build-dev-final` | **0**, existing GCC 16.2.1 C++26 viewer/CLI/diagnostic/core profile |
| `cmake --build --preset compat-cxx23 -j 4`, `build-compat-final` | **0**, explicit C++23 CPU profile |
| `build/dev/we_core_tests 'synthetic clipping*'`, `focused-checked` | **1 case / 27 assertions**, exact independent bit patterns, scene validity, projection regions and extent rejection |
| `.venv-train/bin/python tests/gpu/reference_input_tests.py`, `input-tests-checked` | **26/26** CPU cases, including **eight new** fixture/presence cases |
| `ctest --preset dev`; `ctest --preset compat-cxx23` | **263/263 / 245/245**, **71.07 / 70.06 s** CTest totals; detailed logs include **133 worker cases** each, Python 3.14.8 |
| `python3 R/run_matrix.py`, `native/compare-clipping-checked` | Both **0**, one view / six resident splats / two submitted frames / zero Vulkan errors; **2.47 / 2.87 s**, numeric/region results above |
| Same matrix, default/compositing/termination/batches/bounded | All **0**, **32** existing synthetic views, maximum one level, zero Vulkan errors |
| Same matrix, `real-default` | Both **0**, three full **648,332-splat** retained sample_3 views, maximum one level, zero Vulkan errors |
| Format/tidy 23.1.1, docs/boundaries/diff | All **0**, `format-checked`, `tidy-checked`, `docs`, `boundaries`, `diff` logs |
| `python3 R/run_checks.py after` | **0**, protected/preceding/production SHA-256 and sizes unchanged; `preservation-after.json` |

Native Linux **RTX 4060 Laptop / driver 615.78.08**; Python 3.10.22,
gsplat 1.5.3+pt24cu124, torch 2.4.1+cu124, CUDA 12.4. This is not H_R/H_V/H_M or
another OS. CPU suites and native work overlapped; durations and sampled owned
allocation **29,682,656 bytes** are not performance or whole-device peak-memory
qualification. Clang C++23 GPU, sanitizers and coverage were not rerun here.
Initial tidy found lowercase local constexpr `bits`; renaming it `BITS` fixes the
style diagnostic without suppressing warnings. Initial and final logs remain.

`R/regression-summary.json` retains every case/hash and verifies default synthetic
and real fixtures/readbacks byte-identical to preceding evidence. The native
fixture and comparator remain separate from production code; real holdout scores
and previous all-camera reports retain their original scope. Before/after checks
preserve **2,088 protected files / 1,206 preceding evidence files / 94 src/include
files**, both completed scenes and every failed stage. No decode, solve, fit,
checkpoint/model load, installation, download, upload or push occurred. M1's
prescribed ≤250k fixture, rights and reference/native platform qualification
remain open. [Progress](../progress.md) owns next executable work.
