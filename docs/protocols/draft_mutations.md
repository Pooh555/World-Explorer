# Action-bound draft metadata replacement v1

[`draft_mutations.hpp`](../../include/world_explorer/asset_catalog/draft_mutations.hpp)
and the shared [`application` command](../../include/world_explorer/application/draft_mutations.hpp)
publish selected draft product bytes with durable action/operation linkage.
A0/AP0 remain **IN_PROGRESS**. Payloads remain opaque: this does not validate or
produce masks, geometry, materials, typed assets or live scene transactions.

## Admission and ancestry

`replace_draft_metadata` receives independently admitted before/after receipts,
an admitted [manual-action journal](manual_actions.md), selected context and a
request containing action ID, kind, operation ID, even base generation and exact
target references. Only `EDIT_MASK`, `EDIT_GEOMETRY` and `EDIT_MATERIAL` are
supported replacement declarations. The kind and complete target set must match
the journal entry. Its exact original receipt/graph/context binding must pass
`inference_manual_action`; setup/evaluation actions cannot enter this command.
Target order is normalized; target IDs cannot be duplicated.

Before/after graphs must have identical product membership. The command computes
the complete transitive closure of selected targets in the before graph:

| Products | Required after state |
|---|---|
| Selected targets | Same IDs, exact revision +1 without overflow, different content bytes, unchanged recipe, current journal action hash and manual ancestry |
| Every transitive descendant | Exact revision +1, different bytes, manual ancestry, unchanged own action declaration and recipe |
| Unrelated products | Identical node fields; dependency ordering may differ |

Dependency IDs/edges remain unchanged; references must select their exact new
parent revisions/hashes. Both graphs pass existing closed-DAG validation, which
retains inherited manual ancestry. Split/merge, added/removed products, changed
recipes and partial descendant refresh are unsupported. Old action/source
identities remain in immutable history. Distinct proposal bytes do not prove
that a descendant was recomputed correctly; typed payload checks remain required.

At most 16 affected products, including descendants, and 64 KiB of before/after
bytes per product are admitted. After-receipt action bytes must hash to the
selected journal action (at most 16 KiB). All bytes are copied from owned admitted
snapshots before publication. Input receipt and journal source directories are
protected from output overlap in either direction. `source_directory()` on the
journal records its original canonical path for that protection, even after the
source is removed; it is not read again when using an owned handle.

## Two durable phases

The existing [immutable registry](asset_metadata_io.md) owns files, writer locks,
immutable blobs and atomic manifest publication. A dedicated output directory
serializes mutations: at most one prepared mutation, no interleaved requests.
This command reserves core operation IDs `2*n-1` and `2*n` for positive request
operation `n ≤ 2^63-1`. Bases are even, 0–1,022; at most 512 mutations fit the
1,024-operation ceiling, further bounded by 4,096 keys/64 MiB registry limits.

1. Commit `mutation-<action_id>` at the selected even base. This canonical intent
   binds kind, operation/base, context, both receipt/graph hashes, journal commit
   pin/action hash, sorted targets and all before/after revision references.
2. At base +1, publish immutable `product-<id>-<revision>` bytes for both versions,
   `action-<action_id>` bytes and `result-<operation_id>`, then atomically publish
   their manifest. The result binds the intent hash and sorted output references,
   with `status: COMMITTED` and `assisted: true` for this draft publication only.

Intent encoding is compact sorted-key UTF-8 JSON, integer IDs/counts, no extra
whitespace/newline, at most 32 KiB. History decoding rejects unsupported versions/
kinds, foreign keys, duplicate keys, noncanonical bytes, inconsistent references,
operation reuse, stale branching, missing/changed bytes and noncontiguous phases.
Before a new intent, all previously changed products in the selected input graph
must match the directory's committed heads. Existing revision keys cannot acquire
different bytes. No source or foreign output is repaired/deleted.

Exact retry binds all original parameters, including receipt and journal pins.
Completed replay returns the current validated commit without observers; prepared
retry resumes the same result phase. Different reuse rejects. Action uniqueness
is scoped to this owned directory, not a global authority across separate roots.

## Observation and limits

`read_draft_metadata_mutation` requires the original selected inputs/request and
an independently selected current commit pin. It validates the complete serial
history and the exact intent. `PREPARED` exposes no output bytes or result hash.
`COMMITTED` owns the replacement bytes and hashes. Old commit pins require resync
after later publication. Core `read_immutable_records` can observe a current pin
after interruption; this observation is not authenticated history selection.

`INTENT_DURABLE` observer events follow the first manifest commit;
`RESULT_RECORD_WRITTEN` events precede the second manifest commit. Observers are
diagnostic/fault-injection callbacks, not output-edit hooks. Cancellation and
observer exceptions return explicit errors and retain prepared history; partial
result blobs are never exposed as committed outputs. Resume may repeat progress
notifications. Failure reasons are returned, not durably recorded as distinct
terminal failed/cancelled events. Core staging interruption before intent commit,
arbitrary power loss and hostile filesystem replacement retain their prior limits.

The command enforces lineage and byte publication within this path. Legacy graph
and metadata admission still validate declarations without requiring it. Actor
authenticity, actual human labor, omitted external edits, typed descendant
correctness and enforced adoption by inference/asset commands remain A0 work.
No automatic-success classification, worker execution, measured collision or
runtime adoption follows. The [validation](../validation/2026-10-25-draft-mutations.md)
records local synthetic CPU/recovery evidence separately from real product gates.
