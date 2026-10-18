# macOS visibility JSON repair — 2026-10-19 Asia/Hong_Kong

E1/E2 repair: **VERIFIED locally** for the tested Linux GCC/libc++ profiles;
native macOS confirmation **IMPLEMENTED_UNVERIFIED**. M3 and all milestone/gate
statuses remain unchanged. Repository root is the default cwd. Byte-input fix
and regression were committed as `39e4e9b` during the interrupted session;
the commit containing this report adds the raw-NUL guard and correct empty-file
expectation. No user commit was rewritten. Ignored evidence root
`R=artifacts/validation/macos-byte-json-20261018` retains the session's original
directory date; `identity.json` pins final source/binaries and supplied log.

The [supplied macOS build](https://github.com/Pooh555/World-Explorer/actions/runs/39130840631/job/117685486479#step:11:198)
fails at `depth_visibility.cpp:47`, using Homebrew LLVM 20 / libc++ / arm64 /
explicit C++23. `json::parse(vector<byte>)` instantiates undefined
`std::char_traits<std::byte>`. The application now borrows a bounded
`std::string_view` over the same checksum-checked bytes with safe empty handling,
as in the [earlier bundle portability repair](2026-10-11-ci-followup.md).
No dependency/header patch, standard downgrade or warning suppression.

The new malformed-input regression initially exposed two distinct issues:
the shared asset reader deliberately rejects empty files as RESOURCE_EXHAUSTED,
and this JSON parser accepts a valid document followed by raw NUL and ignores
later bytes even with a bounded view. Preserve the existing empty-file behavior
and explicitly reject raw NUL before parsing. Escaped JSON text is unaffected.
Empty/truncated/NUL-suffixed requests reject without output creation or caller
request changes. Existing successful exports, checksum/domain/overlap rejects
and cancellation checks remain intact. Four focused cases / **84 assertions**
pass under both tested standard-library profiles.

## Actual verification

Local host Linux x86_64: GCC **16.2.1** C++26; Clang **23.1.3**, existing isolated
libc++/libc++abi **23.1.1** C++23. Pinned formatter/tidy **23.1.1**. The existing
libc++ cache was reconfigured/rebuilt; no download or worker-environment install.
This is not a native Homebrew LLVM 20/macOS run or platform/GPU qualification.

Reproduction command, before the character-view change (exit **1**, same byte
traits diagnostic in `libcxx-before.log`), then after (exit **0**,
`libcxx-after.log`):

```sh
clang++ -std=c++23 -nostdinc++ -isystem "$PWD/.deps/libcxx-ci/root/usr/include/c++/v1" \
  -Iinclude -isystem build/dev/_deps/json-src/include \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -fsyntax-only src/application/depth_visibility.cpp
```

Full libc++ configuration (exit **0**, `libcxx-configure.log`):

```sh
cmake --preset compat-cxx23 -B build/ci-libcxx -DCMAKE_CXX_COMPILER=clang++ -DWE_BUILD_VIEWER=OFF \
  "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem $PWD/.deps/libcxx-ci/root/usr/include/c++/v1" \
  "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -L$PWD/.deps/libcxx-ci/root/usr/lib -Wl,-rpath,$PWD/.deps/libcxx-ci/root/usr/lib"
```

| Actual command | Result / raw log in R |
|---|---|
| `cmake --build --preset dev -j 4` | Exit **0**, native viewer/CLI/tests, final `dev-build-complete.log` |
| `cmake --build build/ci-libcxx -j 4` | Exit **0**, full C++23 CPU application/CLI/tests, `libcxx-build.log` |
| `ctest --preset dev` after all development relinks complete | Exit **0**, **312/312**, **132.37 s**, `dev-ctest-final.log` |
| `ctest --test-dir build/ci-libcxx --output-on-failure` | Exit **0**, **289/289**, **101.60 s**, `libcxx-ctest.log` |
| `build/dev/we_core_tests '[visibility]'`; `build/ci-libcxx/we_core_tests '[visibility]'` | Both exit **0**, **4 cases / 84 assertions**; `visibility-dev-final.log`, `visibility-libcxx.log` |
| Pinned style command below | Exit **0**, both changed C++ files, format/tidy/boundaries/docs/whitespace; `style-final.log` |
| `python3 "$R/audit.py"` | Exit **0**, **24,579 protected files**, zero changed/missing, including saved runs/scenes/media, previous depth/fusion outputs, pose/train files and 1,188 dense environment files; `audit.json/log` |
| `python3 tools/check_docs.py`; `git diff --check` | Both exit **0**, **826 local targets / 173 files**, `docs-final.log`, `diff-final.log` |

```sh
LD_LIBRARY_PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/lib" \
PATH="$PWD/artifacts/tools/llvm-23.1.1/usr/bin:$PATH" \
python3 tools/check_cpp_style.py src/application/depth_visibility.cpp tests/unit/dense_input_tests.cpp
```

Initial focused failures remain in `visibility-dev.log`. Initial full development
testing (`dev-ctest.log`, exit 8) included that failure and overlapped relinking,
causing executable permission/not-run errors; it is not a final verification run.
Final full testing follows completed builds, without overlapping relinks.
No reconstruction, fusion, long stress, GPU, platform promotion or remote push.
Runtime loader/mapping receipt refresh remains due after relinking. Native macOS
must rerun the existing `native-builds` workflow before a native pass is claimed;
M3's next executable work remains [native fused-geometry admission](../progress.md#next-executable-step-and-protection-rules).
