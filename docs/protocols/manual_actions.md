# Manual-action journal v1

Native [`asset_catalog`](../../include/world_explorer/asset_catalog/manual_actions.hpp)
owns bounded declared action receipts and an immutable local journal. The shared
[`application` command](../../include/world_explorer/application/manual_actions.hpp)
records a declaration without editing an asset or launching a worker. A0/AP0
remain **IN_PROGRESS**. This is not authenticated human activity, consent
verification, a complete mutation audit or an automatic-success classifier.

## Requests and classification

`ManualActionId` is distinct from product IDs and operation IDs. Requests own an
action ID, kind, actor token, start Unix milliseconds, elapsed and active
milliseconds, exact target revisions/hashes and UTF-8 detail. Targets must exist
in the independently admitted receipt at the selected world epoch/pose revision;
stale or foreign targets/context require resync. Duplicate product IDs reject.
Target order does not affect canonical bytes.

| Derived scope | Declared kinds |
|---|---|
| `SETUP` | `SELECT_INPUT`, `SELECT_RECIPE`, `CAPTURE_MEASUREMENT` |
| `EVALUATION_ONLY` | `ANNOTATE_EVALUATION` |
| `OUTPUT_ASSISTANCE` | `SELECT_OBJECT`, `EDIT_MASK`, `SELECT_CANDIDATE`, `ALIGN_OUTPUT`, `EDIT_GEOMETRY`, `EDIT_MATERIAL`, `APPROVE_OUTPUT` |

An edit kind cannot independently select setup/evaluation scope. False
declarations about which action occurred remain undetectable. Detail is opaque
text, not executable parameters or verified change evidence. Actor tokens are
declarations, not authenticated accounts; do not put credentials in detail.
Times are supplied by callers, not measured labor. Accounting sums declarations
without deduplicating overlapping/repeated human work.

Bounds: 1–16 targets, 1–64 lowercase ASCII letters/digits/hyphens in the actor token,
1–4,096 UTF-8 bytes of detail without NUL, and at most 16 KiB encoded record bytes.
JSON escaping consumes that byte budget. Elapsed time is at most seven days;
active time cannot exceed elapsed time. Start plus elapsed must fit Unix
milliseconds through year 9999. Zero elapsed/active and epoch-zero timestamps are
allowed declarations. No clock authority or inter-actor chronology is claimed.

## Encoding, publication and recovery

`record_manual_action` delegates to `append_manual_action`. Version-1 fields are
`schema_version`, `action_id`, `kind`, derived `scope`, `actor`,
`started_unix_ms`, `elapsed_ms`, `active_ms`, `detail`, sorted `targets`,
`operation_id`, `base_generation`, `world_epoch`, `pose_revision`,
`receipt_sha256` and `graph_sha256`. Target rows have `product_id`, `revision` and
`content_sha256`. Encoding is compact sorted-key UTF-8 JSON, unsigned decimal
integers, no extra whitespace or newline. The action SHA-256 hashes those bytes;
it is distinct from target, receipt, graph and journal manifest hashes.

The [immutable record registry](asset_metadata_io.md) stores each record as
`manual-<action_id>`, one action per operation/generation. At most 1,024 actions
are allowed; committed records require unique operation IDs and contiguous base
generations from zero. Closed-schema decoding rejects foreign keys/records,
duplicate JSON keys, unknown versions/kinds, wrong scope, invalid integers/bounds
and noncanonical bytes. Existing committed records are checked before appending.
Input receipt/output trees must be disjoint.

The registry owns writer exclusion, immutable intent before blob, atomic manifest
last, cancellation and controlled crash recovery. Exact retries bind the original
base and all canonical request bytes; completed replay returns the current
validated commit. Reordered targets replay identically. Changed operation reuse
rejects; an action ID cannot acquire a new operation or different bytes. Wrong
bases require resync. Caller files are preserved without repair/deletion.
Pending actions are excluded from committed accounting. Matching retry recovers
them; a declaration does not prove a corresponding output edit completed.

## Inspection and inference boundary

`read_manual_action_journal` requires an independently selected generation and
manifest SHA-256. It owns decoded history after source deletion. Generation zero
with an empty digest selects an owned interrupted journal with no committed
actions; nonempty selections require lowercase SHA-256. History is returned in
generation order with separate counts and active-time sums by scope.

```sh
build/dev/we_process inspect-actions JOURNAL GENERATION MANIFEST_SHA
```

This read-only CLI prints declared accounting and its authenticity/completion
limit. Wrong generation/hash or invalid arguments exit 2.

`inference_manual_action` returns a journaled action hash only for
`OUTPUT_ASSISTANCE`, with the exact original receipt, graph, context and a listed
target. Setup/evaluation records cannot supply inference manual ancestry through
this API. No evaluation labels or bytes are exported. Missing actions reject;
stale/source/target substitutions require resync. Existing graph admission still
validates declared action hashes without requiring this journal binding. Actual
mutation/result linkage now has a [draft byte replacement path](draft_mutations.md),
with exact intents, revised dependency closure and prepared/committed history.
Typed mutation validation, enforced adoption by inference/asset commands and
terminal failure accounting remain A0 integration work. A valid selected journal
may contain false declarations from a cooperating caller; it is not signed or tamper-proof against an actor who
can replace history and its independently selected pin.

The [validation](../validation/2026-10-25-manual-actions.md) covers synthetic native
admission, independent hashes, interrupted publication and controlled child exit.
Hostile filesystem replacement, arbitrary power loss, real human actions and full
product yield/rights qualification remain outside that evidence.
