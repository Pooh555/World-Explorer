# A0 canonical metadata I/O and owned history — 24 October 2026

**VERIFIED: synthetic local CPU slice; A0/AP0 remain IN_PROGRESS.** Strict native
decoders and bounded owned publication extend typed definitions/instances.
The [protocol](../protocols/asset_metadata_io.md) owns schemas, budgets, history
and recovery scope. No actual mesh/material validation, real asset production,
live scene adoption, rights or measured-scale qualification is implied.

Cwd: repository root; timezone Asia/Hong_Kong. Parent `a0382f7`;
`R=artifacts/validation/a0-metadata-io-20261024` (ignored).
`implementation.json` pins 13 source/build/test inputs, SHA-256
`dfaca08b4a8742a3cd1e1ad79c4f4cd5c767ff51e53d8d7747048644754e3dd1`.

## Exercised behavior

Decoders bind exact independently selected hashes, semantic IDs/revisions,
source receipt, provenance and context; reconstruct typed inputs; then require
byte equality with native canonical encoding. The owned registry durably writes
operation intents before immutable blobs and atomically replaces its manifest
last. Matching completed retries return the current store observation without
writes; changed operation intent, committed semantic revisions and stale bases
are refused. Instances and all selected definitions publish in one commit.
Core stores opaque bytes; domain readers remain necessary for schema trust.

Eleven new cases exercise canonical round trips, independent source/provenance
selection, malformed/rehashed/foreign records, generated-material labeling,
invalid transforms/variants/lineage, array/byte bounds, declared metric/manual
pivot preservation, cancellation, immutable revision conflicts, same-operation
collisions before/after completion, retry after later commits, writer contention,
foreign/corrupt/hard-linked output preservation and a controlled child exit.
Exact 4 MiB record and 8 MiB batch limits pass; excess refuses before mutation.
Retained private receipt bytes allow decoding after the source directory is
removed in an owned temporary fixture. No user source is removed.

A native probe reads the prior slice's independently generated canonical metadata
and original receipts. Four positive cases (reuse, movement, declared units,
manual pivot) decode, publish and reopen with exact bytes. Four negative cases
(wrong variant, stale source selection, hidden generated texels, negative scale)
reject. All original input hashes remain unchanged. Negative documents are
separate diagnostic copies under R; `oracle-results.json` retains argv, hashes
and outputs. These are synthetic metadata, not reconstructed game assets.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26, **572/572**, 214.20 s; `build-dev-verified.log`, `ctest-dev.log` |
| Same with `compat-cxx23` | 0; explicit C++23, **547/547**, 189.60 s; `build-compat-verified.log`, `ctest-compat.log` |
| Rebuild both profiles; `build/PROFILE/we_core_tests '[asset-metadata-io]'` | 0; **11 cases / 1,287 assertions** each; `build-{dev,compat}-hash-order.log`, `focused-{dev,compat}-hash-order.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[asset-metadata-io]'` | 0; **11 cases / 1,287 assertions**, ASan/UBSan/default LSan; `build-asan-hash-order.log`, `asan-hash-order.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on 12 affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-complete.log` |
| `git ls-files -z -- '*.py' \| env PYTHONPATH="$PWD/artifacts/tools/python-style-26.5.1" xargs -0 -n1 python3 -m black --check --quiet --workers 1` | 0; pinned 26.5.1; `black.log` |
| `python3 tools/check_dependency_manifest.py`; same with `--local-evidence` | **0 / 2**; saved runtime payload remains stale after relinking; `inventory.log`, `inventory-local.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

Final review moves hash/result allocation before manifest replacement, avoiding
allocation after a successful commit. Full suites precede that ordering-only
change (`implementation-before-hash-order.json`, SHA-256
`5ae64ed82def887123a9d529ec9c1f76a021753e95771312e960b3714cd10847`);
focused C++26/C++23 and sanitizer checks are repeated afterward. No warnings,
sanitizers, thresholds or scope are weakened.

Probe compilation: `c++ -std=c++2c -Iinclude -isystem build/dev/_deps/json-src/include R/probe.cpp build/dev/libscene_store.a build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe`; execution: `python3 R/run_oracle.py`.
Its adapter supplies independent provenance selections; production readers decode
the actual definition/instance files.

Initial build/style evidence retains helper-name/JSON-array-sort errors, naming
and static-initialization findings, a misplaced test default label, and unfinished
document links (`build-dev.log`, `build-dev-final.log`, `style-first.log`,
`style-final.log`, `style-verified.log`). Corrected checks are separate. Two oracle
harness attempts used an incorrect fixture name and assumed negative files were
saved; `oracle-first.log`/`oracle-second.log` retain those failures. Neither changed
prior inputs; the final harness uses existing positive files and new negative copies.

All **378 protected media/result hashes match** (`preservation.json`). No saved
reconstruction, fit, bake, worker environment, model installation or runtime/GPU
qualification is rerun. Arbitrary power loss, disk-full/OOM, hostile concurrent
filesystem mutation and trusted action journaling remain unverified. AP0 and
A1's real native asset/PBR output remain open; Windows/macOS passes are unclaimed.
