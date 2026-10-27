# Windows probe environment repair — 2026-10-28

**Reported MSVC C4996/C2220 cause repaired; native Windows rerun pending.**
Three optional test artifact lookups now use an owned environment-value helper.
No production code, CMake, language standard, warning setting, test assertion,
artifact capability or milestone order changes. E2 and A0/AP0 stay **IN_PROGRESS**;
the recipe/capture/readiness audit remains next, and M4 has not begun.

Repository root `/home/Pooh555/programming/World-Explorer` is default cwd;
timezone Asia/Hong_Kong. Parent `2d14facd8028242d58f4a6a6198c53540a959ed8`.
Ignored evidence root `R=artifacts/validation/windows-probe-environment-20261028`.
`supplied-windows-excerpt.txt` retains the reported diagnostic, not the entire log;
the supplied log does not identify a source commit or successful test run.
Commands use `python3 R/run_check.py TAG COMMAND...`, retaining argv/cwd/exit/time.
Four C++ files have sorted SHA-256
`fd47aa63cef62c4b9e79eb15eb5fb88e07645676f07c4aa94828f75695165ed0`;
`source-identity.json` defines the algorithm, per-file hashes and excerpt identity.

MSVC **14.51.36231**, `/std:c++latest`, `/W4 /WX`, `/permissive-` stops at
`asset_external_models_tests.cpp:85`, build **426/524**, because `std::getenv`
is deprecated by its CRT. `native_shape_tests.cpp` and
`native_collider_diagnostic_tests.cpp` contain two more exposed calls that could
fail later. All three now use `test_support::probe_environment`; remaining
artifact `getenv` calls are inside existing Linux-only guards.

On MSVC, the helper calls `_dupenv_s`, immediately owns its allocation with
`unique_ptr`/`free`, and returns an owned optional string. On other compilers it
copies `getenv` storage. Missing and present-but-empty values remain distinct;
CRT errors fail the test instead of silently disabling output. This follows the
[Microsoft CRT contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/dupenv-s-wdupenv-s?view=msvc-170),
including freeing copied values and treating absent variables as non-errors.
No `_CRT_SECURE_NO_WARNINGS` or compiler-warning exclusion is added.

The isolated `R/crt_probe.cpp` diagnostic exercises the exact MSVC branch using
a host `_dupenv_s` shim, forbidding any `getenv` token in that branch. It checks
missing, empty, present and allocation-error returns with native ASan/UBSan/default
LSan. A separate POSIX run checks those environment states and independent value
ownership after changing the child environment. This is a branch/ownership check,
**not** an MSVC compiler/UCRT execution or native Windows pass.

| Actual final checks, exit 0 | Result / evidence tag |
|---|---|
| GNU16.2.1 C++26 development / explicit C++23 core build | `build-dev-final`, `build-compat-final`; normal warnings retained |
| Three affected groups on GNU C++26 | **27 cases /32,974 assertions**, 25.62 s (`focused-dev-final`), all three artifact variables set to fresh ignored paths |
| Same groups on GNU C++23 | **27 /32,962**, 25.12 s (`focused-compat-final`), artifact variables unset; 12 fewer artifact-write assertions |
| Native ASan/UBSan/default LSan diagnostics | `compile-crt-shim-final`, `compile-posix-probe-final`, `crt-shim-native-final`, `posix-probe-native-final`; both branches pass with no reported errors; sanitizer options unset |
| LLVM23.1.1 formatter/tidy and repository gates | `style-final`: four files pass; no warning settings changed |
| Clang23.1.4/libc++23.1.1 C++23 syntax | `libcxx-models-final-run`: originally failing test TU passes; no native-platform claim |
| Independent artifact parity | `product-oracle-final-run`: both 36-file external MODEL products match retained hashes/metadata apart from the selected native report path; shape input/report (two files) and both collider packages/requests/reports (62 files) are byte-identical |
| Protected data/docs/whitespace | `preservation-final-run`: **378 unchanged files**; `docs-final`, `whitespace-final` pass |

The 156,008,466-byte and doubled synthetic payload hashes remain
`da17ed8233dda77a0f80e391ac1288a7ec1532a88cc37cea146668183e553ac7` and
`4507efd42e061bba8728d55a9593f34471e03a54b4b2cea937d0affde61f2d66`.
New products live only under `R/products-final`; original products/media remain
unchanged. `product-oracle-final.json` retains the complete comparisons.
No decode, solve, fitting, model acquisition or native GPU execution occurs.
Full CPU suites were not rerun for this test-helper-only change; the prior
[request/CLI suites](2026-10-27-external-model-cli.md) retain their original scope.
The first independent parity check ran before the shape writer completed; that
failing receipt remains. Waiting for test completion and rerunning the identical
comparisons passes, without changing a production assertion or old artifact.

`R/finish.json` reconciles the final command/source/protection receipts and confirms
all production, CMake, specification and runbook files are unchanged. A complete
native Windows C++23 build/test rerun remains the next platform confirmation.
