# A0 recipe lock inspection — 24 October 2026

**VERIFIED: synthetic local CPU contract slice; A0/AP0 remain IN_PROGRESS.**
Native recipe inspection extends owned [byte receipts](2026-10-24-asset-receipts.md)
with exact dependency/source binding, bounded declarations and resource/rights
blockers. External component/config/prompt/terms/review bytes and installed files
remain unverified; there is no provisioning, inference, rights approval or asset
output. A1–A6/AP1–AP9 and full Q/P/N/E statuses are unchanged.

Default cwd: repository root; timezone Asia/Hong_Kong. Parent `e026c3f`;
ignored evidence root `R=artifacts/validation/a0-recipes-20261024`.
`implementation.json` pins all six source/build inputs, SHA-256
`610e42d54cfeb4e8d9de34b3e21dbd7c751d9f76b7deaa6af5745423262549e3`.
The [protocol](../protocols/asset_recipes.md) owns exact fields, bounds,
declaration limits, blocker meanings and ownership semantics.

## Implemented and exercised

Inspection selects a node's exact recipe from admitted bytes, rechecks the
independently expected epoch/pose and graph dependencies, and binds `INPUT`
hashes to admitted source content. Parsed results own their values; source edits
after admission cannot replace them. Code/environment/model identities, separate
local/distribution rights declarations, nullable installation costs and explicit
device/RAM/VRAM/scratch/time/count limits remain frozen in the recipe hash.

Ten new cases cover malformed/rehashed/stale records, duplicate keys/NUL/depth,
the exact 64 KiB boundary, exact 64 components/16 models and excess, role/input
binding, nullable costs, restricted/unreviewed rights, exact and insufficient
host capacities, uint64 reservation overflow (also with unknown costs),
cancellation, known zero versus unknown archive costs, and bounded independent blob copies. Eight valid configuration/
prompt/seed/code/environment/model/rights/budget changes alter recipe, graph and
receipt identity while old snapshots remain inspectable. No self-declared
permission removes `COMPONENT_BYTES_UNVERIFIED`; the host remains contracts-only.

An independent Python standard-library producer creates six synthetic frozen
graphs/receipts (`oracle.py`). An ignored C++ probe links the actual native
library. Four inspections succeed with independently expected sums/blockers;
stale dependency and forged-input cases reject. Recipe, canonical graph and
receipt identities match; all source files remain unchanged. A seventh isolated
fixture rejects object-valued `components` through the intended malformed-data
path (`malformed-components.json`). Final binary replay is in
`oracle-release-slice.json`. Known fixture recipe SHA-256:
`e95acb197d2b668192fa76e0c0a0c635b6629bba03c22fb890adbf2839b833fc`;
receipt `1111605910b3f08e1614c8940d37550aa2cf5cf559c19a9ed48b0c143add7636`.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | 0; GNU 16.2.1 C++26, **518/518**; `build-dev-release-slice.log`, `ctest-dev-release-slice.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **493/493**; `build-compat-release-slice.log`, `ctest-compat-release-slice.log` |
| `build/{dev,compat-cxx23}/we_core_tests '[asset-dependencies],[asset-receipt],[asset-recipe]'` | 0 each; **26 cases / 2,955 assertions**; `focused-dev-release-slice.log`, `focused-compat-release-slice.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native matching focused command under `build/asan/` | 0; **26 / 2,955**, no ASan/UBSan/LSan reports, default leak detection retained; `build-asan-release-slice.log`, `asan-release-slice.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on both receipt/recipe headers and implementations plus the new test | 0; format/tidy/boundaries/docs/whitespace; `style-release-slice.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; unchanged inventory, runtime payload receipt still stale after preceding Windows repairs; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Probe compilation: `c++ -std=c++2c -Iinclude R/probe.cpp build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`; producer: `python3 R/oracle.py`.
Final retained fixtures are replayed against the final probe binary.
Initial tidy reaches a Catch2 negative-assertion enum-bitmask analyzer warning;
equivalent positive assertions resolve it without disabling warnings
(`style-first.log`, `style-final.log`). An intermediate gate correctly rejects
links before this report exists (`style-final-source.log`); final gate passes.

All **378 protected media/result hashes** match (`preservation.json`). No saved
decode, solve, fit, bake, map or source is rerun/changed; new fixtures stay ignored.
No dependencies/environments or viewer features change. Native Windows/macOS
confirmation, current runtime recapture, trusted external-payload/rights and
installed-file reconciliation, measured/enforced budgets and real asset output
remain open. No new coverage percentage or full AP0 pass is claimed.
