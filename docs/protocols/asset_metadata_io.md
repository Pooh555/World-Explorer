# Asset metadata byte admission and owned publication v1

[`definition_io.hpp`](../../include/world_explorer/asset_catalog/definition_io.hpp)
and [`asset_instances_io.hpp`](../../include/world_explorer/scene_store/asset_instances_io.hpp)
add strict byte readers and persistent metadata publication to the
[typed contracts](asset_definitions.md). A0/AP0 remain **IN_PROGRESS**.
These records contain metadata, not validated mesh/material payloads, portable
asset packages, live scene transactions or permission to execute a worker.

## Independent selection and byte admission

Definition decoding requires an independently selected `(asset_id, revision,
sha256)`, original admitted input receipt, provenance selections and current
world epoch/pose revision. Instance decoding requires selected scene/revision,
SHA-256, definition handles, original receipt and context. Neither decoder learns
trusted selections from the supplied document. The source receipt remains
necessary; publication does not copy its payloads into a standalone package.

Readers bound input before parsing (64 KiB definition, 4 MiB instance set), reject
raw NUL, duplicate keys, depth above 16, foreign/missing fields, wrong types and
unsupported versions/enums. Array limits match typed admission and precede typed
vector growth. Integer IDs/counts cannot be floating-point or negative. Numbers
must be finite. Exact hashes and embedded source-receipt identity must match.
The reconstructed typed inputs pass the existing admission APIs, including region
inspection, variant, transform, unit, manual-ancestry and reference checks.
Finally, native canonical encoding must exactly equal the input bytes. Whitespace,
array order and alternate number encodings cannot create additional identities.
Failure preserves inputs and returns no partial handle.

## Publication ownership and history

Domain publishers accept private admitted handles and recheck source/context.
`AdmittedAssetReceipt::source_directory()` exposes the original canonical path for
output protection; admitted bytes remain usable without that source directory.
Selected source and output trees must be disjoint. `scene_store` publishes every
selected definition and the instance set in one metadata commit. Definition keys
are `asset-<id>-<revision>`; instance-set keys are `scene-<id>-<revision>`.
A committed key cannot acquire different bytes. New revisions use new keys;
old ones remain available. Ownership is scoped to this directory, not a global
identity authority or a trusted human-action journal.

The private JSON implementation is shared between the allowed `scene_store` →
`asset_catalog` edge; public headers expose project types only. The underlying
[`immutable_records.hpp`](../../include/world_explorer/core/immutable_records.hpp)
is an opaque byte primitive in `core`, with a private JSON dependency. Its
snapshot owns committed record bytes; callers must use domain decoders before
trusting their meaning.

A named output requires an existing nonsymlink parent/ancestor chain. An absent,
empty or lock-only root may initialize. Nonempty unowned/foreign/corrupt roots
are refused without repair or deletion. The closed root permits only:

| Entry | Contract |
|---|---|
| `owner.json` | Exact canonical schema 1, role `immutable_record_store_v1` |
| `.writer.lock` | Nonblocking cooperating-writer lock; remains after release |
| `intents/<operation_id>.json` | Immutable schema/version, positive operation ID, base generation and sorted key/hash/byte rows |
| `blobs/<sha256>.bin` | Immutable nonempty bytes matching filename hash and an owned intent |
| `manifest.json` | Atomic completion: schema 1, generation, sorted committed rows and operation/intent-hash/new-generation history |

Files must be nonsymlink regular files with one hard link; directories cannot be
links. Every observation validates physical blobs/intents and committed history;
the union of committed intents must exactly match the manifest's key bindings.
The manifest is reread to detect changes. Pending intent references can lack blobs;
committed references cannot. No file is silently cleaned up.

Limits: 65 records/8 MiB per request, 4 MiB per record; 4,096 committed keys,
64 MiB logical committed bytes and 64 MiB physical blobs (including pending
bytes); 8,192 physical blobs, 1,024 intents/operations, 16 MiB aggregate intent
bytes, 64 KiB per intent and 1 MiB manifest. Keys are 1–64 lowercase ASCII letters,
digits or hyphens. Counts, sizes and checked aggregate budgets bound allocation.

## Retry, interruption and scope

The writer checks conflicts, acquires its lock and rechecks, then durably installs
an immutable operation intent **before** any blob. The manifest changes last.
An operation ID binds the exact base and sorted record batch, including pending
attempts; changed reuse returns `DUPLICATE_OPERATION`. A committed key conflict or
wrong base returns `RESYNC_REQUIRED`. Exact completed replay verifies bytes and
returns the **current** store generation/hash without writes or observer calls,
even after later commits. Otherwise callers must reopen/resynchronize and select
a new operation if the base changed.

Cancellation or observer failure before manifest replacement retains the last
commit and any owned intent/blobs. Matching retries reuse those bytes. Readers
can inspect an owned first interrupted attempt as generation zero with no
committed records. Successful completion wins over late cancellation; uncertainty
after rename/flush returns `RESYNC_REQUIRED`, requiring reopen. The observer runs
after each verified/installed request record and before completion.

The [validation](../validation/2026-10-24-asset-metadata-io.md) covers a controlled
child exit after a durable intent and first fully installed blob. Arbitrary
power loss, disk-full/OOM and kills inside helper staging remain unqualified.
Unexpected staging entries are preserved/refused. Filesystem checks and locks
assume cooperating writers; hostile concurrent directory replacement is outside
this contract. Metadata is not adopted by the runtime: actual payload validation
must precede the existing scene update path, including initial publication.


## Physical operation inspection

Core `inspect_immutable_record_operation` uses the existing complete owned-store
admission to observe one operation's original base, immutable record key/hash/size
bindings and committed flag. It can report a durable uncommitted operation without
exposing pending bytes in the logical snapshot. It is bounded and read-only;
absence returns an empty optional, malformed/foreign stores fail. The
[draft attempt journal](draft_attempts.md) uses it to recover a finite canonical
outcome after interruption. This adds no persistent schema or adoption authority.
