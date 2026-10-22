# Synthetic step clearance certificate — 2026-10-22 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The [V4 contract](../protocols/collision_steps.md)
now admits explicit clipped-above-floor synthetic space evidence, static and
same-landing clearance, and a conservative lift/cross/descent certificate.
Every segment checks the full capsule envelope against selected floor tiles
and explicit free/occupied/unknown cells. This does not implement per-tick
step movement: V4 controller creation and shared capsule sessions stay blocked.
Q5/N2, the M3 exit review and A0 remain open; measured walking stays disabled.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `9756f4ec29c8622a253e7241ea1e7482a85e46ce`. The six-file source receipt
in ignored `artifacts/validation/m3-step-certificate-20261022/` (`R`) hashes to
`d156362691bd06c9dca986d4108665f8907b760ac7ce02100343e312e10ea2b1`.
`sha256sum -c R/source-files.sha256` exits 0. All data, logs and screenshots
remain there; no real capture, reconstruction or asset job ran. GNU C++
16.2.1/CMake/Ninja build the C++26 development and explicit C++23 profiles.

The optional `step.clearance_profile` must be exactly
`CLIPPED_ABOVE_SELECTED_FLOOR_V1`. Its document hash binds the new evidence;
legacy V4 documents retain geometry-only blocking. This avoids reinterpreting
their world-aligned labels. Unknown profile names/types reject admission.
Tests check both crossing directions at the admitted binary32 height immediately
below 0.30, just-above-height admission rejection, upper-landing clearance,
direct-riser rejection, unknown/occupied space, diagonal approach corners,
exactly touched cells, unsupported tiles, grid edges, short/airborne endpoints,
stale source revisions and controller rejection from a clear landing.

The initial focused run failed because its route crossed floor tiles that the
existing one-triangle containment rule leaves unsupported. The route now uses
selected supported tiles; a separate assertion retains rejection of the gap.
No containment rule or height threshold changed.

All final commands below exit 0; build and test logs have the corresponding
`*-final.log` names in `R`:

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure -j 4
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure -j 4
cmake --build --preset asan --target we_core_tests -j 4
```

Development CTest passes **477/477**; C++23 passes **452/452**. This focused
command passes **42 cases / 4,498 assertions** on both development and ASan:

```sh
build/dev/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[collision-step],[collision-ramp],[collision-support],[collision-cook],[collision-wall],[jolt-shape],[kinematic-capsule],[capsule-session]' --reporter compact
```

The first sanitizer build linked but failed Catch2 discovery because
LeakSanitizer cannot operate under sandbox ptrace (`R/asan-build.log`). The
final build and focused ASan/UBSan/LSan run used local process access outside
that sandbox and report no sanitizer errors.

Local saved-file/product checks also exit 0:

```sh
build/dev/we_collision_fixture R/clearance-final --step-clearance
build/dev/we_process inspect-collision R/clearance-final/request.json 7c795398e027aaf07047f95b3d1f28d009dae47f272d2f81e4b046294c42af75 R/inspection-final
timeout 45s build/dev/we_viewer --scene R/inspection-final/diagnostic.wexplore --frames 20 --screenshot R/inspection-final.ppm
```

The source manifest hash is
`b467351d0972227c5e3ec8fd38f7f5db99dd6f8cde80ad1514714fbd40f02ca2`;
the opted-in support hash is
`bc230c21a8dcd5047b2ee9ebe3dd54dec872a6671a422cc233ba7392f0d30bf7`.
Inspection reports two clear probes (two cells each) and two unsupported probes.
The native run uses NVIDIA GeForce RTX 4060 Laptop GPU, displays 320 synthetic
markers, and reports zero Vulkan validation errors. Visual inspection of
`R/inspection-final.png` confirms synthetic labeling and `Walk unavailable`.
Native execution required device access; the sandbox attempt returned
`No available video device`. This display check does not exercise step input.

Fresh default, `--walls`, `--ramp` and `--step` fixtures retain all **7/8/11/9**
files byte-for-byte against retained fixtures (`R/legacy-checks.log` and
`R/fixture-checks.log`). Legacy V4 CLI inspection still reports four
`UNSUPPORTED_FLOOR` probes and zero checked cells.

The scoped repository C++ gate covers all six changed C++ files with formatting,
clang-tidy, boundary/docs and whitespace checks. The available local binaries
report LLVM **23.1.3**, despite their `llvm-23.1.1` directory name; exact
23.1.1 qualification remains unavailable. This command exits 0, with output in
`R/cpp-style-final.log`:

```sh
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" python3 tools/check_cpp_style.py include/world_explorer/scene_store/collision_support.hpp src/scene_store/collision_support.cpp src/collision/kinematic_capsule.cpp tests/unit/collision_step_tests.cpp tests/fixtures/collision_support_fixture.hpp tests/fixtures/collision_fixture.cpp
```

Separate `python3 tools/check_docs.py` and `git diff --check` commands exit 0.

Next is a continuous, bounded per-tick controller transition and shared native
replay, including mixed-slope cases. This certificate alone cannot enable V4
sessions or complete the M3 development exit.
