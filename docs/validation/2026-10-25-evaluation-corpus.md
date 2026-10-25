# A0 sealed evaluation metadata — 25 October 2026

**VERIFIED: local CPU contract slice; A0/AP0 remain IN_PROGRESS.** Bounded corpus metadata
now freezes exact committed capture selections, recipe/resource/rights pins and
optional evaluation evidence refs. Missing and opaque prerequisites remain
explicit blockers. The [protocol](../protocols/evaluation_corpus.md) owns the
contract. Synthetic fixtures cannot qualify real evidence, execution or AP gates.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`f560ffc92d18d12a04b87286d91fe9a4dcee400f`.
`R=artifacts/validation/a0-evaluation-corpus-20261025` (ignored).
`implementation.json` pins the five build/source/test inputs, SHA-256
`88c303e540048379d54b8f7713214e96422ae927a0007badff9b7d7fa30f07ee`.
The full suites used `implementation-before-style.json` (SHA-256
`a7c33f143df598a88c23e5c58f441f6658c91482534abb6860f503cf873819a0`); later changes only add
direct standard headers and replace dependent template structured bindings with
pair-member access to satisfy naming analysis. Final builds/focused checks cover
those sources. No dependency, standard, benchmark scope or threshold changed.

## Exercised behavior

Independent selection and current registry pins prevent a serialized manifest
from choosing its own inputs. Decode rechecks actual admitted sources and recipes,
canonical bytes and selected identity. Publication uses one generation-one seal;
changed requests cannot append or overwrite it. Existing owned manifest-last
storage keeps pending bytes invisible until exact replay commits. Copies retain
metadata after source deletion; a fresh read requires current selected registries.

Tests exercise missing/opaque evidence, exact measurement selection, receipt/
semantic/context/commit substitutions, malformed/rehashed/noncanonical documents,
bounded tables, source deduplication, deterministic recipe ordering, stale registry
rejection, cancellation/replay and protected-tree overlap. Existing immutable
registry fixtures cover the reused storage primitive; this slice adds no separate
native crash probe or power-loss qualification.

The first focused run failed two checks (`focused-first.log`, exit 42): runtime
closure was absent from corpus blockers, and a cancellation test expected a
callback count zero although the registry notifies after installing each blob.
Corpus diagnostics now explicitly retain unverified reviews/installation/runtime/
worker limits and capture-to-recipe input binding. The test cancels at the actual
first-blob boundary. No warnings, storage contract or acceptance gate was weakened.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 native viewer/CLI/core; **652/652**, 235.51 s; `build-dev-final.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 --parallel 4`; `ctest --preset compat-cxx23 -j 4` | 0; explicit GNU C++23 CPU profile; **627/627**, 236.12 s; `build-compat-final.log`, `ctest-compat.log` |
| `build/dev/we_core_tests '[asset-corpus],[capture-selection],[asset-recipe]'`; same under `build/compat-cxx23/` | 0; **33 cases / 6,350 assertions each**; `focused-dev.log`, `focused-compat.log` |
| Repeat dev/compat build commands above, then the same focused commands on final stable sources | 0; **33/6,350 each**; `build-dev-stable.log`, `build-compat-stable.log`, `focused-dev-stable.log`, `focused-compat-stable.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[asset-corpus],[capture-selection],[asset-recipe]'` | 0; **33/6,350**, GNU C++26 ASan/UBSan/default LSan, no reports; `build-asan-stable.log`, `asan-stable.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py include/world_explorer/application/evaluation_corpus.hpp src/application/evaluation_corpus.cpp tests/fixtures/evaluation_corpus_fixture.hpp tests/unit/evaluation_corpus_tests.cpp` | 0; format/tidy, include boundaries, docs/whitespace; `style-stable.log` |
| `python3 R/libcxx_probe.py libcxx-stable` (expand R) | 0; actual test translation unit, Clang 23.1.3 with retained libc++23.1.1 headers, explicit C++23 syntax check; `libcxx-stable-argv.json`, `libcxx-stable.log`, `libcxx-stable-summary.log` |
| `python3 R/check_preservation.py` | 0; **378 protected media/product hashes unchanged**; `preservation.json`, `preservation.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

The initial style run rejects four dependent structured-binding names
(`style-first.log`). Renaming them alone produces conflicting naming diagnostics
between template declarations/instantiations; the intermediate run also raced a
header rewrite and reported stale parse locations (`style-final.log`). Plain
pair-member access removes the dependent declarations. Stable final analysis
runs against unchanged files; no warning suppression or policy changes.
The earlier full-local-libc++ Jolt dependency blocker and native macOS/Windows
reruns remain pending; syntax admission is not a native platform build pass.

## Available real inputs

A read-only inventory of `data/local/` and existing sample source/request/result
metadata found the three retained videos (`pilot-input-inventory.json` pins all nine selected metadata
files and records each saved success/failure outcome). Their source receipts contain only
schema/video/hash fields, with no consent, measurement, empty-background or
changed-light bindings. This is an inventory of these selected locations, not
proof that the user has no other captures. Known arbitrary-scale sample outputs
cannot supply A1's independently scaled, consented three-prop pilot. No real
corpus seal, inference, provisioning or media upload was performed. Protected
media and saved products are checked separately in `preservation.json`.

Full evaluation-body admission, independent measurement/correspondence, authenticated
rights, candidate component reviews/runtime closure and worker enforcement remain
open. Current host stays contracts-only; A1–A6 and M4 remain NOT_STARTED. No native
platform qualification, GPU/render/export work or runtime-inventory refresh ran.
