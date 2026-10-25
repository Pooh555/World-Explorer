# Draft mutation attempt journal v1

[`draft_attempts.hpp`](../../include/world_explorer/asset_catalog/draft_attempts.hpp)
adds explicit historical execution outcomes around the existing
[draft publisher](draft_mutations.md). The shared application command is
`attempt_draft_metadata`; `attempt_object_mask_edit` performs native typed checks
and derives its selection binding before invoking the same journal path.
A0/AP0 remain **IN_PROGRESS**. This is a local metadata execution contract, not
an asset-worker completion or AP9 product classification.

## Admission and identity

Callers select the original mutation/receipts/action journal/context/draft directory
plus a separate `DraftMetadataAttempt`: positive attempt ID ≤2^63−1, even journal
base 0–1,022 and explicit output directory. Read-only mutation admission computes
the canonical mutation intent/result hashes before any attempt write. Typed mask
admission additionally validates both masks and capture-role closures first.
Invalid admission and a pre-cancelled call create no attempt directory.

Attempt and draft roots must be disjoint in both directions. Attempt publication
protects both receipt roots, the manual journal and draft directory through the
existing core registry. All source bytes and previous draft commits remain
immutable. Draft formats v1/v2, result v1 and core registry formats are unchanged.
Direct mutation calls remain available without an attempt journal.

The attempt intent is compact sorted-key UTF-8 JSON, ≤8 KiB, with exactly:
`schema_version: 1`, `attempt_id`, `base_generation`, `action_id`,
`mutation_operation_id`, canonical absolute `draft_directory` (≤4,096 bytes),
`mutation_intent_sha256` and `mutation_result_sha256`. The mutation intent hash
already binds context, source/graph/journal pins, selected revisions and any typed
adapter admission hash. The result hash is the expected immutable draft result,
not proof that execution succeeded.

## Durable states and retries

Each dedicated journal serializes at most 512 attempts, subject to core quotas.
Attempt `n` reserves core operation IDs `2*n-1` and `2*n`:

| Publication | Record / generation | Meaning |
|---|---|---|
| Start | `attempt-<id>` / base +1 | `PENDING`; execution may be absent, interrupted or already finished without a terminal journal manifest |
| Outcome | `outcome-<id>` / base +2 | Immutable `COMMITTED`, `FAILED` or `CANCELLED` attempt outcome |

Outcome JSON is ≤2 KiB, with exactly `schema_version: 1`, `attempt_id`,
`attempt_intent_sha256`, `status`, `error_code` and `mutation_result_sha256`.
Successful publication records `COMMITTED`, null error and the expected result
hash. Cancellation records `CANCELLED`/`CANCELLED` and null result. Other returned
mutation errors and execution-observer exceptions record `FAILED`, the stable
`ErrorCode` spelling and null result. All fourteen current error tokens are
explicitly enumerated. Arbitrary exception text, actor identity and timing are
not stored; bounded local diagnostics remain separate.

A returned `Result` value can contain `FAILED` or `CANCELLED`: callers must inspect
`m_phase`. An outer error means terminal outcome durability is not assured. Once
work has started, cancellation finalization uses a fresh uncancelled token for
one bounded metadata commit. A full/locked/unwritable journal can still prevent
finalization; committed history then remains pending or must be reopened after
an uncertain manifest flush. Failure of journaling never reports successful work.
`FAILED` records the returned execution error; a `RESYNC_REQUIRED` error can follow
an uncertain draft manifest flush, so it does not prove that the draft never
committed. Reopen and independently admit the draft before using any retained output.

An exact terminal replay returns the original outcome at the current journal
commit without mutation execution or observers. Failed/cancelled outcomes never
become successful. A new attempt ID and next even base can resume the same draft
mutation/operation and prepared outputs. Pending attempts prevent interleaving.
New identities do not authorize changes to a previous mutation's receipt/intent.

## Interruption recovery and reading

After start publication, recovery first checks whether the outcome's core
operation intent is already durable. Its exact key/base/byte-count/hash must match
one of the fifteen canonical outcomes (success or fourteen errors). This finite
vocabulary reconstructs the chosen bytes even if their blob was never installed.
Recovery publishes that same decision before any mutation rerun. Unknown physical
decisions are rejected without repair. If no outcome decision exists, the
idempotent draft publisher resumes; pending execution/observer callbacks may repeat.

Core `inspect_immutable_record_operation` is a bounded, read-only observation of
owned physical bindings and whether the selected operation is committed. It uses
the existing complete ownership/schema/hash/inventory checks and exposes no
pending record bytes as committed data. Its observation can become stale.
Neither it nor attempt history authenticates the process/person that authored
metadata; callers serialize owned work, and hostile filesystem replacement or
arbitrary power loss is outside this controlled recovery evidence.

`read_draft_metadata_attempt` requires original selections and an independently
selected current attempt-journal commit. `read_object_mask_attempt` also repeats
typed mask/role admission. Readers validate the entire bounded serial history,
canonical bytes, keys, identity and status/error/result consistency. Old commit
pins require resync; historical failed outcomes remain readable with a new pin.
Only committed attempts expose a mutation-result hash, with no product bytes.
That hash reports historical publication, not a current draft-store commit or
asset correctness. `read_object_mask_edit` still independently validates the
current committed draft before constructing an inference-mask handle.

No outcome counts as automatic success or authentic human assistance. Full job
accounting, geometry/material payload adoption, real corpus freezes, native
visual output and rights/runtime qualification remain open. See the
[validation report](../validation/2026-10-25-draft-attempts.md).
