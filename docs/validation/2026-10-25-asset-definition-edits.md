# A0 action-bound asset-definition edits — 25 October 2026

**VERIFIED: local CPU typed-edit slice; A0/AP0 remain IN_PROGRESS.**
Both edit kinds now validate actual bounded generated triangle-mesh and constant
PBR parameter bytes before any attempt publication. Committed typed reads own
the selected definition and payloads. The [protocol](../protocols/asset_definition_edits.md)
owns the restricted profile and limits. All new fixtures are synthetic; no real
asset generation, PBR rendering/relighting, native visual result or AP gate passes.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`3631d13ffcf15458dca118f62cbb7200adca859b`.
`R=artifacts/validation/a0-asset-edits-20261025` (ignored).
`implementation.json` pins eleven build/source/test inputs, SHA-256
`f9a547e819dca8b7f3ce1c471e408b5a0d1223cf495dddb5b3caaa1fdb486777`. No dependencies, worker
environments, standards, benchmark scope or acceptance thresholds changed.

## Exercised behavior

Definitions include their receipt hash, making storage within the same receipt a
hash cycle. Explicit intent v3 binds attached definition keys/sizes/hashes and
publishes them with product/action/result bytes in one atomic result phase.
Default requests retain byte-identical v1/v2 behavior. Result/core schemas stay
unchanged. The adapter independently binds both definition hashes, checks native
regions and actual payloads, limits edits to their declared kind and preserves
stable source/rights/pivot identity. Edited roots label every element manually;
derived products retain inherited ancestry. Directory-local definition heads
reject semantic revision branching before writing another attempt.

Eight new cases exercise both kinds, mesh indices/normal/winding/UV errors,
channel bounds, hidden profile switches, variant/definition/provenance
selections, cross-field changes and semantic no-op rejection, owned typed output after source removal and
semantic-head rejection followed by valid successive edits. Attachment quotas
admit four exact 64-KiB records and reject count/size/key/duplicate/binding errors
before output; changed immutable bytes cannot replay. Payload vertex/face ceilings
are implemented; exact 512-element geometry limits are not stressed end-to-end.

Cancellation at intent, first product and first attached definition leaves only
prepared logical history. A new attempt recovers the same revision without
changing the old cancellation. A native child exits 77 after the first definition
blob installs; typed reads still cannot admit it until recovery commits the result.
Old pins require resync. These controlled crashes do not qualify arbitrary power
loss, filesystem races or disk-full/OOM recovery.

The independent standard-library oracle (`python3 R/oracle.py`, expand R) matches
v3 intent/result/attached bytes: **two unchanged replays, four refused mutations
and three native crash recoveries**, including attachment installation before
manifest commit. Earlier v1 mutation and attempt encodings also match, with their
original replay/refusal/crash checks. Source hashes stay unchanged
(`v3-oracle-results.json`, `oracle-results.json`, `attempt-oracle-results.json`).

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core, **643/643**; `build-semantic.log`, `ctest-dev-semantic.log` |
| Same with `compat-cxx23` (`--parallel 4` build) | 0; explicit GNU C++23 CPU, **618/618**; `build-compat-semantic.log`, `ctest-compat-semantic.log` |
| `build/dev/we_core_tests '[asset-edit],[draft-mutations],[draft-attempts],[object-mask-edit]'`; same under `build/compat-cxx23/` | 0; **36 cases / 11,280 assertions each**; `focused-semantic.log`, `focused-compat-semantic.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native same filter under `build/asan/` | 0; **36/11,280**, ASan/UBSan/default LSan, no reports; `build-asan-semantic.log`, `asan-semantic.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on ten affected C++ paths, then the expanded test | 0; format/tidy/boundaries/docs/whitespace; `style-final.log`, `style-semantic.log` |
| `python3 R/libcxx_probe.py libcxx-semantic` | 0; actual new test translation unit, Clang 23.1.3 / retained libc++23.1.1 C++23; `libcxx-semantic-argv.json`, `libcxx-semantic.log`, `libcxx-semantic-summary.log` |
| Pinned Black 26.5.1 tracked-file check per AGENTS | 0; `black.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |
| `python3 R/oracle.py`; `python3 R/check_preservation.py` | 0; oracle above; **378 protected hashes match**; `oracle.log`, `preservation.json` |

Before the final semantic-rejection test addition, complete suites passed
642 C++26 / 617 C++23 cases and 35 cases / 11,064 sanitizer assertions; those logs
remain retained. Final expanded runs supersede the test identity.

Initial fixtures reused an 8-texel domain for a one-texel constant material and
failed ordinary native selection admission; fixture-owned selections were fixed
(`focused-first.log`). The style gate found a missing direct `<span>` include and
unnecessary moves of immutable handles that support copying; explicit includes/
copies fix these without warning suppression (`style-first.log`, `style-corrected.log`).

No media/model processing, provisioning, runtime inventory refresh, GPU or native
platform qualification ran. The earlier local full-libc++ Jolt blocker and native
macOS rerun remain pending. Legacy opaque publication still requires separate
typed admission; other descendants, observed correspondence, complete payload
profiles, worker/scene/export adoption and real A1 output remain open.
