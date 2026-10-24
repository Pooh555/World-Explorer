# A0 capture/evaluation selections — 24 October 2026

**VERIFIED: local CPU selection slice; A0/AP0 remain IN_PROGRESS.** Strict native
role manifests bind selected receipt/view/annotation identities, create separate
inference inputs and publish immutable metadata. The
[protocol](../protocols/capture_selections.md) owns fields, bounds and limits.
This does not seal a real corpus, authenticate consent/scale, discover undeclared
fit history, integrate a worker or produce assets.

Cwd: repository root; timezone Asia/Hong_Kong. Parent `cf45cfb`;
`R=artifacts/validation/a0-capture-selections-20261024` (ignored).
`implementation.json` pins six source/build/test inputs, SHA-256
`dfb2c1688e0632f7ec608ee5f070a26f1dbf1a84a23bfa54a9a502eb71545ad5`.
No dependency boundary, coverage denominator or acceptance threshold changes.

## Exercised behavior

The coordinator handle retains normalized source/role/annotation selections and
evaluation data. Its distinct inference handle owns only selected encoded images,
observation/calibration/pose/world values, expected epoch/map revision, ARBITRARY
scale and input manual ancestry. It retains neither the original receipt nor full
pose assets/evaluation labels. Copies survive original handle/source destruction.
Known evaluation hashes and annotation dependencies cannot enter inference;
every observation in an inference pose asset must have an explicit inference role.
Direction is preserved: inference source data may feed evaluation. Manual
evaluation annotation ancestry does not become inference input ancestry.

Thirteen new cases cover independent role/source selection, malformed/rehashed
fields/JSON, duplicate keys/NUL/depth, unknown enums, role and byte aliases,
annotations on inference, indirect/aliased label dependencies, shared label
recipes, hidden/undeclared pose observations, stale/lost/foreign views, missing
annotation handles, cancellation and source/output preservation. Exact limits
admit 64 frames across 32 sources and 32 MiB retained inference images; excess
frame/source/annotation/byte tables refuse. Publication tests exact replay/reopen,
semantic revision conflicts and source-tree overlap through the existing registry.

The independent standard-library producer (`python3 R/oracle.py`) builds synthetic
receipt/selection documents around native encoded pose fixtures. **Five cases
admit, publish and reopen; nine reject.** Evaluation label changes, evaluation-only
role selection, absent annotations and table permutations leave inference payloads
identical. Native results include explicit context/scale. Direct/aliased label
links, role substitution, overlapping image bytes, foreign sources, hidden pose
observations and NUL reject (`oracle-results.json`, `oracle-final.log`).

A separate refusal uses unchanged sample_1 pose asset
`d0383cb0510d82c7c92cb8184f1214c688bac11619ab65b232c9ec3d66db3abf`
and its matching decoded PNG
`ea68f655eeb03edf5500d3ea404662c9dda8de888b41df02a0db59e7749db0c3`.
Selecting just one inference frame fails because the actual pose snapshot contains
additional undeclared fit inputs. No artificial subset of that solve is passed
as an independent evaluation solve. Synthetic positives and this real-data
refusal certify only the documented admission scope. All original oracle inputs
and real source hashes remain unchanged.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26, **597/597**; `build-dev-complete.log`, `ctest-dev-complete.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **572/572**; `build-compat-complete.log`, `ctest-compat-complete.log` |
| `build/dev/we_core_tests '[capture-selection]'` | 0; **13 cases / 2,929 assertions**; `focused-complete.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[capture-selection]'` | 0; **13 cases / 2,929 assertions**, ASan/UBSan/default LSan, no reports; `build-asan-complete.log`, `asan-complete.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on five affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-verified.log` |
| Pinned Black 26.5.1 tracked-Python check | 0; command below; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; existing stale runtime payload after relinking; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Black command: `git ls-files -z -- '*.py' | env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1`.
Probe compile: `c++ -std=c++2c -Iinclude -isystem build/dev/_deps/json-src/include R/probe.cpp build/dev/libmap_store.a build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`.
Native `probe emit R/probe-inputs` creates only synthetic pose fixtures; the Python
producer supplies separate independent selections and manifests.

Initial tidy requests vector reservation (`style-first.log`). Expanded fixtures
first failed declared ancestry before their staged graph was complete
(`focused-dev.log`); final fixture ordering fixes it. Final metadata assertions
caught a missing public scale member (`build-dev-final.log`, `style-complete.log`);
the corrected header and copied context/scale pass final checks. Earlier passes
(10 cases/2,606 assertions and 13/2,926) remain separate from final 13/2,929 evidence.
No production warnings, sanitizers or thresholds are weakened.

All **378 protected media/result hashes match** (`preservation.json`). No saved
reconstruction/fit/bake, model installation, runtime observation or GPU/platform
qualification is rerun. Trusted manual-action journaling, full capture/evaluation
freeze, actual rights/independent measurement and A1 real asset/PBR output remain
open. Unknown model/environment costs still block provisioning on this contracts-only
host. AP0 completion and native Windows/macOS passes remain unclaimed.
