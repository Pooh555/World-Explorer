# Native CI JSON and filesystem portability follow-up

2026-10-11. The user supplied the next macOS build log and Windows CTest log
after `a7c1911`. Both passed configuration; Windows also built and passed 22/24
CTest entries. The previous toolchain/OpenSSL discovery failures are resolved
in these supplied native logs. Full native CPU/profile qualification is pending.

macOS compilation failed at `json::parse` on byte-vector iterators. The JSON
adapter instantiates `std::char_traits<std::byte>`, which libc++ does not define.
Both manifest and provenance reads had this problem. They now parse an explicit
bounded `std::string_view` of the existing bytes, including a safe empty view.
Reads still enforce byte budgets; parsing and schema/checksum errors still fail
admission. No copy, feature-macro workaround or upstream-header patch is needed.
The [JSON parse contract](https://json.nlohmann.me/api/basic_json/parse/) documents
character/string inputs and iterator ranges; the pinned adapter implementation
and the supplied compiler diagnostic identify the unsupported byte traits.

Windows test 16 compared a resolved path using `runneradmin` with its fixture's
`RUNNER~1` spelling. Both refer to the same file; the assertion now checks
filesystem equivalence. Test 18's synthetic Python worker compared its canonical
training-directory argument with the fixture's uncanonicalized spelling, which
explains its assertion exit during start/resume. That assertion now uses
`Path.samefile`; source, video, checkpoint and resource arguments retain their
exact-value checks. Captured worker logs are included in failure diagnostics.
All branches require the worker's completion marker, and the changed-source
branch additionally requires INVALID_DATA and preservation of the prior manifest.
This prevents a worker assertion failure from satisfying the preservation test.

Regression coverage also verifies UTF-8 provenance round-trip and rejection of
empty, truncated, invalid UTF-8 and embedded-NUL malformed manifests. Tests still
check path escapes, allocation limits, checksums and failed publication. Targeted
tidy found a pre-existing fixture helper with interchangeable integer arguments;
its ID now uses `chunk_id`. Log callbacks use const references. No warning check
or test was suppressed.

Actual final commands on Arch Linux, each exit 0:

```sh
cmake --build --preset dev -j 4
ctest --preset dev
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23
cmake --build --preset clang -j 4
ctest --preset clang
mkdir -p artifacts/validation/ci-followup-tmp
ln -s ci-followup-tmp artifacts/validation/ci-followup-tmp-alias
TMPDIR="$PWD/artifacts/validation/ci-followup-tmp-alias" ctest --preset dev
cmake --preset compat-cxx23 -B build/ci-libcxx -DCMAKE_CXX_COMPILER=clang++ -DWE_BUILD_VIEWER=OFF \
  "-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem $PWD/.deps/libcxx-ci/root/usr/include/c++/v1" \
  "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -L$PWD/.deps/libcxx-ci/root/usr/lib -Wl,-rpath,$PWD/.deps/libcxx-ci/root/usr/lib"
cmake --build build/ci-libcxx -j 4
ctest --test-dir build/ci-libcxx --output-on-failure
clang-format --dry-run --Werror src/scene_store/bundle.cpp tests/unit/contracts_tests.cpp tests/unit/reconstruction_job_tests.cpp
clang-tidy -p build/dev src/scene_store/bundle.cpp tests/unit/contracts_tests.cpp tests/unit/reconstruction_job_tests.cpp --warnings-as-errors='*'
.deps/bin/actionlint .github/workflows/native.yml
python tools/check_boundaries.py
git diff --check
```

Results: **24/24 tests pass in every run**: GCC 16.2.1 C++26 development, GCC
16.2.1 C++23 CPU, Clang 23.1.1/libstdc++ C++23 CPU, Clang 23.1.1/libc++ 23.1.1
C++23 CPU, and GCC development through the symlinked temporary directory.
The alias run exercises both repaired path assertions with distinct spellings
of identical files. Format/tidy 23.1.1, actionlint 1.7.7, boundaries and diff
whitespace checks pass with no unsuppressed first-party diagnostics.

Local libc++ and libc++abi 23.1.1-1 packages were downloaded from the official
Arch package service, verified against its installed public keyring, and extracted
only into ignored `.deps/libcxx-ci/`. SHA-256:

```text
libc++:    35cde231751893fb2381d564dbeb458170cfc65c482d0d4169d6a3baab375703
libc++abi: 6467853a75a70fea9217862ac92a84137a7394f399b87db19609fb28264b076f
```

An isolated reproduction of the original byte-iterator call fails with the same
undefined `char_traits<std::byte>` diagnostic under this libc++. The complete
patched build above passes. Initial local test setup put `-stdlib=libc++` in
compile flags alongside `-nostdinc++`; Clang rejected that unused selection flag
under `-Werror`. The final setup selects headers during compilation and the
library during linking, retaining strict warnings. Logs, probe and signature
evidence remain ignored under `artifacts/validation/ci-followup-*`.

These checks exercise libc++ on Linux and aliased paths; they do not certify
Homebrew LLVM/libc++ 20 on macOS or Windows 8.3 handling natively. The next user
push should rerun the existing four jobs and confirm complete build/test passes.
No remote push, GPU run, reconstruction rerun or milestone promotion occurred.
