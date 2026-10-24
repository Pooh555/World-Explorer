# A0 installed components — 24 October 2026

**VERIFIED: bounded synthetic Linux CPU slice; A0/AP0 remain IN_PROGRESS.**
Read-only reconciliation now binds explicitly selected code/environment/model
roots to admitted component inventories. Full installations, archive expansion,
runtime closure, rights, worker limits and real asset inference remain unqualified.
The [protocol](../protocols/asset_installed_components.md) owns the exact contract.

Cwd: repository root; timezone Asia/Hong_Kong. Parent `f228935`;
`R=artifacts/validation/a0-installed-components-20261024` (ignored).
`implementation.json` pins nine source/build inputs; SHA-256
`eb87f417e6a54361dc1ef8e1574c4bc884a03e208b0c849608b78fa11058341a`.

## Observed behavior

The API revalidates recipe/evidence snapshots and independent review selections;
it accepts no caller-manufactured inspection as authority. Require exact role/hash
root selections, absolute nonlinked directories and disjoint filesystem identities.
Two closed-tree scans reject missing, changed, case-aliased, extra, linked or
special entries. Exact declared/hash budgets are 64 MiB/pass, 8,192 enumerated
entries and 16 relative path segments; existing snapshot limits remain. Hashing
supports empty files and cancellation. Nothing is installed, repaired or imported.

Eight new cases exercise replay/order independence, immutable prior observations,
missing/changed/same-size corruption, extra files/empty directories, case changes,
file/directory substitution, wrong/missing/duplicate/overlapping/relative/unrelated
root selections, empty/model files, exact/excess depth/read/entry budgets,
cancellation, stale context and retained review/rights blockers. Linux-only link
coverage includes symbolic roots/ancestors, regular/dangling file links, hard links
and FIFO rejection; outside targets and caller files remain unchanged. Existing
component fixtures moved unchanged into a shared test header.

Success clears only installed-file uncertainty for this bounded profile, adding
explicit runtime-closure and worker-limit blockers. Caller host/rights and missing
review blockers remain. Results are observations, never launch tokens. Portable
path-based checks do not contain hostile concurrent filesystem replacement or
make a passing tree immutable after return.

The independent Python observer (`python3 R/oracle.py`) prepares nine installed
fixtures from previously retained independent receipt bytes. Its native public-API
probe accepts selected/unselected/restricted reviews with expected blockers and
rejects missing/changed/added/linked files, overlapping roots and wrong reviewer.
Final replay (`oracle-current-results.json`) reproduces all expected results and
preserves every fixture/source entry. The selected fixture reports **2 roots,
2 files, 35 bytes**, blockers `[0,4,9,10]`; recipe identity
`d675f5ad2a59332e59d9333efc72de19827df3e844ee10690b24fc1e507e5650`,
evidence receipt `5cfce103a13736b585930874e537e0a025e142a10985ff7514b426dbd425e3ff`.
No worker framework is loaded.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | 0; GNU 16.2.1 C++26, **538/538**; `build-dev-current.log`, `ctest-dev-current.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **513/513**; `build-compat-current.log`, `ctest-compat-current.log` |
| `build/compat-cxx23/we_core_tests '[asset-installed],[asset-component-evidence],[asset-recipe],[asset-receipt],[asset-dependencies]'` | 0; **46 cases / 15,394 assertions**; `focused-compat-current.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests` with the same filter | 0; **46 / 15,394**, no ASan/UBSan/LSan reports, default leak detection retained; `build-asan-native.log`, `asan.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on the eight affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-finish.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; existing stale runtime payload after preceding relinks, no inventory change; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Probe: `c++ -std=c++2c -Iinclude -Ibuild/dev/_deps/json-src/include R/probe.cpp build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`.
`oracle.py` and recorded argument arrays identify selected roots/reviews and inputs.
Initial tidy rejects an unnecessary path copy; the reference then needs the repo's
reference-variable naming and named parent/child relation fields. Corrections
retain all warnings/checks (`style-first.log`, `style-second.log`). Sandboxed
sanitizer discovery fails because LSan cannot use ptrace (`build-asan-current.log`);
native discovery/run pass without disabling leak detection.

All **378 protected media/result hashes match** (`preservation.json`). No saved
decode/solve/fit/bake/map reruns, dependency/environment changes or model downloads.
Current runtime observation, full installation/rights qualification, native
Windows/macOS and real completed/PBR output remain open. No AP0 pass or new
coverage percentage is claimed; next A0 schema work is in the handoff.
