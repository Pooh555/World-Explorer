# Native-build refactor repair — 2026-10-18 Asia/Hong_Kong

Scope/status: E1/E2 repair **VERIFIED locally**; native Windows/macOS repair
**IMPLEMENTED_UNVERIFIED**. Milestone and reconstruction gates retain their
existing statuses. Parent `a425b843fcd919dc23767a4ddd9ef9eedddc44f0`;
implementation is the commit containing this report. All commands use repository
root as cwd. Raw local logs are in ignored
`artifacts/validation/native-style-repair-20261018/` (`R` below).

The supplied native CPU log fails two batch-admission tests: the expected PPM
size is 19 bytes but the actual file is 17 bytes, and disk admission unexpectedly
succeeds. The style conversion changed the fixture header's dimensions from
`2 1` to `2 1uz`. Restoring the header preserves the existing exact-bound and
one-byte-under tests, including caller-data preservation and resume checks.
The style checker now masks character/string literals consistently before
checking C++ initialization and size suffixes. It previously required suffixes
inside literal data. New regressions accept the original header and still reject
an unsuffixed size outside the string. Eight tooling tests now run through
`cpp_style_tooling` in CPU CTest; compile-database fixtures are independent of
local build directories.

The supplied MSVC 14.51.36231 build fails in `file_lock.cpp` and `asset_files.cpp`.
The refactor renamed only active Linux declarations/references. Repairs cover
the Windows destructor, temporary paths, durable-flush constants, map-lock path,
bundle process ID/write result and preferences replacement. Windows worker
execution also now uses the renamed `ProcessControl`, `ProcessOutputCallback`,
members and input limit from the shared header. Existing ownership, durability,
process cancellation and resource policies remain unchanged.

## Actual verification

Local host: Linux x86-64, GCC 16.2.1; installed Clang/format/tidy **23.1.3**.
Prescribed format/tidy **23.1.1** were also recovered from existing local package
cache archives into ignored `artifacts/tools/llvm-23.1.1` (`T`), including their
matching libraries and resource headers. No system installation was changed;
`R/pinned-tool-archives.sha256` records both archive identities. No native
Windows/macOS run or GPU run is claimed.

| Command / observation | Actual result / log in R |
|---|---|
| New literal regression against `HEAD:tools/check_cpp_style.py`, loaded separately with `git show` | Expected exit **1**; reproduces the false suffix diagnostics, `style-regression-before.log` |
| `python3 -m unittest discover -s tests/tools -p check_cpp_style_test.py -v` | Exit **0**, **8/8**, `style-tooling.log` |
| `cmake --preset dev`; `cmake --build --preset dev -j 4` | Exit **0**, GCC C++26 viewer/CLI/tests, `dev-configure.log`, `dev-build.log`, `dev-final-build.log` |
| `ctest --preset dev` | Exit **0**, **303/303**, **104.88 s**, `dev-ctest.log`; started before the new tooling case was registered, checked separately below |
| `ctest --preset dev -R cpp_style_tooling` | Exit **0**, **1/1** (eight Python cases), `dev-style-tooling-ctest.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Exit **0**, explicit GCC C++23 CPU profile, **281/281**, **78.64 s**, `compat-{configure,build,final-build,ctest}.log`; includes the new tooling case |
| Pinned style gate below | Exit **0**, all **7 changed C++ files**; format/tidy **23.1.1**, warnings as errors, docs/boundaries/diff checks, `style-pinned-changed.log` |
| `python3 tools/check_cpp_style.py` | Exit **0**, **182 files**, **104 selected tidy sources**, installed format/tidy **23.1.3**; no first-party diagnostics, `style.log` |
| `python3 tools/check_docs.py`; `git diff --check`; `git diff --cached --check` | Exit **0**, **767 local targets / 163 documents**, `docs.log`, `diff-check.log`, `staged-diff-check.log` |

Pinned command (with `T=artifacts/tools/llvm-23.1.1`):

```sh
LD_LIBRARY_PATH="$PWD/$T/usr/lib" PATH="$PWD/$T/usr/bin:$PATH" \
python3 tools/check_cpp_style.py \
  apps/we_viewer/preferences.cpp src/core/asset_files.cpp src/core/file_lock.cpp \
  src/map_store/map_persistence.cpp src/reconstruction_adapters/process.cpp \
  src/scene_store/bundle.cpp tests/unit/batch_pose_admission_tests.cpp
```

Saved reconstructions, source videos, worker environments and specification/
runbook were not modified. No processing, dependency download, upload or remote
push was performed. Rerun the existing `native-builds` workflow on the repair
before claiming Windows/macOS success; physical viewer/release qualification
remains separate. The M3 next executable step remains in [progress](../progress.md).
