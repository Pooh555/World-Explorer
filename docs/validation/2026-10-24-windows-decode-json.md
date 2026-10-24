# Windows decode-coordinator comparison repair — 24 October 2026

**Reported C2666 source ambiguity repaired; full native Windows rerun pending.**
Local C++26/C++23 builds and stage-chain/admission checks pass. E2 and A0/AP0
remain IN_PROGRESS; no quality/platform gate or milestone is promoted.

Default cwd: repository root; timezone Asia/Hong_Kong. Parent `3869054`.
Ignored evidence root `R=artifacts/validation/windows-decode-json-20261024`.
The supplied native log is retained as `windows-failure.txt`, SHA-256
`27c2be5c7555820b5ca8ceeb6c21d520cdc3ac74ff2651e0551189bcf1f77ac0`.
It identifies neither source commit nor a test run. `identity.json` pins the
parent, repaired source and retained log/audit inventory.

## Cause and correction

The supplied `cmake --build --preset compat-cxx23 --parallel 4` run compiles
`decoded_frame_source.cpp`, both asset-catalog sources and the receipt-probe
object, and links `reconstruction_adapters.lib`/`asset_catalog.lib`. Thus the
[previous reader repair](2026-10-24-windows-quality-json.md) is past its reported
compile failure in this run. Full executable linking/tests remain unconfirmed.

MSVC **14.51.36231**, `/std:c++latest`, `/W4 /WX`, `/permissive-` and nlohmann JSON
**3.12.0** then report C2666 at `video_decode.cpp:275`: coordinator completion
validation directly compares JSON `selection` with `std::string_view K_SELECTION`.
JSON inequality and rewritten string-view equality have competing conversions.

The correction extracts `get_ref<const std::string&>()` before comparison.
Nonstring metadata still rejects through the existing `INVALID_DATA` exception
path; mismatched profile strings still reject. Accepted profile names, schemas,
cache keys, helper hashes, worker parameters, standards and warnings are unchanged.
Repaired source SHA-256:
`b1e7dcd35d427f7dd05bb4f35b7cbfece330503b88f6950328c78955ca0453ee`.

Reviewed explicit string-view declarations, parameters and label-returning uses
in `src/`, `include/` and `apps/` (`string-view-uses.txt`, produced by
`rg -n 'std::string_view' src apps include --glob '*.cpp' --glob '*.hpp'`). No
additional direct JSON comparisons were found in that review. The reader uses
typed extraction, fusion uses `text_is`, geometry scope uses a typed string,
digest comparisons use string results, and remaining JSON uses construct/assign
values or access keys. This source review does not certify MSVC compilation.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core build; `build-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4` | 0; explicit GNU C++23 build; `build-compat.log` |
| `build/dev/we_core_tests '[stage-chain],[decoded_admission]'` | 0; **28 cases / 42,824 assertions**; `focused-dev.log` |
| Same executable under `build/compat-cxx23/` | 0; **28 / 42,824**; `focused-compat.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py src/application/video_decode.cpp` | 0; format/tidy/boundaries/whitespace; `style.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; default inventory passes, saved runtime payload receipt remains stale after relinking; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Existing synthetic stage-chain tests execute automatic timestamp decode through
the repaired coordinator and verify restart/dependency/cancellation preservation;
decoded-admission fixtures also retain valid and tampered quality/tracking/
calibration coverage. No production test/threshold changes are needed for this
overload-only repair. All **378 protected media/result hashes** match
(`preservation.json`). No user-media decode, reconstruction, refit, bake, model
installation or upload occurs; dependency/environment identities are unchanged.

Fresh runtime observation remains required before claiming current local payload
consistency. Native Windows must rerun the same build and then
`ctest --preset compat-cxx23 --output-on-failure`; local passes do not establish
that result. The A0 semantic-recipe task remains in [progress](../progress.md).
