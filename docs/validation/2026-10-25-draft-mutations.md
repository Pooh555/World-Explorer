# A0 action-bound draft metadata replacement — 25 October 2026

**VERIFIED: local CPU mutation/publication slice; A0/AP0 remain IN_PROGRESS.**
A shared native command binds declared assistance to exact draft revisions and
publishes durable intent/result history. The [protocol](../protocols/draft_mutations.md)
owns fields, bounds and recovery semantics. Replacement bodies remain opaque;
this does not produce/qualify masks, geometry, materials, real assets or live edits.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`8d7dfcc6e6945cfdba4160c56f312f6688c52b1d`.
`R=artifacts/validation/a0-draft-mutations-20261025` (ignored).
`implementation.json` pins ten build/source/fixture inputs, SHA-256
`b39576a85f001a5d145f802a607283bb5c6e0bdbb21354957491b5500469d5dc`.
No dependency boundary, coverage denominator, worker recipe or acceptance
threshold changes. The journal gains an original-source-path accessor for output
protection; its persistent schema is unchanged.

## Exercised behavior

The command checks selected before/after receipts, journal action, kind, complete
target set and context. Every target/transitive descendant requires new bytes,
revision +1 and manual ancestry; target action hashes must match the journal,
dependency IDs/recipes stay fixed and unrelated products stay identical. The
intent binds exact receipt/graph/journal pins and sorted changes. A separate
committed result binds its hash, owned before/after/action bytes and explicit
assisted draft status. This enforces the new command path; legacy metadata APIs
still admit declarations without requiring it. Human activity and actual typed
descendant correctness remain unverified.

Ten new cases exercise all three replacement kinds, exact/later replay, changed
pending operation refusal, stale branches after sequential revisions, bad pins,
foreign history, recipe/target/dependency/subtree substitutions, absent manual
lineage, setup/evaluation exclusion, wrong identities/bases, byte quotas and
source/journal/foreign/held-lock preservation. Available directory-symlink cases
also refuse. Exact 16-product closure, 64 KiB replacement and maximum reserved
operation ID admit; the seventeenth descendant and oversized body refuse.
The 512-mutation ceiling is implemented but not stressed end-to-end.

Cancellation/observer exceptions at intent durability or result-record writing
leave a readable `PREPARED` intent without exposed outputs. Matching retry reaches
`COMMITTED`. Actual child processes exit 77 at both durability points; source
hashes remain unchanged and recovery publishes exact bytes. Technical errors are
returned; distinct terminal failure reasons are not durably journaled. These are
controlled crashes, not arbitrary power-loss/disk-fault qualification.

The independent standard-library producer (`python3 R/oracle.py`, expand R)
builds four-product receipts, one action and a three-product replacement closure.
Native action/intent/result hashes match its independent canonical encodings.
**Two completed replays preserve files, six mutations reject without output,
and two process-crash recoveries match the original result**. After the first
replacement blob is installed, the manifest still exposes only the intent.
Both source receipts and journal bytes remain unchanged (`oracle-results.json`,
`oracle.log`). All payloads/actions are labeled synthetic declarations.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core, **617/617**; `build-complete.log`, `ctest-dev-complete.log` |
| Same with `compat-cxx23` | 0; explicit C++23 CPU, **592/592**; `build-compat-complete.log`, `ctest-compat-complete.log` |
| `build/dev/we_core_tests '[draft-mutations],[manual-actions]'`; same under `build/compat-cxx23/` | 0; **19 cases / 4,423 assertions each**, including ten new cases/2,899 assertions; `focused-final.log`, `focused-compat.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[draft-mutations],[manual-actions]'` | 0; **19/4,423**, ASan/UBSan/default LSan, no reports; `build-asan-complete.log`, `asan-complete.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on nine affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-verified.log` |
| Pinned Black 26.5.1 tracked-file check per AGENTS | 0; `black.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |
| `python3 R/check_preservation.py` (expand R) | 0; **378 protected media/result hashes match**; `preservation.json` |

An early focused invocation saw the old binary before new tests linked
(`focused-first.log`). The first full CPU run overlapped a relink and reported
508 permission-denied/BAD_COMMAND startups (`ctest-dev.log`, exit 8); the stable
final rerun passes all 617. Initial tidy reports found constant naming and
unguarded fixture optionals; checked fixture accessors retain runtime validation
without suppressions (`style-first.log`, `style-corrected.log`).

No media processing, inference, model installation, runtime inventory refresh or
GPU/platform qualification ran. Saved runtime receipts still require recapture
after relinking. Typed mutation validation and enforced inference/asset adoption
remain in [progress](../progress.md); A1 real-product and all full gates stay open.
