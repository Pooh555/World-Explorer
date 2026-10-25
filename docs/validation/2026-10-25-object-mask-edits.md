# A0 typed object-mask edits — 25 October 2026

**VERIFIED: local CPU typed-edit slice; A0/AP0 remain IN_PROGRESS.**
The shared native command admits both observation/mask versions before durable
publication. Committed typed reads expose owned inference-mask inputs with manual
ancestry. The [protocol](../protocols/object_mask_edits.md) owns contracts and
limits. Fixtures are synthetic; no real segmentation, inference, editable asset,
native visual product or AP gate is claimed.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`e0d3c9c893e8970bca6fb7fe0bf7effa5e519c8f`.
`R=artifacts/validation/a0-object-mask-edit-20261025` (ignored).
`implementation.json` pins eleven build/source/test inputs, SHA-256
`dc4726beefe2d2a6fa6bc93f55108eee37b5a37b811b968a932f17ad094b7b95`. No dependency,
threshold, benchmark scope or worker-environment change.

## Exercised behavior

Eight new mask cases cover ordinary replacement, contained grid change,
malformed/overlapping/empty/unsupported runs, incorrect grids/lineage/confidence,
semantic changes, stale observation/pose/mask selections, wrong target/kind and
foreign receipts. Independently admitted capture roles exclude both held-out and
evaluation-only frames. Upstream traversal rejects evaluation content, recipe and
action aliases and indirect evidence paths before any intent. Failed typed
admission leaves the output directory absent.

Only exact committed results create an immutable typed handle. It owns mask
coverage and selected native image/camera values after source/output removal,
without evaluation payloads, observation label bodies or complete receipts/poses.
Source/journal hashes remain unchanged after publication/replay. Cancellation at
intent durability or first result-record writing leaves a prepared intent;
typed reads return `WOULD_BLOCK`, exact retry succeeds and old pins require resync.
Generic publication cannot bypass malformed-body admission.

Review identified that the original generic intent did not bind the typed role
selection. An explicit v2 intent adds an adapter-derived admission hash covering
the capture and both observation product references. A second admitted capture
product aliasing identical selection bytes cannot substitute its identity on
read/retry; removing the binding cannot downgrade the operation. One additional
generic case checks v2 identity, replay/read refusal after binding removal and
invalid-hash refusal before output. Absent bindings retain byte-identical v1;
result and core-registry schemas stay unchanged.

The independently encoded prior v1 oracle was copied into R with a fresh output
root. Native action/intent/result hashes still match: two completed replays leave
files unchanged, six bad requests refuse, and two controlled native child crashes
recover exact results. This verifies v1 compatibility, not independent v2 encoding
or arbitrary power-loss qualification (`oracle.py`, `oracle-results.json`).

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core, **626/626**; `build-dev-bound.log`, `ctest-dev-bound.log` |
| Same with `compat-cxx23` (`--parallel 4` build) | 0; explicit C++23 CPU, **601/601**; `build-compat-bound.log`, `ctest-compat-bound.log` |
| `build/dev/we_core_tests '[object-mask-edit],[capture-selection],[object-observation],[draft-mutations]'`; same under `build/compat-cxx23/` | 0; **44 cases / 12,744 assertions each**; `focused-bound.log`, `focused-compat-bound.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[object-mask-edit],[draft-mutations]'` | 0; **19 cases / 7,960 assertions**, ASan/UBSan/default LSan, no reports; `build-asan-bound.log`, `asan-bound.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on ten affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-complete.log` |
| Pinned Black 26.5.1 tracked-file check per AGENTS | 0; `black.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |
| `python3 R/oracle.py`; `python3 R/check_preservation.py` (expand R) | 0; compatibility oracle above; **378 protected media/result hashes match**; `oracle.log`, `preservation.json` |

The first focused run failed one downsampled-grid fixture: scale 0.5 with zero
offset crossed the negative half-pixel boundary. Correct integer-center offset
−0.25 admits (`focused-first.log`). Initial tidy found a missing synthetic switch
default and Catch2's negated-macro enum combination; an explicit default and
ordinary negated expressions preserve assertions without suppressions
(`style-first.log`). A later style run passed C++ analysis but found links to
the not-yet-created report; final documentation checks pass after its creation
(`style-bound.log`). Pre-binding development/C++23 suites passed 624/599 and native
ASan/UBSan/default LSan passed 42 cases/12,575 assertions; final checks supersede
those source identities. All earlier logs remain retained.

No media processing, model installation, inference, runtime inventory refresh,
GPU or platform qualification ran. Other descendants remain opaque; terminal
failure journaling, enforced worker/asset adoption, authentic actions/rights,
complete capture freezes and real A1 product validation remain open. Saved runtime
receipts still require recapture after relinking. See [progress](../progress.md).
