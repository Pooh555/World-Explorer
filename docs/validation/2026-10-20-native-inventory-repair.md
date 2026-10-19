# Native build, formatting and inventory repair — 2026-10-20 (Asia/Hong_Kong)

Scope: repair the reported Windows compilation failure path, all 24 existing
Black failures and stale dependency inventory. Local checks pass; **native Windows
confirmation remains pending**. M3 remains IN_PROGRESS; no reconstruction,
asset-track, platform or rights gate is promoted.

Repository root is the default cwd. Parent `c1078454d758`; ignored evidence root
`R=artifacts/validation/native-inventory-repair-20261020`. `source-identity.json`
records all 32 implementation/inventory source identities; `formatting.json`
records the 24 before/after hashes and equal ASTs. Repaired geometry source SHA-256:
`120ce00d409950a35d0fc8039ae5ef2f102d542ad801d920da70a07f1667752c`.

The full Windows log supplied later confirms **MSVC 14.51.36231 C4702**
(unreachable code), promoted by `/W4 /WX` to **C2220**, in `geometry_job.cpp`
lines 482–650. `supplied-windows-diagnostic.txt` retains the decisive excerpt.
Public [Actions run 39288585246](https://github.com/Pooh555/World-Explorer/actions/runs/39288585246)
confirms failure only in the Windows build, with Linux GCC/Clang and macOS jobs
successful at the parent commit. Saved API job/annotation records identify those
jobs; the user's log supplies the compiler diagnostic absent from the public API.

A concrete defect was reproduced: the non-Linux geometry launcher returned
unconditionally before the Linux solver body. Clang's focused non-Linux branch
check (`non-linux-argv.json`, `-U__linux__ -Wunreachable-code -Werror`) failed at
that following statement. The implementation now selects separate Linux solver
and unsupported-platform definitions, with the fresh-depth helper compiled only
on Linux. The same branch check passes. `platform-probe-commands.json` compiles,
links and executes that branch locally, confirming BACKEND_FAILURE without
accessing missing caller input or creating output. A matching native-platform
regression is included in the CPU suite. These are local branch checks using
Linux headers/libraries; an actual MSVC/Windows rerun remains required. Standards and warning policy remain unchanged.

Pinned Black **26.5.1** removes the 24 failures with unchanged ASTs. Formatting
changes worker byte digests: historical pinned requests/recipes and completed
outputs remain immutable; future execution requires newly pinned requests.
No solver, training, media processing or environment repair was run.

Actual checks (exit **0**, except explicit historical/closure failures):

| Command / command record | Result / raw log |
|---|---|
| `cmake --build --preset dev -j 4` | GCC 16.2.1, C++26 viewer/CLI/core; `build-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4` | Explicit GCC C++23 CPU; `build-compat.log` |
| `ctest --preset dev` | **379/379**, 151.64 s; `ctest-dev.log` |
| `ctest --preset compat-cxx23` | **355/355**, 139.78 s; `ctest-compat.log` |
| `python3 tools/check_cpp_style.py src/application/geometry_job.cpp tests/unit/geometry_job_tests.cpp` | clang-format/clang-tidy **23.1.1**, boundaries/docs/whitespace; `quality-commands.json`, `style.log` |
| Required tracked-file Black pipeline below | All files pass; `black-final.log` |
| `python3 tools/check_docs.py`; `git diff --check` | `final-checks.json` |

```sh
git ls-files -z -- '*.py' | xargs -0 -n1 python3 -m black --check --quiet --workers 1
```

The checker's default now selects the new
[inventory](../../third_party/dependency-audit-repair-20261020.json), rather than
the original pre-VMA declaration. `capture_loader.py`, four `run_probes.py`
commands and `observe_viewer.py native` freshly observe current bytes before
`prepare_summaries.py`, owner capture and `prepare_inventory.py` publish new
records. Historical receipts are retained. Native RTX 4060 Laptop access is used
for the training import query and empty viewer; imports use guarded, isolated
work directories and request no optimizer/model/checkpoint execution.

Results: **15 roots / 213 loader ELF files / zero unresolved edges**;
**five fresh mapping contexts / 208 ELF files / zero changed overlap**;
**137 owner targets / 16 groups / 30 notices**. The empty native viewer renders
**900 frames / zero Vulkan validation errors**. `inventory-commands.json` records
default and local-evidence exit **0**, strict closure exit **3** for the unchanged
**253 gaps / 84 components / seven selected profiles**. Explicit old manifests
still exit **2**, preserving stale-evidence detection. No dependency pins,
comparison rules, gap checks or rights claims are weakened. Dense/fusion profiles
retain their separate evidence and are outside this selected inventory's scope.

`preservation.log`/`preservation.json` confirm **24,457 protected files**, four
original bakes and 33 prior audit/bundle files unchanged. The existing **3,982
external header differences** remain explicit and unrepaired; current mapped-ELF
consistency does not establish full installed-package integrity or source rights.
The execution prompt, saved media/runs, failed attempts and environments are
preserved. A native Windows rerun is still required to confirm its
reported build failure is resolved; the next product slice remains bounded
full-sequence depth/cache work in `progress.md`.
