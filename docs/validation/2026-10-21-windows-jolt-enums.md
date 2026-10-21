# MSVC Jolt enum-conformance repair — 2026-10-21 (Asia/Hong_Kong)

E2 remains **IN_PROGRESS**; native Windows confirmation is pending. The supplied
compatibility-build excerpt stops in Jolt 5.3.0's `JobSystemThreadPool.cpp`:
MSVC toolset 14.51.36231 diagnoses Windows SDK 10.0.26100.0 enum definitions
with C4865, and upstream `/Wall /WX` makes them fatal through C2220. The
reported command lacks `/Zc:enumTypes`. This is a build failure before CPU tests,
not a reconstruction or collision-test failure.

Repository root is the default cwd; parent is `547719e`. Ignored evidence is
under `artifacts/validation/windows-jolt-enums-20261021/` (`R`). The retained
`R/supplied-msvc.log` SHA-256 is
`068a57c9e6093ce3c35a275f321b941053e1c2036428c5dfc8f124019ee5af60`.
The excerpt contains no run URL or source commit, so neither is inferred.

The development Jolt target now publishes the MSVC-only compile option
`/Zc:enumTypes`. Its direct header consumer, `collision`, inherits the same
enum-deduction mode; unrelated targets do not inherit it through that private
dependency. Microsoft documents
[C4865](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warnings-c4800-through-c4999?view=msvc-170)
and the
[conformance option](https://learn.microsoft.com/en-us/cpp/build/reference/zc-enumtypes?view=msvc-170):
the option is off by default, `/permissive-` does not enable it, and enabling it
can change enum layout. Keeping library/header-consumer modes consistent is
therefore intentional. Warning levels, warnings-as-errors, dependency source
pins and C++ standard selections are unchanged. No warning suppression, SDK
downgrade or upstream source patch is added. Release-vcpkg is outside this
development-target change and remains unqualified.

`cmake --preset dev` and `cmake --preset compat-cxx23`, followed by their
`cmake --build --preset PRESET -j4` commands, exit 0 on GNU 16.2.1. The generated
commands retain Jolt's C++17 and first-party C++26/C++23, with no MSVC switch in
GNU commands. A retained generation-only CMake probe executes the actual new
option block with MSVC/GNU/Clang branch IDs: Jolt and its direct consumer receive
the option only in the MSVC branch, and an unrelated target remains isolated.
This checks generator-expression scope; it does not compile Windows code or
qualify MSVC ABI/runtime behavior. Probe source, generated commands and output
are in `R/scope-probe*` and `R/scope-*`.

`ctest --preset dev --output-on-failure -j4` exits 0 with **455/455** checks;
the corresponding `compat-cxx23` command exits 0 with **430/430**. Raw suite
logs are `R/ctest-dev.log` and `R/ctest-cxx23.log`. `python3 tools/check_docs.py`,
`git diff --check` and `python3 tools/check_dependency_manifest.py --manifest
third_party/dependency-audit-jolt-20261021.json` exit 0. The inventory result is
tracked-input consistency only; prior runtime recapture/rights gaps remain open.
No first-party C++ or Python source changed, so no source-format, sanitizer or
GPU rerun is represented as evidence for this compiler-option repair.

No native MSVC toolchain or Windows SDK is available on this Linux host. The
existing Windows workflow must rerun its compatibility build and CPU contracts
to confirm this repair. No CI rerun, remote push, native-platform pass, GPU run
or milestone completion is claimed. M3's next task remains the 120 Hz capsule;
Q/P/N/E thresholds and the M3 → A0–A5 → M4 sequence are unchanged.
