# Maximum-scale synthetic admission — 2026-10-16 Asia/Hong_Kong

Scope/status: local **2M synthetic admission VERIFIED**; **M2/P4 IN_PROGRESS**,
P2/P3 reference performance remains **BLOCKED**. No full Q/P/N/E certification.
Parent `818f119`; implementation is the commit containing this report. Cwd:
repository root. Ignored raw evidence **R**:
`artifacts/validation/m2-scale-fixture-20261016`. Final source/binary/runner/log
identity receipt `R/implementation-identity-final.json` SHA:
`a1cff29299b8de82a9dbc1d903c73a957943ab8f9a71c8a03ff8366529ac9371`.
No production library/viewer/CLI/shader or dependency-environment changes.

## Fixture and observations

The separate [fixture contract](../protocols/viewer.md#maximum-scale-synthetic-fixture)
freezes **2,000,000 positive-opacity degree-3 splats / eight chunks**. Camera
`(0,0,8)`, optical right/down/forward `(+X,-Y,-Z)`, intrinsics
`(1536,1536,959.5,539.5)`, clip `1/32`, **1920×1080**. A million splats at Z=0
are projectable; the other million at Z=16 are behind the camera. Reversing
right/forward exchanges banks without replacing or evicting data. Binary-grid
positions/SH and fixed opacity give two-pixel projected radii: exact full-image
tile crossings **1,248 × 1,251 = 1,561,248**, below the unchanged 8M bound.
CPU checks independently verify frustum eligibility, complete degree/opacity
counts and integer tile coverage. Scale remains arbitrary; no collision/geometry.

Canonical scene SHA:
`589336d9a958d9870936b6707857e0407fd8bf0643e3d822b5c1416080ae64f8`.
Profile SHA:
`3708029894dce4dd3277eac7712c44ddc0d9cae1db4a29e2661de6c94232ac5f`;
**12 assets / 480,004,960 bound bytes**, including the MIT notice. Three complete
GCC/GCC/Clang publications and all asset hashes match byte-for-byte. Bootstrap
stopped at the initial unfrozen digest before qualification; the final generator
requires the fixed identity before publication. The frozen 250k M1 fixture and
all its existing evidence remain unchanged.

Native Linux/Wayland, NVIDIA RTX 4060 Laptop **8,188 MiB**, driver **615.78.08**,
Hyprland **0.56.2** (`native-environment.json`); existing GCC **16.2.1 C++26 Debug**
and Clang **23.1.1 explicit C++23/libstdc++ Debug** profiles. Four final processes
retain exactly **2M resident splats / 1,561,248 tile references** in front, reverse
and return views, with **zero Vulkan errors** and full-size nonblank readbacks.
All twelve nonblank captures agree by view across compilers/storage; return
matches front, reverse differs. Front/reverse hashes:
`e8233a89e4f43552f5ba14bb647bb053e9357190b5afb4d2e2a38fd2279cbc94` /
`490852d7b6732f842dfb45f170862ada7594a1c1b5cb6d095f9dbadbe447d98e`.
Each source coefficient is exactly binary16-representable; this equality is
specific to the synthetic fixture, not a captured-scene Q6 result.

The normal asynchronous update path uploads **512,000,016 FP32 / 320,000,016
packed bytes**, including the initial empty sentinel; device-copy bytes are zero.
Every completed frame respects the existing **15 MiB** copy cap; no splats are
trimmed. Canonical scene and source/binary/shader/runner bytes remain unchanged.

| Final run | Peak sampled tracked allocation (bytes) | Peak sampled rollup RSS (bytes) | Maximum observed status HWM (bytes) | Submitted frames |
|---|---:|---:|---:|---:|
| gcc-fp32-final | 1,090,486,384 | 1,179,451,392 | 1,729,695,744 | 50 |
| gcc-packed-final | 709,618,112 | 989,306,880 | 1,346,961,408 | 38 |
| clang-fp32-final | 1,090,486,384 | 1,181,114,368 | 1,730,768,896 | 50 |
| clang-packed-final | 709,618,112 | 989,270,016 | 1,346,924,544 | 38 |

Explicit tracked-buffer budget: **2 GiB**. Peak staging is **512,000,000 FP32 /
320,000,000 packed bytes**; maximum scratch **323,262,912**, framebuffer buffers
**66,355,200 bytes**. Category maxima may occur at different times. Tracked VMA
buffer allocations include host-visible staging and exclude swapchain images and
driver/context allocations. RSS/rollup are sampled after draws/drains, not every
allocation; status HWM includes earlier load activity but remains approximate.
These observations do not establish a complete device/process memory peak.

A **512 MiB** attempt exits expected **2** on atomic old/new/staging/scratch
budget admission. The previously visible empty revision, baseline pixels and
caller scene remain unchanged (`gcc-budget-512-final/native.json`). Application
budgets and all specification thresholds remain unchanged.

## Checks, observer repair and limits

- `cmake --build --preset dev -j 4`; `cmake --build build/m2-vma-clang -j 4
  --target we_scale_fixture we_gpu_scale_fixture we_core_tests`: **0**;
  `build-dev.log`, `build-clang.log`. After the observer correction, both
  `we_gpu_scale_fixture` targets rebuild successfully (`build-*-memory.log`).
- `ctest --preset dev --output-on-failure -j 4`: **0**, **280/280**, **30.31 s**,
  including **149 worker cases** (`ctest-dev.log`). Clang
  `we_core_tests '[scale_fixture]'`: **0**, **two cases / 51 assertions**.
  CPU fixture source is unchanged by the later diagnostic observer correction.
- `python3 R/run_cpu.py`: **0**, three generation/roundtrip/all-asset checks;
  exact argv, exits, durations and bound bytes in `cpu-commands.json`.
- `python3 R/run_native.py NAME BINARY [--fp16-sh]`: **0** for all four final
  table rows; BINARY is `build/dev/we_gpu_scale_fixture` or
  `build/m2-vma-clang/we_gpu_scale_fixture`. Adding `--budget-mib 512` for
  `gcc-budget-512-final` observes expected child exit **2**, wrapper **0**.
  Runs are sequential after builds/CPU work. Four fixed-view warmup draws precede
  each readback; idle/readback waits are intentional. No FPS/latency distributions
  or P2's prescribed five 120-second adversarial paths are claimed.
- Initial native pilots pass counts/pixels, but independent audit exits **1**:
  the summary's last status HWM is **0.79–1.34 MB below earlier retained samples**.
  A second audit assumption also fails (`audit.log`, `audit-final.log`). The
  [Linux manual](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html)
  identifies status RSS/HWM as inaccurate counters. The diagnostic now reads HWM
  once per sample, retains its actual maximum and records separate `smaps_rollup`
  RSS/maxima. All raw readings remain; no threshold is loosened. The complete
  short native matrix is repeated after this correction in new `*-final` folders.
- `python3 R/audit_checked.py`: **0**, independently checks every asset, launch
  identity, native exit, count, transfer sum, sample maximum/bound and PPM hash.
  `audit-checked.json` SHA:
  `8a636476308b41c216b31ddc3635523fad18946bb3d5116d3c7b6dda9cf88dbc`.
- Nine pre-GPU argument/output/frozen-identity rejects pass (`cli-commands.json`,
  preceding observer version). clang-format/clang-tidy **23.1.1** with unchanged
  warnings-as-errors, boundaries, documentation and whitespace pass; exact static
  argv/log hashes in `static-commands.json` and `final-static-commands.json`.
  Active selected-runtime local checking exits **0** (`runtime-current.log`);
  all selected root hashes remain unchanged, so no receipts are replaced.

All **1,513 protected files** match `preservation-start.json`: saved runs/sources,
instructions/specification and original 250k fixture sources/raw evidence. No
solver, optimizer, two-hour stress, provisioning, upload or push. Full captured
corpus/scale/Q6, native platforms, presentation timing and H_V P2/P4 qualification
remain open. [Progress](../progress.md) identifies the next executable check.
