# Frozen portable viewer fixture — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. A project-authored MIT synthetic viewer input
is now frozen at **250,000 SH3 splats**. Five generated copies across local GCC
C++26/C++23 are byte-identical. Native 1280×720 full-count readback and actual GUI
opening pass with zero Vulkan validation errors. Native Windows/macOS fixture
execution is **IMPLEMENTED_UNVERIFIED**; prescribed capture/reference-device,
30 FPS and full Q/P/N/E qualification remain open. Cwd: repository root; parent
**2dea6bf**. Raw ignored/local root **R**:
`artifacts/validation/viewer-fixture-20261015/`.

## Frozen contract and identity

The [fixture contract](../protocols/viewer.md#frozen-portable-viewer-fixture)
provides CPU-only generation, normal bundle roundtrip and a standalone native
Vulkan admission/readback helper. Four 250×250 layers use binary-grid positions,
anisotropic scales, fixed opacity and all SH3 coefficient slots. There is no
randomness, libm-based geometry, private source, solver or fitted correction.
The full count is frozen before GPU testing; no existing scene is trimmed.

Canonical scene SHA:
`10ddfa905d3136ed8343d13393cfdab1c79cc23bfd846d23bc237793492b65ba`.
Profile SHA:
`d13ca4e1970c09f2e8f50bbfb9849f3c2d2bf752fd9db4aad88d51fab5751a7e`.
The **60,006,060-byte** output contains the profile and eight hashed assets:
four chunks, index, provenance, bundle manifest and repository MIT notice. Only
source/recipe/tests/docs are committed. The final profile is atomically renamed
after normal immutable bundle publication and verified loading. Existing output
directories are refused, and failed new outputs/reports remain for inspection.
Scale is arbitrary and collision unavailable; GUI labels remain synthetic.

The calibrated helper uses position `(0,0,12)`, basis `(+X,-Y,-Z)`, intrinsics
`(900,900,639.5,359.5)`, near **1**, far **64**, at **1280×720**. It rejects any
bundle whose complete content hash differs, enforces full residency and zero
validation errors, checks exact RGB8 readback dimensions/bytes, and requires
>1% foreground at >8 levels from clear color in any channel. Observed foreground
is **357,717 pixels**; readback SHA
`9977bcbf66dd71a11804709648123fb596ff189fe592626276b3eb7e43b3ae45`.
It records no FPS pass. The GUI uses its independently fitted spectator camera.

## Actual checks and failures

`R/run_checks.py`, SHA
`cd9043c5c867bfeb22263f86eb243ae4257ec4a23eb29b58a65d9ac473508d71`,
records exact argv/cwd/exit/elapsed time, source/executable hashes and logs as
`LABEL-command.json` / `LABEL.log`. Native receipts include isolated home/cache
and display variables. `R/fixture-identity.json` binds implementation/binaries,
all nine generated files and native observations. Local GCC **16.2.1**, explicit
C++26 development / C++23 CPU modes; native Linux RTX 4060 Laptop, driver
**615.78.08**, Wayland. This is not H_R/H_V/H_M.

| Commands / receipt labels | Actual result |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build --preset compat-cxx23 -j 4`, checked/final build receipts | **0**, generator/probe/core and existing viewer/CLI build; C++23 CPU has no GPU dependencies |
| `build/dev/we_core_tests 'portable synthetic viewer*'`, `focused` | **2 cases / 54 assertions**: frozen full-payload hash, independent endpoints/conventions, count/chunk limits, no geometry/observations and fixed calibrated camera |
| `ctest --preset dev`; `ctest --preset compat-cxx23`, `ctest-*` | **267/267 / 249/249**, **133.77 / 131.86 s**, including **133 worker cases** each; raw `*-LastTest.log` retained |
| `build/{dev,compat-cxx23}/we_viewer_fixture R/NEW`, `generate-*`; `python3 R/verify_fixture.py`, `verify-fixture-checked` | All **0**, five complete outputs byte-identical, every asset checksum/size verified |
| `python3 R/probe_cli.py`, `cli-probes` | **0**, five invalid requests exit **2**, caller sentinel unchanged; wrong synthetic bundle fails before SDL/GPU initialization and retains `native.json` |
| `build/dev/we_gpu_portable_fixture R/dev-checked/scene.wexplore R/native-checked`, `load-checked` | **0**, 250k resident splats, two submitted frames, zero validation errors, matching initial/final readbacks |
| `build/dev/we_viewer --scene R/dev-checked/scene.wexplore --max-splats 250000 --frames 120 --screenshot R/viewer.ppm --settings R/viewer-settings.json`, `viewer-checked` | **0**, **134** actual frames / full 250k / zero Vulkan errors; actual GUI readback **1428×1746**, synthetic label/arbitrary scale/walk unavailable visually inspected |
| Format/tidy **23.1.1**, dependency-manifest/docs/boundaries/diff checks | All final **0**, no first-party tidy diagnostics; strict dependency closure not newly qualified |
| `python3 R/run_checks.py after` | **0**, protected/prior evidence/production size and SHA-256 inventories unchanged |

Initial tidy identified a possible exception escaping diagnostic `main`; an outer
handler fixes it without suppressions. Initial inventory verification expected
seven assets, omitting the separately hashed provenance file; correcting the
observer to eight assets/nine total files preserves complete byte/checksum checks.
Both initial logs and the earlier observer source remain. No renderer/shader,
worker, dependency or fitted asset change was needed. PPM previews were converted
to PNG for inspection without changing pixels; original readbacks remain.

CPU suites, generation and native work overlapped. Elapsed times and owned GPU
allocation **149,388,448 bytes** are not presented FPS or peak-device-memory
qualification. No reference comparison, Clang build, sanitizer or coverage rerun
is claimed here. Before/after checks preserve **2,088 protected / 1,936 prior
evidence / 94 src/include files**, both completed scenes and all failed stages.
No decode, solve, fit, installation, download, push or upload occurred. M1 still
requires native/reference-machine execution and the prescribed real-room capture;
[progress](../progress.md) owns the next executable step.
