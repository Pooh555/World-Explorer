# A0 byte-bound receipts — 24 October 2026

**VERIFIED: synthetic local CPU slice; A0/AP0 remain IN_PROGRESS.** Bounded
file admission and immutable publication/retry extend the
[graph contract](2026-10-23-asset-dependencies.md). No asset production, model
installation/inference, native PBR viewing, rights approval or M4 work occurs.
A1–A6/AP1–AP9 remain NOT_STARTED; full Q/P/N/E statuses are unchanged.

Default cwd: repository root; timezone Asia/Hong_Kong. Parent `7bff5a5`;
ignored evidence root `R=artifacts/validation/a0-receipts-20261024`.
`implementation.json` pins all five source/build inputs; SHA-256
`b077501ef61c0a65a5d724f4032258283804f0dab87b2c02a84465ad546e0b3c`.
The [protocol](../protocols/asset_receipts.md) owns fields, budgets, ownership,
error semantics and qualification limits.

## Implemented and exercised

The native reader requires an independently pinned manifest hash and epoch/pose
context, strict exact JSON fields and an exact hash/size inventory. It privately
owns every admitted byte before publication. Rehashed malformed inputs cannot
bypass graph, type, path or inventory validation. Tests exercise exact 1 MiB
manifest, 16 MiB/file, 64 MiB aggregate and 768-file limits, plus excess rejection.
Editing the original source after admission cannot change published bytes.

The publisher locks an owned single-operation directory, installs immutable
blobs and atomically exposes completion last. Tests verify unchanged completed
replay, cancellation/observer failure after one installed blob, byte-preserving
resume, lock contention and changed-context refusal before completion. A child
process deliberately exits 77 after one fully installed blob; the parent reopens
and completes the same request. Foreign/corrupt/missing completed data, operation
collisions and symlink/overlap redirections are rejected without replacing data.

The independent standard-library producer `python3 R/verify_receipt.py` creates a
separate **synthetic** three-blob fixture, computes canonical graph/receipt hashes
and invokes `build/dev/we_asset_receipt_probe` through argument arrays. Its five
runs exit **0, 0, 77, 0, 0**: fresh publish, unchanged replay, controlled crash,
resume and reopening the published source into another directory. Manifest hash
`4378ee4634aef8a00e7e03bbad906691690fdcb861c592626ec1cf6120145a47` and graph hash
`359d60f9a26ebc64dc688e583941bb01d0e4983462ccc7663e502f5b8b7e6d44`
are accepted by native admission. Source and retained partial-file hashes match
(`oracle-results.json`). The probe is test-only and is not installed.

## Actual checks

| Commands | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | 0; GNU 16.2.1 C++26, **508/508**; `build-dev-verified.log`, `ctest-dev.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **483/483**; `build-compat-verified.log`, `ctest-compat.log` |
| `build/{dev,compat-cxx23}/we_core_tests '[asset-dependencies],[asset-receipt]'` | 0 each; **16 cases / 2,062 assertions**; `focused-dev.log`, `focused-compat.log` |
| `cmake --build --preset asan -j 4`; native `build/asan/we_core_tests '[asset-dependencies],[asset-receipt]'` | 0; **16 cases / 2,062 assertions**, no ASan/UBSan/LSan reports; default leak detection retained; `build-asan.log`, `asan.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on the four new C++ files | 0; format/tidy/boundaries/whitespace pass; `style-verified.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py` and `--local-evidence` | 0 each; unchanged 86 components/seven profiles/257 open gaps; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Initial tidy rejects iterator increment in a compound condition, optional-member
access in fixture serialization and an unnecessary callback copy. Explicit
sequencing/local optional selection and a const-reference callback fix these
without warning changes (`style-first.log`, `style-final.log`). Intermediate
documentation checking correctly rejects links before this report exists
(`docs-pre-report.log`); final checking passes.

All **378 protected media/result files** match their retained identities
(`preservation.json`). An initial runtime comparison selected the superseded
`runtime-selection-m3-20261023.json` and flags two old executable hashes
(`runtime-selection-check.json`). The active **preflight** selection, used by the
current manifest, matches all **15 roots** (`runtime-selection-check-preflight.json`).
This slice does not relink the viewer/CLI or change dependencies/environments.

Recipe/action bytes remain opaque; no actual action, evidence-class or rights
certification follows. Mutable catalog heads, trusted journals, semantic recipe
and resource/installation locks, coordinator integration and real asset output
remain open. Controlled crash recovery does not qualify power loss, disk-full,
OOM or arbitrary mid-staging kills; leftover foreign staging entries are preserved
and refused. The intentional `_Exit` child bypasses its teardown/leak scan;
ordinary admission, parent and retry paths receive sanitizer checks. No native
Windows/macOS, resource-performance or full AP0 qualification is claimed.
