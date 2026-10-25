# macOS draft-test JSON repair — 25 October 2026

**VERIFIED locally for the repaired compile path and GNU focused tests;
IMPLEMENTED_UNVERIFIED on native macOS.**
E2 build repair only. A0/AP0 remain IN_PROGRESS; the next asset-definition edit
task and all product/platform qualification duties remain unchanged.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`17c02c389d085e2b452fa5b030218002772ab976`.
Ignored evidence root `R=artifacts/validation/macos-draft-json-20261025`.
The [supplied native job](https://github.com/Pooh555/World-Explorer/actions/runs/40160176808/job/121069891265#step:11:437)
uses Homebrew LLVM 20/libc++/arm64, explicit C++23 and warnings as errors.
`supplied-macos.log` retains its excerpt; `implementation.json` pins the parent,
original/patched source, supplied log, compiler and libc++ configuration identity.
Its SHA-256 is
`81c71143a7bf96066f43beb66b2395d45cd133208323d223aa72814b2f279a54`.

The build fails at `tests/unit/draft_mutation_tests.cpp:79`: passing a
`std::vector<std::byte>` directly to `Json::parse` instantiates undefined
`std::char_traits<std::byte>`. The existing versioned-intent test now parses an
explicit `std::string_view` over the same owned bytes and exact length, as existing
production/test readers already do. The view lives only during parsing; no
fixture bytes, expected assertions, production parser, dependency, warning policy
or language standard changes. No extra regression was added: the existing case
is the failing compile path and still checks v2 encoding, selected binding,
replay/read refusal and invalid-hash preservation.

The saved 109-call JSON parse audit identifies this direct byte-vector use;
other apparent byte-reader calls use strings/views/character pointers/streams,
and the fuzz payload iterator uses `uint8_t`, not `std::byte`. No other repair
was needed in that audit. This repeats the portability issue documented in the
[earlier visibility repair](2026-10-19-macos-byte-json.md), in a subsequently added
test; historical passes are not upgraded.

## Actual checks

Local host: Linux x86_64; GNU 16.2.1 C++26/C++23 and system Clang 23.1.3 with the
retained clean isolated libc++/libc++abi 23.1.1 C++23 profile. This is not a native
Homebrew LLVM 20/macOS pass. No global toolchain or worker installation changed.

| Command | Exit / result; logs under R |
|---|---|
| `python3 R/libcxx_probe.py libcxx-before` (expand R) | **1**, same two byte-traits errors in the original complete test translation unit; `libcxx-before.log`, `libcxx-before-argv.json` |
| Same with `libcxx-after` | **0**, patched translation unit; `libcxx-after.log`, `libcxx-after-argv.json` |
| `cmake --build --preset dev -j 4`; `build/dev/we_core_tests '[draft-mutations],[draft-attempts],[object-mask-edit]'` | **0**, native C++26 viewer/CLI/tests build; **28 cases / 9,669 assertions**; `build-dev.log`, `focused-dev.log` |
| Same build/test with `compat-cxx23` (`--parallel 4` build) | **0**, explicit GNU C++23 CPU; **28/9,669**; `build-compat.log`, `focused-compat.log` |
| `cmake --build build/ci-libcxx -j 4` | **1**, separate Jolt C++17 PCH failure below; `build-libcxx.log`; focused libc++ execution did not run |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py tests/unit/draft_mutation_tests.cpp` | **0**, format/tidy/boundaries/docs/whitespace; `style.log` |
| `python3 tools/check_docs.py`; `git diff --check` | **0**; `docs.log`, `whitespace.log` |
| `python3 R/check_preservation.py` | **0**, **378 protected media/result hashes match**; `preservation.json`, `preservation.log` |

The reproducer reuses the development compile database's complete include/macro
arguments, selects explicit C++23 and retained libc++ headers, and runs
`clang++ -fsyntax-only` on the actual test source. Both exact argument arrays and
the original source copy are retained. No mirror parser or standard-library shim.
The existing libc++ CPU cache rebuild uses its unchanged explicit C++23 profile;
its cached CMake compiler identification predates the current compiler binary.
It compiles the core/asset-catalog objects, then Jolt's own C++17 PCH fails with
missing `std::is_trivial` under the retained libc++ 23 headers. This separate
local dependency/header issue blocks a full libc++ build and execution claim.
No dependency patch or forced compatibility shim was introduced. The supplied
LLVM 20 macOS log reaches the test object after Jolt, so it establishes the
reported byte-traits failure independently of this newer local-profile limit.

No media processing, inference, sanitizer rerun, full CTest, runtime inventory
refresh, GPU or native platform qualification was requested for this test-only
conversion. The local build and focused existing cases verify this repair's
scope. A fresh native macOS `native-builds` job remains necessary to establish a
native build/test pass; local libc++ evidence cannot certify it. Runtime receipts
remain stale after relinking.
