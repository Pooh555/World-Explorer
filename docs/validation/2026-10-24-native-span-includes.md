# Native public-header include repair — 24 October 2026

**Reported missing `std::span` declaration repaired; native CI reruns pending.**
Local C++26/C++23 builds, full CPU suites and isolated header probes pass. E2
and A0/AP0 remain IN_PROGRESS; no platform or product milestone is promoted.

Default cwd: repository root; timezone Asia/Hong_Kong. Parent
`527d1e64f8c8bf8d647b5e47080af279d8d9a594`. Ignored evidence root
`R=artifacts/validation/native-span-20261024`. `implementation.json` pins all
seven changed build/tool/header inputs; its SHA-256 is
`00dbc1310e8acee4d85d8c235953e1302d60e4564b746a0196c8dcb5ac981451`.

## Failure and correction

The supplied Windows excerpt reports MSVC **14.51.36231** C2039/C7568 at
`asset_catalog/definition_io.hpp:9` under `/std:c++latest`, `/W4 /WX`.
The attached Linux log reports the same undeclared `std::span` under **GCC 13**
and explicitly identifies missing `<span>`. It is retained as `linux-failure.txt`,
SHA-256 `4f6e0ac7016f5627888dd729a30b27ea9e1c86026e0cc4247407e13012a4364a`.
Both use `cmake --build --preset compat-cxx23 --parallel 4`; their source commit
is unreported. The user also reports macOS failure, without a macOS log.

Four public headers used `std::span` without including `<span>` directly:
`asset_catalog/definition_io.hpp`, `scene_store/asset_instances_io.hpp`,
`scene_store/depth_space.hpp` and `scene_store/fusion_cleanup.hpp`. All four now
include it; the byte-oriented signatures also include `<cstddef>`. The local
newer libraries happened to provide the declaration transitively: isolated
pre-fix decoder-header probes **passed**, so these are not local reproductions
of the supplied compiler failures.

The existing header policy now requires a direct `<span>` include when code
uses `std::span`, ignoring comments and string/raw-string contents. The new
`--headers-only` mode requires Python without formatter/tidy discovery. A
`public_header_policy` CTest runs it across all **82 public headers** on CPU CI.
Before the include repair it rejected all four headers (`header-policy-before.log`);
after repair it passes. Standards and compiler warnings retain their settings.
This targeted lexical rule does not prove every header dependency is complete.

## Actual checks

All commands below exited **0**, except the intentional pre-repair policy
failure (**1**). Logs and probe scripts are under R.

| Command | Result / log |
|---|---|
| `cmake --build --preset dev -j 4` | GNU 16.2.1 C++26 viewer/CLI/core build; `build-dev.log` |
| `ctest --preset dev -j 4` | **598/598**, including worker integration and header policy; `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 --parallel 4` | GNU 16.2.1 explicit C++23 CPU build; `build-compat.log` |
| `ctest --preset compat-cxx23 -j 4` | **573/573**; `ctest-compat.log` |
| `python3 R/check_headers.py` (expand R) | **30/30** isolated checks over 15 relevant public headers: GNU 16.2.1 C++26 and Clang 23.1.3 C++23/libstdc++ 15.3.0, warnings as errors, no PCH; `header-compile.log`, exact argv in `probes-after.json` |
| `python3 -m unittest discover -s tests/tools -p check_cpp_style_test.py -v` | **10** methods pass, including missing/direct/commented includes and formatter-free mode; `style-tests.log` |
| `python3 tools/check_cpp_style.py --headers-only include/world_explorer` | Public policy passes; `header-policy-after.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` with the four changed headers and both decoder source files | Format, tidy, documentation, boundaries and whitespace pass; `style-final.log` |
| Pinned Black 26.5.1 tracked-file check, one file per invocation per AGENTS | Pass; `black.log` |
| `python3 tools/check_docs.py`; `git diff --check` | Pass; `docs.log`, `whitespace.log` |
| `python3 R/check_preservation.py` (expand R) | All **378 protected media/result hashes** match; `preservation.json` |

No reconstruction, model installation, GPU validation or dependency-inventory
refresh ran. Saved runtime receipts still need fresh observation after relinking.
Rerun the compatibility build and `ctest --preset compat-cxx23 --output-on-failure`
on native Windows, macOS and the reported GCC 13 runner. Local passes establish
only the tested profiles. The next A0 task remains in [progress](../progress.md).
