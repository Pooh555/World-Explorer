# Windows record-store observer repair — 24 October 2026

**Reported test-observer defect repaired; native Windows rerun pending.**
E2 and A0/AP0 remain IN_PROGRESS. The change affects one test translation unit;
record-store implementation, locking policy and product gates retain their scope.

Default cwd: repository root; timezone Asia/Hong_Kong. Parent
`1cae6aa59669cb20812da7ed1c9f16fb73b240a1`. Ignored evidence root
`R=artifacts/validation/windows-record-lock-20261024`. The supplied excerpt is
retained as `windows-failure.txt`, SHA-256
`836a6c27a850d5cdef5a735cc3a32182e68e3332869492ff32f39c3a2182ee58`.
It identifies neither source commit nor compiler version. `identity.json` pins
the parent, repaired test and 213 unchanged production source/header/app files.
Repaired `tests/unit/asset_metadata_io_tests.cpp` SHA-256:
`de9458d1872135ab67a51c6f2d77cb7af8c02f63c755eb28a36f5792c3eea172`.

## Supplied failure and correction

The Windows log runs the linked Catch2 executable and reports **568/569 CTest
passes**, including worker integration and `public_header_policy`. Its sole
failure is `record stores refuse foreign corrupt linked and locked output
without repair`: `tree()` cannot hash an entry at line 45, after 144 assertions
pass. Thus this excerpt shows a test failure after compilation/linking, rather
than another missing-header diagnostic. It does not establish a complete build
or platform pass.

In generator variant 8 the test acquires `.writer.lock`, then hashes every
entry while holding it. The Windows `FileLock` uses `CreateFileW` with share
mode zero, which deliberately denies a second read handle. POSIX advisory
locking permits that read, masking the observer error locally. This is the
same platform distinction recorded in the earlier
[map-store observer repair](2026-10-14-windows-map-observer.md).

The test now snapshots every entry before acquiring the lock, attempts the
publication while the lock remains held, requires rejection and checks
`WOULD_BLOCK`, then releases the handle before hashing the complete tree again.
The lock file remains in the byte-preservation comparison; no path or hashing
failure is skipped. The test also reopens the unchanged committed manifest and
successfully retries operation 2 at generation 1, reaching generation 2. The
eight foreign/corrupt/linked variants retain their rejection and preservation
checks. Production locking and file-sharing permissions are unchanged.

## Actual local checks

All commands below exit **0** on this Linux host. Logs are under R.

| Command | Result / log |
|---|---|
| `cmake --build --preset dev -j 4` | GNU 16.2.1 C++26 viewer/CLI/core build; `build-dev.log` |
| `cmake --build --preset compat-cxx23 --parallel 4` | GNU 16.2.1 explicit C++23 CPU build; `build-compat.log` |
| `build/dev/we_core_tests '[asset-metadata-io]'`; same under `build/compat-cxx23/` | **11 cases / 1,292 assertions each**, including all nine failing-test variants and controlled child-exit recovery; `focused-dev.log`, `focused-compat.log` |
| `ctest --preset dev -j 4`; `ctest --preset compat-cxx23 -j 4` | **598/598**, **573/573**; `ctest-dev.log`, `ctest-compat.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py tests/unit/asset_metadata_io_tests.cpp` | Format, tidy, boundaries and whitespace pass; `style.log` |
| `python3 tools/check_docs.py`; `git diff --check` | Pass; `docs.log`, `whitespace.log` |
| `python3 R/check_preservation.py` (expand R) | **378 protected media/result hashes** match; `preservation.json` |

No user-media processing, reconstruction, GPU run or dependency-inventory refresh
occurred. Saved runtime receipts still need fresh observation after relinking.
Linux results verify the test's preservation, rejection and retry assertions;
they do not reproduce Windows sharing denial. Rerun the native Windows
`cmake --build --preset compat-cxx23 --parallel 4`, then
`ctest --preset compat-cxx23 --output-on-failure`. Full native confirmation
remains due; current A0 work stays in [progress](../progress.md).
