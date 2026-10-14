# Windows map preservation observer — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**. The supplied Windows CPU run reaches CTest and
passes **148/155 CTest**, but seven canonical-admission cases fail in their shared
tree-preservation observer. The observer is repaired; local results below do not
certify native Windows. Parent `1bdcc63`; implementation is the commit containing
this report. Cwd: repository root. `R = artifacts/validation/windows-map-observer-20261014`
retains the supplied log, exact local evidence and preservation/source identities.

## Failure and correction

The supplied `windows-2025` log runs `ctest --preset compat-cxx23` under
`D:/a/World-Explorer/World-Explorer/build/compat-cxx23`: **110.84 s, exit 1**.
Cases **54–58, 60–61** fail at `persistent_map_tests.cpp:126`, the observer's
`REQUIRE(digest)`. The native child, earlier durable-map, codec and Python worker
integration tests pass. Build/compiler version and checkout SHA are absent from
this attachment; its SHA-256 and failing IDs are retained in `supplied-log.json`
alongside `supplied-windows-ctest.txt`. No macOS result was supplied for this slice.

The helper added in `1bdcc63` hashes every file, including the live store's
`writer.lock`. The Windows [lock implementation](../../src/core/file_lock.cpp)
uses `CreateFileW` with share mode zero; its owner intentionally blocks another
read handle. The observer thus fails before evaluating import preservation.
Linux advisory locking allowed the observer's inappropriate read and hid the defect.

The helper now accepts the **exact held lock path** through a distinct parameter
type, checks that entry is regular/nonsymlink and retains it in the inventory
without opening its contents. Every other regular file still receives SHA-256;
directories remain inventoried, unexpected entry types fail, and failures include
the observed path/hash error. Existing snapshots, manifest/assets, operation
retry and recovery assertions remain active. Production code, lock sharing,
worker/environment/schema/recipe and coverage exclusions are unchanged.

One new synthetic case verifies held-lock inventory, concurrent writer rejection,
content-change detection for an unrelated `writer.lock`, unchanged manifest/
snapshot, and normal hashing of the actual lock after store closure. No writer
ownership is released for live observation; no filename-wide exclusion is used.

## Actual local verification

Arch x86-64, GCC **16.2.1**, Clang/format/tidy **23.1.1**, CMake/CTest **4.4.4**.
All final successful checks below exit 0. Logs are in R.

| Command | Result / log |
|---|---|
| `cmake --build --preset dev -j 4`; `build/dev/we_core_tests '[canonical_admission]'`; `ctest --preset dev` | C++26 viewer/core build; **14 cases / 1,612 assertions**, **175/175 CTest**, 41.93 s; `build-dev-final.log`, `focused-final.log`, `ctest-dev-final.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Existing explicit GCC C++23 CPU-only profile; **157/157 CTest**, 41.14 s; `build-cxx23-final.log`, `ctest-cxx23-final.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/persistent_map_tests.cpp` | No user-code diagnostics; `tidy-final.log` |
| `clang-format --dry-run --Werror tests/unit/persistent_map_tests.cpp`; `python3 tools/check_docs.py`; `python3 tools/check_boundaries.py`; `git diff --check` | Pass; exact commands/exits in `static-commands.json` |

Initial local build/tidy attempts fail on Catch's unparenthesized ternary message
and adjacent same-type path parameters (`build-dev.log`, `build-cxx23.log`,
`tidy.log`, exit 1). Parenthesizing the message and using a distinct held-lock type
correct both without weakening warnings. The early `ctest-dev.log` attempt began
before the failed build was resolved and is not final-source evidence; only the
final logs above qualify this repair. No production or coverage failure is inferred
from these observer defects; coverage/sanitizer measurements retain their earlier
recorded scope.

Read-only hashing confirms **2,393/2,393 protected files unchanged** (`preservation.json`):
videos, retained reconstructions/fits/checkpoints and specification/runbook.
No reconstruction, GPU run, model/environment change, remote push or upload.
**Native Windows rerun remains pending**, using the existing CI workflow after
this commit is made available to its runner. Local Linux tests validate the repaired
observer contract, not Windows sharing/metadata execution or full milestones.
