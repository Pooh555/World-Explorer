# Shared synthetic capsule session — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Application-owned pinned admission, CLI replay and
native synthetic capsule controls now share one camera/input/lifetime command.
Focus/dialog input pauses the clock; stop/cancellation is terminal; corrected
scene/epoch/pose data permanently invalidates the session while preserving the
last accepted pose. Public application types contain no Jolt. The 45°/0.30
settings still lack non-flat support/fixtures. Measured walking and Q5/N2 stay
open. See the [session contract](../protocols/capsule_session.md).

Repository root is the default cwd; parent is `991cff9129caaa385d38a724cbf1a08ccd141383`. The 11-file source
receipt `R/source-files.sha256` hashes to
`7b9e6c4a08893a2119b25e5afc7e05c300750590b3c05a41e23812bfe903688c`. Ignored logs, requests, traces, readbacks, scripts and receipts are in
`artifacts/validation/m3-capsule-session-20261021/` (`R`). No real solve, fusion,
bake or refit was rerun; all 5,184 retained real cells remain unknown.

GNU 16.2.1/C++26 `cmake --build --preset dev -j2` and
`ctest --preset dev --output-on-failure -j2` exit 0: **467/467** checks. The
focused development command `build/dev/we_core_tests
'[capsule-session],[kinematic-capsule],[collision-support],[collision-wall],[collision-cook],[jolt-shape]'
--reporter compact` passes **32 cases / 2,352 assertions**. It covers pinned
native reload, bad schemas/hashes/fields/dimensions, horizontal camera/look,
focus/dialog clock pauses, stop/cancel, permanent epoch/scene/pose invalidation,
failed replacement ownership and unknown CLI replay. The reverse-motion assertion
was corrected to use the existing float precision allowance. Naming/automatic-move
issues and a Catch false-test flag analyzer path were corrected without warning
suppression. A first tidy process crashed while its viewer input was being
edited; the stable-source rerun is recorded separately.

GNU 16.2.1/C++23 `cmake --build --preset compat-cxx23 -j1` and
`ctest --preset compat-cxx23 --output-on-failure -j2` exit 0: **442/442**.
Native `cmake --build --preset asan --target we_core_tests we_process -j1`
and the focused command using `ASAN_OPTIONS=detect_leaks=1
build/asan/we_core_tests` exit 0: **32 cases / 2,352 assertions**, with no
reported ASan/UBSan/LSan errors. `python3 R/cli_sanitizer.py` exits 0: all
three sanitizer CLI reports match development bytes exactly. The nine-file
clang-format/clang-tidy 23.1.1 gate, documentation links and whitespace pass.
The boundary policy explicitly permits the application's private collision
interface dependency; Jolt headers remain collision-only. The pinned Black
check on every tracked Python file exits 0.

`build/dev/we_collision_fixture R/fixture --capsule` emits three pinned session
requests. `build/dev/we_process capsule-session REQUEST SHA256 1 0 10000000 100`
exits 0 for wall, unknown and unsupported routes. Each reaches tick 120; their
final feet are `(2.745001316,0,1.699999571)`, `(1.5,0,3.725000858)` and
`(3.708335638,0,3.5)` respectively, with clear diagnostic / unknown / unsupported
clearance. `python3 R/audit.py` exits 0: all 300 replay samples exactly match the prior
controller traces, all 15 prior fixture files and all three sanitizer reports
are unchanged. Re-running `inspect-collision` after shared-loader extraction reproduces all
five previous V2 inspection output files byte-for-byte.

Native input checks use local compositor-injected events, not human latency
qualification, through `python3 R/run_native_verified.py` (exit 0, **13 assertions**). Each viewer starts with
`--capsule-request R/fixture/session-NAME.json --capsule-request-sha SHA256
--settings R/verified-NAME-settings.json --input-trace R/verified-NAME.jsonl`. The script records
exact arguments, targeted input, state assertions and readbacks, restores focus/
cursor and closes only its own windows. The wall/unknown/correction viewers exit 0 after **318 / 55 / 489 frames**,
each with 4 mesh triangles / 8 vertices and zero Vulkan errors on the RTX 4060
Laptop GPU. Native W verifies wall sliding and unknown blocking; settings/focus
pauses hold pose/ticks; Stop preserves capsule state while Fly moves. A loaded
revision-15 scene without the old alignment proof permanently invalidates the
revision-14 session. Visual inspection confirms synthetic labels, active/stopped/
invalidated controls and unavailable measured walking. `R/receipt.json` binds
requests, source/support, replacement and four readbacks. Wall/unknown/unsupported
request SHAs are `5e99a4e15ce40b1b1a595c705dffcc32ce168c3628c2281f446c8ca8165b0a76`,
`4d9be1dde94d31ae5512d246ca33cff4e476eeead500efaa66039da700d6de02`,
`97f2b3a1e7e29c7c64bbf986f766d47f68bf59369bd38692fb87f24b781b5e6b`.
Initial harness pointer-entry/warp failures are retained. A first revision-only
fixture edit retained an incompatible world-frame proof; admission correctly
rejected it and preserved the active prior session. Clearing that proof in the
separate schema-2 replacement fixture fixes the experiment, not the validation.
No source or admission rule was weakened.


The next work is explicit non-flat source-bound support admission and actual
step/slope boundary fixtures through this session, followed by the dated local
M3 exit review before A0. No synthetic result substitutes for independent scale,
real Q5/N2 evidence, A1's pilot or native platform qualification.
