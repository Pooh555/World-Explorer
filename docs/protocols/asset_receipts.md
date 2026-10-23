# Asset dependency receipt v1

Native `asset_catalog` now admits and publishes byte-bound snapshots of the
[dependency graph](asset_dependencies.md). The public API is
[`dependency_receipt.hpp`](../../include/world_explorer/asset_catalog/dependency_receipt.hpp).
This additive format leaves existing worker/scene schemas unchanged. It is not
an asset producer or mutable catalog. A0/AP0 remain **IN_PROGRESS**.

## Selection and bounded admission

`AssetReceiptSelection` contains a directory and independently selected lowercase
SHA-256 of its exact `receipt.json` bytes. `read_asset_receipt` also requires an
independently selected epoch/pose context; a receipt cannot select its own trusted
context. Admission is read-only and returns a privately owned immutable byte
snapshot. Copies share that snapshot. Later source edits do not change its bytes.

The strict JSON root contains exactly:

| Field | Contract |
|---|---|
| `schema_version` | Integer 1 |
| `operation_id` | Nonzero uint64; scoped to the owned publication directory |
| `graph_sha256` | Canonical native graph digest, independent of manifest formatting |
| `graph` | Exact graph encoding in the dependency protocol; context, products, references, recipes and manual ancestry |
| `files` | Exact deduplicated inventory of every product-content, recipe and manual-action digest in the graph |

Each inventory row has exactly `file`, `sha256` and `bytes`. The only valid path
is `blobs/<sha256>.bin`; size is a positive integer and bytes must hash exactly.
No arbitrary absolute/relative paths are accepted. Root/graph/node/reference/row
fields are exact; unknown fields, duplicate JSON keys, raw NUL, negative/floating
integers and wrong types are rejected. Unsupported schema returns
`UNSUPPORTED_OPERATION`; invalid bytes/schema/hash returns `INVALID_DATA`.
Stale graph dependencies or expected context return `RESYNC_REQUIRED`.

Budgets are 1 MiB manifest, nesting depth 16, 768 files, 16 MiB/file and 64 MiB
aggregate payload; graph bounds remain 256 products, 64 parents/product and
4,096 edges. Inventory sizes are checked before reading any payload. Oversized
declarations return `RESOURCE_EXHAUSTED`; lower-level I/O errors retain their
core error codes. Source root/blob directories and selected files must have the
expected nonsymlink type. Unreferenced source files are ignored and never copied.
Every required file is independently read/hashed; the manifest is reread before
return to detect changed selection. All admitted bytes are owned, with no caller
buffers or framework objects retained.

Recipe/action payloads are **opaque bytes** here. Hash binding does not prove
recipe completeness, rights, actual human actions, evidence classes or successful
execution. Those semantic schemas, trusted action journal, resource/installation
locks and coordinator integration remain A0 work.

## Immutable publication and retry

`publish_asset_receipt` requires a named output directory with an existing
parent, disjoint from the admitted source tree. It revalidates the supplied
current context before writes and again before completion. The coordinator must
provide coherent current context; this API is not a transaction with `map_store`.

An absent, empty or lock-only directory may initialize. Nonempty unowned output
is refused before adding files. Publication uses a nonblocking `.writer.lock`
(`WOULD_BLOCK` on contention) and an immutable `owner.json` containing exactly
schema 1, role `asset_dependency_receipt`, operation ID and receipt SHA-256.
The output root permits only owner, lock, `blobs/` and `receipt.json`. Stored
blobs must be expected nonsymlink regular files with the exact admitted bytes.
Foreign/corrupt entries are refused and preserved, never overwritten or removed.

Files are installed with the core durable immutable-publication helper. The
completion manifest appears atomically only after all payloads are installed and
the output is rechecked. Matching interrupted requests retain verified blobs and
install only missing ones. A matching completed request verifies every byte and
returns without writes or observer calls. The same operation with different
receipt bytes, including whitespace changes, is a collision; a different operation
cannot adopt the directory. This is per-directory identity, not a global journal.

Cancellation or an observer exception before completion retains owner/verified
blobs without a completion record. The observer runs after each verified or
installed blob, before completion. Successful completion wins over late
cancellation; an uncertain manifest flush outcome returns `RESYNC_REQUIRED`
and requires reopening. Errors do not roll back or delete retained output.

The [validation](../validation/2026-10-24-asset-receipts.md) kills an owned child
after one fully installed blob and resumes after the OS releases its lock. It
does not qualify arbitrary power loss, disk-full/OOM or kills during helper
staging. Unexpected leftover staging entries are preserved/refused; inspect them
or choose a fresh output directory. Locks coordinate cooperating writers; path
checks do not promise protection against hostile concurrent directory mutation.
Only the recorded local Linux profiles are verified.
