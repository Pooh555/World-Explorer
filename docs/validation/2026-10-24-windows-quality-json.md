# Windows quality-policy comparison repair — 24 October 2026

**Reported MSVC C2666 source ambiguity repaired; native Windows rerun pending.**
Local C++26/C++23 builds and decoded-admission checks pass. E2 remains
IN_PROGRESS; A0/AP0 and all milestone/quality/platform statuses are unchanged.
This maintenance repair does not begin M4 or alter capture schemas, policy
hashes, thresholds, dependencies, language standards or warning settings.

Repository root is the default cwd; timezone Asia/Hong_Kong. Parent `79969b9`.
Ignored evidence root `R=artifacts/validation/windows-quality-json-20261024`.
The supplied Windows build log is retained as `R/windows-failure.txt`, SHA-256
`a40db0d20dd972053512695c61058e51d907b286d9e3e77a9c284bcf11091df8`.
It does not identify the source commit or contain a test run.

## Cause and correction

`cmake --build --preset compat-cxx23 --parallel 4` stops compiling
`decoded_frame_source.cpp:278`. MSVC **14.51.36231** uses `/std:c++latest`,
`/W4 /WX`, `/permissive-` and nlohmann JSON **3.12.0**. JSON's inequality operator
and rewritten standard string-view equality have similar conversions for
`json != std::string_view`. The log reports one C2666 error; concurrent compiler
jobs finish before Ninja stops. Linking collision/Jolt libraries earlier in this
log does not demonstrate final executable linking or resolve all prior CRT gates.

The one-line correction extracts `policy_sha256` with
`get_ref<const std::string&>()` before comparing it with the expected policy view.
Both operands now have explicit string types; JSON comparison overloads are
excluded. Wrong JSON types throw into the existing `INVALID_DATA` admission
path; wrong strings and recomputed-policy mismatches still reject. All three
candidate profiles retain their exact policy identities. This follows the same
typed-comparison approach as the [earlier fusion repair](2026-10-19-windows-fusion-json.md).
Repaired source SHA-256:
`71269aa9d042dbb006201f31238b0a220be66c30f1e1a8695caaf91a436c736a`.
`identity.json` retains parent/source/log identity.

## Actual local checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core build; `build-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4` | 0; explicit GNU C++23 build; `build-compat.log` |
| `build/dev/we_core_tests '[decoded_admission]'` | 0; **21 cases / 42,464 assertions**; `decoded-dev.log` |
| `build/compat-cxx23/we_core_tests '[decoded_admission]'` | 0; **21 / 42,464**; `decoded-compat.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py src/reconstruction_adapters/decoded_frame_source.cpp` | 0; format/tidy/boundaries/whitespace; `style.log` |
| `python3 tools/check_dependency_manifest.py` | 0; `inventory.log` |
| Same with `--local-evidence` | **2**, current runtime payload differs after viewer/CLI relinking; `inventory-local.log`; fresh runtime receipt required before claiming current local consistency |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Existing synthetic admission fixtures exercise valid quality/tracking/calibration
profiles, rehashed policy tampering, stale/hash/finite/budget rejection, caller
preservation and retry. No new test or production behavior is needed for this
overload-only correction. All **378 protected media/result hashes** match
(`preservation.json`); no decode, reconstruction, fitting, bake or media upload
occurs. No dependency/environment is changed or source inventory refreshed.

The Linux checks do not reproduce MSVC or certify native Windows. Next Windows
check is the same native build command, followed by
`ctest --preset compat-cxx23 --output-on-failure`; retain the complete result.
The current A0 semantic-recipe task remains in [progress](../progress.md).
