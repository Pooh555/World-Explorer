# A0 dependency contract — 23 October 2026

**VERIFIED: synthetic CPU contract slice. A0/AP0: IN_PROGRESS.** This follows
the [M3 local exit](2026-10-23-m3-development-exit.md). It does not produce,
complete, render or qualify assets, install models, or begin M4. AP1–AP9 and
A1–A6 remain NOT_STARTED. Full Q/P/N/E statuses are unchanged.

Default cwd: repository root. Parent `4d30d62`; ignored evidence root
`R=artifacts/validation/a0-dependencies-20261023`. `implementation.json` pins seven
implementation/build/gate inputs, SHA-256
`d8a5cef9083b4e3923f2996d1c58b50639a8cc3c40389976a0d4b2d1f1e785db`.
Finish receipts retain actual check results. The
[protocol](../protocols/asset_dependencies.md) owns exact fields, bounds and limits.

## Implemented behavior

New native `asset_catalog` depends only on project `core` and private JSON support;
no framework types enter its public headers. It is linked into CPU conformance
tests, without adding an application/viewer/solver path. Core owns a distinct
typed product ID. Admission validates a bounded closed DAG, independently
expected epoch/pose revision, exact dependency revision/content identities,
deterministic order/hash and transitive manual ancestry. Rejected/cancelled calls
preserve caller records. Content/recipe/action SHA-256 fields are declarations,
not file-byte/rights or evidence-class certification.

An independent Python standard-library oracle freezes the canonical fixture
(`oracle.json`/`oracle.sha256`):
`4be7d137ff187c08673fb7b9952b442fdf8a5e2afbee3800e42dbcfbeee0ba12`.
Native C++26/C++23 results match. Permuting products/dependencies preserves the
digest; distinct semantic products remain distinct with shared content bytes.
Recipe, revision, epoch/pose and manual-action changes alter the cache identity.
Tests reject malformed, duplicate, cyclic, missing and stale references, erased
direct/transitive manual ancestry, cancellation and all three exact budget edges
(256 products, 64 dependencies/product, 4,096 aggregate edges).

## Actual checks

| Commands | Results / logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | GNU 16.2.1 / C++26, 498/498 pass; `build-dev-verified.log`, `ctest-dev.log` |
| Same options with `compat-cxx23` | Explicit C++23, 473/473 pass; `build-cxx23-verified.log`, `ctest-cxx23.log` |
| `build/{dev,compat-cxx23}/we_core_tests '[asset-dependencies]'` | Six cases / 102 assertions in each profile; `focused-dev.log`, `focused-cxx23.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[asset-dependencies]'` | Six cases / 102 assertions, no ASan/UBSan/LSan reports; leak detection enabled; `build-asan.log`, `asan.log` |
| Exact LLVM 23.1.1 `python3 tools/check_cpp_style.py` on all four new C++ paths | Format/tidy/boundaries/docs/whitespace pass; `style-final.log` |

Initial tidy rejects four constant structured-binding names (`style-first.log`);
they are corrected without changing warnings. The include gate registers only
`core` and self as legal asset-catalog dependencies. Coverage scope includes the
new module in package/update validation; no coverage threshold/exclusion is
weakened and no fresh coverage percentage is claimed. Pinned Black checks the
changed Python gate using the required per-file tracked command.

No external dependency/version, worker environment, capture, saved job, retained
bake/map or selected viewer/CLI bytes change. Default/local dependency checks
exit 0; all fifteen selected root hashes and 378 protected media/result hashes
match (`checks.json`, `runtime-selection-check.json`, `preservation.json`).
Unknown runtime/model costs,
rights and installation sizes remain provisioning blockers; the current 8 GiB
host is not qualified for asset inference. Remaining A0 work includes file/receipt
admission, owned immutable stage/recovery records, evidence-class region masks,
full asset/instance schemas and capture/evaluation/recipe/resource lock proposals.
