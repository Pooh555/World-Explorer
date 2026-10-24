# A0 typed asset/instance schemas — 24 October 2026

**VERIFIED: synthetic local CPU slice; A0/AP0 remain IN_PROGRESS.** The native
slice admits immutable reusable definition metadata and exact instance references.
It does not produce assets, decode/publish metadata files, maintain committed
revision history or update live scenes. The [protocol](../protocols/asset_definitions.md)
owns fields, limits, encoding and qualification boundaries.

Cwd: repository root; timezone Asia/Hong_Kong. Parent `53a7cd7`;
`R=artifacts/validation/a0-asset-schema-20261024` (ignored).
`implementation.json` pins nine source/build/boundary inputs, SHA-256
`3bde1a730213b5037846e0fa63a76939a2d04db2f8cce387311e579f7f258657`.

## Exercised behavior

Semantic asset, instance and material-variant IDs remain distinct from product IDs.
Definitions bind sources, geometry/materials/LODs/collider, declared units/bounds/
pivot, rights and independently selected region provenance. Instance sets reuse
exact immutable handles from the same input receipt, with permitted variants,
finite transforms/corners, one explicit unit frame and declared lineage. Identity
never merges solely because geometry bytes match. Captured/estimated labels
cannot hide generated texels. No declarations establish measured scale, legal
rights, PBR quality or measured collision/walking.

Eleven new cases cover immutable replay/order independence, two placements of one
definition, changed placement without definition changes, stale replacement
revision/hash, invalid identities/roles/units/bounds/pivots/quaternions/scales,
overflow, missing/foreign provenance and source metadata, lighting/evidence
consistency, distinct shared-byte assets, declared metric/manual pivots, manual
lineage/action clearing, cancellation, snapshot identity, empty/unused sets and
caller-file preservation. Exact limits exercise 64 definitions, 4,096 instances,
16 lineage entries per instance and 16,384 combined entries. LOD/collider/material
variants each require their own correctly targeted region sidecars.

The independent standard-library producer (`python3 R/oracle.py`) builds eight
synthetic fixtures from the prior region oracle's receipt format. A native
public-API probe exactly matches Python's compact sorted-key definition/instance
bytes and SHA-256 for reuse, movement, declared metric units and a pivot override;
it rejects wrong variants, stale source metadata, hidden generated texels and
negative scale. Input files remain unchanged. Emitted `definition.json` and
`instances.json` are metadata diagnostics, not actual game assets or product
exporter output. `oracle-results.json` pins all input/output identities and argv.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | 0; GNU 16.2.1 C++26, **561/561**; `build-current.log`, `ctest-dev.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **536/536**; `build-compat.log`, `ctest-compat.log` |
| `build/dev/we_core_tests '[asset-schema]'` | 0; **11 cases / 990 assertions**; `focused-dev.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[asset-schema]'` | 0; **11 cases / 990 assertions**, no ASan/UBSan/LSan reports; default leak detection retained; `build-asan.log`, `asan.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on the seven affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-final.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; existing stale runtime payload remains after relinking; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Probe compilation: `c++ -std=c++2c -Iinclude -isystem build/dev/_deps/json-src/include R/probe.cpp build/dev/libscene_store.a build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`.
It constructs typed inputs for the public APIs; its test-only JSON adapter is not
a production decoder. The boundary change allows exactly `scene_store` →
`asset_catalog`, with public CMake linkage for immutable definition ownership.
It does not relax external/framework-header checks or create a dependency cycle.

Initial tidy reports ten optional-access findings (`style-first.log`). Explicit
presence branches/local values and an optional-reference binding helper resolve
them without suppression; the fixed full gate passes. Two earlier focused runs
precede the final added cases (`focused-first.log`, `focused-new.log`); final
11-case evidence is separate. No warnings or acceptance thresholds weaken.

All **378 protected media/result hashes match** (`preservation.json`). No decode/solve/fit/bake/map reruns, worker/environment changes or model
provisioning occur. Actual payload schemas, correspondence, trusted actions,
rights/measurement validation, persistent revision collision policy and ordinary
scene publication remain open. A1 real native output, AP0 and Windows/macOS
qualification are unclaimed; no new coverage percentage is inferred.
