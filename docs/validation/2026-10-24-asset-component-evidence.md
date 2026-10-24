# A0 component evidence — 24 October 2026

**VERIFIED: synthetic local CPU slice; A0/AP0 remain IN_PROGRESS.** Native
inspection binds component inventories, file/archive/config/prompt/terms bytes
and review records to an admitted recipe. Independently selected review hashes
remain separate from the supplied evidence. Actual installed roots, archive
expansion, transitive/runtime closure, legal authority, enforced worker budgets
and inference/output remain unqualified. A1–A6/AP1–AP9 and full Q/P/N/E statuses
are unchanged; M4 has not begun.

Default cwd: repository root; timezone Asia/Hong_Kong. Parent `91dbbf3`;
ignored evidence root `R=artifacts/validation/a0-component-evidence-20261024`.
`implementation.json` pins nine source/build inputs, SHA-256
`3f0703b581267895a677a5138246a8ab3dfde46b22d5b4378d77553151b54796`.
The [protocol](../protocols/asset_component_evidence.md) owns fields, bounds,
error/ownership/trust semantics and qualification limits.

## Exercised behavior

Two independently selected immutable snapshots provide recipe/source and evidence
bytes. Verification checks exact roles, configuration/prompt presence, known
archive/installed costs, complete declared inventories, term/review identities
and reviewer bindings. Missing/partial/unknown evidence rejects without writes.
No path is opened from an inventory or archive extracted. Empty files require
their exact mathematical empty-content SHA-256, not a missing nonempty blob.

Twelve new cases cover source/snapshot preservation after source deletion,
review self-selection refusal, wrong reviewers/hashes/roles/terms/decisions,
unreviewed/restricted/distribution separation, rehashed malformed JSON, duplicate
keys/NUL/depth, unsafe/case-aliased/device/parent paths, byte/count mismatches,
missing payloads, exact 256 KiB manifest/64 KiB record limits, 1,024-file and
4,096-total-entry bounds with excess, empty files, cancellation and stale context.
Complete byte verification replaces the byte blocker with
`INSTALLED_FILES_UNVERIFIED`; host/rights and missing-selection blockers remain.
This is not a provisioning or approval token.

An independent standard-library producer (`python3 R/oracle.py`) creates seven
synthetic fixtures and separate review-selection files. The ignored native probe
accepts selected/unselected/restricted records with independently expected
blockers, and rejects changed reviewer, missing file, wrong terms and unsafe path.
All fixture bytes remain unchanged. Final replay (`oracle-final-results.json`)
matches exact recipe/evidence identities and selected-review counts. Known fixture
recipe: `d675f5ad2a59332e59d9333efc72de19827df3e844ee10690b24fc1e507e5650`;
evidence receipt: `5cfce103a13736b585930874e537e0a025e142a10985ff7514b426dbd425e3ff`;
independent policy: `6ea46a6322ea7b1d8f9dc5603033e04a86ee27fa40c057f27f9731ddb159895a`.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | 0; GNU 16.2.1 C++26, **530/530**; `build-dev-slice.log`, `ctest-dev.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **505/505**; `build-compat-slice.log`, `ctest-compat.log` |
| `build/{dev,compat-cxx23}/we_core_tests '[asset-component-evidence],[asset-recipe],[asset-receipt],[asset-dependencies]'` | 0 each; **38 cases / 5,762 assertions**; `focused-dev-final.log`, `focused-compat.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native same focused command under `build/asan/` | 0; **38 / 5,762**, no ASan/UBSan/LSan reports, default leak detection retained; `build-asan-slice.log`, `asan.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on all eight affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-slice.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; unchanged inventory, runtime payload receipt remains stale after preceding Windows repairs; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Probe compilation: `c++ -std=c++2c -Iinclude -Ibuild/dev/_deps/json-src/include R/probe.cpp build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`.
The producer reuses the prior ignored recipe oracle's standard-library encoding
helpers; it does not call native code to construct hashes or expected outcomes.

Strict recipe JSON helpers and the existing recipe fixture moved into shared
private headers; retained recipe checks preserve their assertions/behavior.
LLVM 23.1.1 initially crashes in `bugprone-argument-comment` on a nested Catch
member-call assertion (`style-first.log`); local result bindings avoid it. Tidy
then identifies adjacent interchangeable fixture cost arguments (`style-final.log`);
named cost fields resolve that warning. No checks, warnings or thresholds weaken.

All **378 protected media/result hashes** match (`preservation.json`); no saved
decode, solve, fit, bake or map reruns occur. No dependencies/environments, viewer
features or model provisioning change. This bounded portable-file profile is not
a full installation/SBOM or large-model admission path. Native Windows/macOS,
current runtime recapture, actual installed files/rights and real asset output
remain open; no fresh coverage percentage or AP0 pass is claimed.
