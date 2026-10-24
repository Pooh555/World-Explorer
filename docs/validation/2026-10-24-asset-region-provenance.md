# A0 region provenance — 24 October 2026

**VERIFIED: synthetic local CPU slice; A0/AP0 remain IN_PROGRESS.** This bounded
native CPU slice admits declared face/texel evidence and graph-bound manual
lineage. It does not qualify real correspondence, measured support, authenticated
human actions, inference or asset output. The [protocol](../protocols/asset_region_provenance.md)
owns fields, limits, trust and failure semantics.

Cwd: repository root; timezone Asia/Hong_Kong. Parent `5614cd4`;
`R=artifacts/validation/a0-region-provenance-20261024` (ignored).
`implementation.json` pins five source/build inputs; SHA-256
`85e3dd882cbf246a9f6dbad99ebf93b38eda7d10aab063b2485599f70b5648f9`.

## Exercised behavior

A separately selected sidecar product depends only on its target. Its immutable
bytes bind exact context, target revision/content/recipe, independently selected
domain and source metadata. Complete run partitions preserve UNKNOWN, OBSERVED,
INFERRED, GENERATED and MANUALLY_EDITED classes separately from confidence.
Source views retain capture/frame/calibration, image and pose/grid identities;
shared image bytes never merge distinct view IDs. Declared local actions and
parent/support ancestry must agree with span and graph flags. Generated/inferred
outputs retain inherited manual ancestry without changing class.

Twelve new cases cover face/row-major-texel partitions; immutable snapshots after
source removal; order-independent source selection; gaps/overlap/tail omissions;
zero/fractional/overflow counts; confidence/support constraints; stale context,
recipe/target/source/grid/calibration; duplicate/unused/unrelated references;
manual-lineage clearing and unsupported edits; malformed/duplicate-key/NUL/deep
JSON; cancellation and caller preservation. Exact/excess limits cover 4 MiB
records, 16,777,216 elements, 16,384 spans, 64 views and 65,536 combined references.
Caller metadata digests are validated before copying the selected table.

The independent standard-library producer (`python3 R/oracle.py`) builds ten
fixtures using the prior ignored component oracle's byte-receipt encoding helpers.
Its native public-API probe accepts face/texel, local-manual and inherited-manual
records; it rejects missing support, a gap, stale source metadata, cleared ancestry,
unknown confidence and wrong domain. All fixture files remain unchanged. The face
fixture selects provenance `5535dfbac4179edddf293face8825c879beba8443efcbd6be86afe9b48f0ed96`,
receipt `4876487fcb31889682eca31b48ca37a8d0d20a68a32658a9440f3edc79ba6d62`,
independent selection `ec38444525b82f8fd755ff308bd6ea74c98163f40dc20e418ba7b49cd1a0e038`;
coverage `[2,2,2,2,0]` and zero manual elements. No real geometry is produced.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | 0; GNU 16.2.1 C++26, **550/550**; `build-dev-current.log`, `ctest-dev-current.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **525/525**; `build-compat-current.log`, `ctest-compat-current.log` |
| `build/dev/we_core_tests '[asset-region],[asset-installed],[asset-component-evidence],[asset-recipe],[asset-receipt],[asset-dependencies]'` | 0; **58 cases / 16,327 assertions**; `focused-dev-current.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[asset-region]'` | 0; **12 cases / 933 assertions**, no ASan/UBSan/LSan reports; default leak detection retained; `build-asan-current.log`, `asan-current.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on the four new C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-final.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; unchanged existing stale runtime payload after preceding relinks; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Final native oracle replay (`oracle-current-results.json`) matches all ten outcomes.
All **378 protected media/result hashes match** (`preservation.json`).

Probe compilation: `c++ -std=c++2c -Iinclude -isystem build/dev/_deps/json-src/include R/probe.cpp build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`.

The first build finds a missing fixture `<set>` include; the first tidy run finds
59 unbraced test branches. The include and braces are fixed without suppressing
warnings (`build-dev-first.log`, `style-first.log`, `braces-fix.log`). Review then
moves caller digest checks ahead of table copying and adds oversized/uppercase
caller cases. The earlier full sanitizer run passes 58 cases / 16,307 assertions
(`asan-before-review.log`, `implementation-before-review.json`); final provenance
sanitizer coverage is recorded separately. A quality run during documentation
editing sees links to this then-unwritten report (`style-docs-incomplete.log`); the full gate passes after the report is created.

No dependencies, environments, model downloads or saved reconstruction runs change.
Source metadata/correspondence and actual manual operations remain declarations;
no AP0, Q5/N2, measured walking or new coverage percentage is claimed. Asset and
instance schemas, owned action journaling and capture/evaluation freezes remain
A0 work. Runtime receipt freshness, rights and real native PBR output stay open.
