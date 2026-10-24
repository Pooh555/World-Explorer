# A0 pose-bound object observations — 24 October 2026

**VERIFIED: local CPU admission slice; A0/AP0 remain IN_PROGRESS.** Native
`map_store` inspects strict observation/mask bytes against independently selected
receipt products and actual decoded pose/calibration bytes. The
[protocol](../protocols/object_observations.md) owns schemas, bounds and scope.
This is not segmentation, verified tracking, a map transaction or asset production.

Cwd: repository root; timezone Asia/Hong_Kong. Parent `1caa3d1`;
`R=artifacts/validation/a0-object-observations-20261024` (ignored).
`implementation.json` pins eight source/build/test/boundary inputs, SHA-256
`534f4644364abd14164893bd57d5f3c38e819b6ebec3f043378201f0e9c0239f`.
The explicit public `map_store` → `asset_catalog` edge exposes project-owned
receipt/reference types; framework-header restrictions and coverage scope remain.

## Exercised behavior

Observation and track IDs remain distinct from asset identity/class labels.
Actual pose decoding checks the selected snapshot epoch/revision, frame image
hash, capture/calibration, per-frame pose revision, tracking and invalidation.
Compressed masks completely partition contained integer-center grids into visible,
occluded, unknown and background pixels. Class/association confidence stays
separate from declared decisions; rejected evidence retains manual ancestry.
Immutable handles own selected metadata, runs and decoded calibration/pose/world
transforms, surviving caller edits, source deletion.

Twelve new cases exercise exact bindings, stale/corrected/lost poses, foreign
calibrations/images, independently selected evidence and parent closure, malformed
rehashed fields/JSON, duplicate keys/NUL/depth, invalid labels/confidence/tracks,
multiple accepted tracks, incomplete/overlapping masks, unknown classes, crops,
half-pixel resize support, excessive grids, declared manual ancestry and
cancellation with source preservation. Exact limits cover 16 classes/associations,
48 evidence decisions, 16,384 runs, 16,777,216 pixels and 4,096 decoded pose frames;
excess rejects. A corrupt pose blob with its own valid receipt hash is rejected
by the actual decoder, rather than accepted as an opaque declaration.

## Retained real-byte binding

The independent Python producer (`python3 R/oracle.py`) creates receipt/selection
fixtures around the unchanged sample_1 pose asset
`poses-d0383cb0510d82c7c92cb8184f1214c688bac11619ab65b232c9ec3d66db3abf.weposes`
and its matching committed pose-decode PNG, SHA-256
`ea68f655eeb03edf5500d3ea404662c9dda8de888b41df02a0db59e7749db0c3`.
The native probe selects frame `238312877451038936`, epoch 0/revision 1,
3840×2160. Full/half-size all-unknown masks and confidence-only hypotheses pass;
eight variants reject stale context, foreign calibration/image, invalid mask/grid,
invented ancestry, duplicate mask keys or corrupt pose bytes. The Python matrix
oracle matches composed world translation within `1e-12`; all original input and
fixture hashes remain unchanged (`oracle-results.json`).

**Masks, hypotheses and decisions are explicitly synthetic.** No segmentation,
measured geometry, object correspondence, independent scale, consent/rights or
AP quality gate is certified by this binding exercise. No image decoding, solve,
fit, bake, model/environment change or inference occurs.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26, **584/584**, 99.42 s; `build-dev-verified.log`, `ctest-dev-verified.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **559/559**, 104.70 s; `build-compat-verified.log`, `ctest-compat-verified.log` |
| `build/dev/we_core_tests '[object-observation],[asset-metadata-io]'` | 0; **23 cases / 3,142 assertions**; `focused-verified.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[object-observation]'` | 0; **12 cases / 1,855 assertions**, ASan/UBSan/default LSan, no reports; `build-asan-verified.log`, `asan-verified.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on six affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-verified.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black-verified.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; existing stale runtime payload after relinking; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Probe compile: `c++ -std=c++2c -Iinclude -isystem build/dev/_deps/json-src/include R/probe.cpp build/dev/libmap_store.a build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`.
Its adapter supplies independent selections; production code reads both actual
observation/mask documents and the selected pose asset.

Initial failures are retained: missing private compose qualification, incorrect
fixture expectations for a half-turn about X, style findings, and a test override
that was not yet used (`build-tests.log`, `focused-first.log`, `style-first.log`,
`focused-dev.log`). One concurrent C++23 run failed a temporary output-stream
assertion (`ctest-compat.log`). Both source-deletion tests still retained their
old path for destructor cleanup, allowing deletion of a subsequently reused root;
they now release that path immediately. Both full profiles and focused sanitizer
checks pass afterward. The probe initially hit an ADL `std::ref` helper collision
and selected the older decode inventory (`probe-build-first.log`, `oracle.log`,
`oracle-final.log`); the fixed helper and matching pose-decode inventory pass
(`oracle-replay.log`). No production warnings or thresholds weaken.

All **378 protected media/result hashes match** (`preservation.json`). Trusted
action journaling, capture/evaluation freezes, full runtime/rights qualification
and A1 real asset/PBR output remain open. Native Windows/macOS and full AP0 are
unclaimed; no new coverage percentage is inferred.
