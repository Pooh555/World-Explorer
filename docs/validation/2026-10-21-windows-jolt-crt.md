# Jolt MSVC runtime alignment — 2026-10-21 (Asia/Hong_Kong)

E2 remains **IN_PROGRESS**. The new native Windows excerpt lists successful
compilation of `JobSystemThreadPool.cpp`, the source that previously failed on
SDK C4865. It then fails linking `we_core_tests.exe`: Jolt objects declare
`MTd_StaticDebug`, while `application_tests.cpp.obj` declares
`MDd_DynamicDebug`. LNK2038 reports the incompatible runtime libraries;
LNK4098 identifies `LIBCMTD`, and LNK1319 reports 95 mismatches. No CPU-test
pass is present. The excerpt omits compiler/SDK versions, run URL and source
commit; these are not inferred from the earlier log.

Repository root is the default cwd; parent is `37743df`. Ignored raw logs and
probes are under `artifacts/validation/windows-jolt-crt-20261021/` (`R`).
`R/supplied-msvc.log` SHA-256 is
`4f35490fa7970bf781ffe2623bb63386bdaac3141db880c00a875a29fb35e7e4`.

Jolt 5.3.0's recipe defaults `USE_STATIC_MSVC_RUNTIME_LIBRARY` on and initializes
its target with a static CRT. The parent project uses CMake's default dynamic
CRT. After dependency creation, the parent now sets Jolt's
`MSVC_RUNTIME_LIBRARY` to the explicit parent setting, including an explicitly
empty value, or to CMake's default
`MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`. See the official
[CMake property contract](https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html)
and Microsoft's
[LNK2038 explanation](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk2038?view=msvc-170).
The correction aligns the library with first-party targets without suppressing
linker checks or adding `/NODEFAULTLIB`. Enum conformance, warning errors,
language standards, source pins and the release-vcpkg recipe stay unchanged.

`python3 R/run_runtime_probe.py` exits 0. Its retained CMake project executes
the actual parent option block, starting Jolt with the upstream static target
property. clang-cl 23.1.3 targets `x86_64-pc-windows-msvc`; llvm-lib builds tiny
COFF archives, and `llvm-readobj --coff-directives` inspects their CRT requests.
The parent revision reproduces `/MTd`/`libcmtd.lib` against
`/MDd`/`msvcrtd.lib`. All eight corrected policy/configuration pairs compile
with matching Jolt/consumer flags and directives:

| Parent policy | Debug | Release |
|---|---|---|
| Default or explicit dynamic | `/MDd` | `/MD` |
| Explicit static | `/MTd` | `/MT` |
| Explicit empty | Neither target gets a CRT-selection flag | Same |

`R/runtime-probe-results.json`, the per-case logs, compile commands and COFF
directive dumps retain the results. These are cross-compiled minimal objects,
without Windows SDK/STL headers, full Jolt compilation, application linkage or
Windows execution. They verify CMake CRT selection, not a native MSVC pass.

`cmake --preset dev` and `cmake --preset compat-cxx23`, followed by their
`cmake --build --preset PRESET -j4` commands, exit 0 on GNU 16.2.1 with the
existing C++26/C++23 profiles. `ctest --preset dev --output-on-failure -j4`
passes **455/455** checks; the corresponding `compat-cxx23` command passes
**430/430**. `python3 tools/check_docs.py`, `git diff --check` and the selected
tracked-input inventory check (`python3 tools/check_dependency_manifest.py
--manifest third_party/dependency-audit-jolt-20261021.json`) exit 0. Existing
runtime-recapture and rights gaps remain open. No first-party C++ or Python
source was changed. A complete native Windows build and CPU suite remain
required to confirm the full repair. No remote push, GPU/reconstruction
run, M4 qualification or milestone completion is claimed. M3's next task remains
the 120 Hz capsule, and the M3 → A0–A5 → M4 order is unchanged.
