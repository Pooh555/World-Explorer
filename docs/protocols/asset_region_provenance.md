# Asset region provenance v1

Native [`region_provenance.hpp`](../../include/world_explorer/asset_catalog/region_provenance.hpp)
implements bounded sidecar admission for declared face/texel evidence and lineage.
A0/AP0 remain **IN_PROGRESS**. No reconstruction, material fitting, measured
support, manual-action authentication or viewer feature is certified by admission.

## Selection and identity

`inspect_asset_region_provenance` accepts an [owned receipt](asset_receipts.md),
sidecar product ID, independent `AssetRegionSelection`, expected world epoch/pose
revision and cancellation token. The selection binds an exact target product,
revision/content hash, domain and complete source-view table. The admitted graph
is revalidated against expected context. The sidecar must have exactly the target
as its dependency and no manual action of its own. Its content bytes contain the
record; the target's recipe hash binds its selected transformation recipe.

The sidecar is a separate product, avoiding a self-referential content hash.
Existing receipt, recipe and scene schemas do not change. All referenced image
products and target/recipe bytes already belong to the immutable receipt. A
later change/removal of the source directory cannot redirect inspection.

Each source row binds `(capture_id, frame_id)`, positive calibration ID, an exact
image-product reference and lowercase SHA-256 identities for calibration, pose
and image-grid transformation. Source images must be graph ancestors of the
target. Their metadata table must equal the independent caller selection; order
has no meaning. Duplicate view identities reject. Different frames may explicitly
share image bytes/product identities without merging their semantic identities.

These metadata hashes are independently selected declarations. This inspector
does not parse calibration/pose/grid payloads, authenticate their origin or prove
image-to-surface correspondence. Real observation adapters must supply verified
bindings; a self-consistent synthetic selection cannot establish measured support.

## Record and partition

Strict JSON has exactly:

| Field | Contract |
|---|---|
| `schema_version` | Integer 1 |
| `world_epoch`, `pose_revision` | Exact independently expected context |
| `target` | Exact `product_id`, `revision`, `content_sha256` reference |
| `recipe_sha256` | Exact target recipe hash in the graph |
| `domain` | Exactly `kind`, `elements`, `width`, `height` |
| `sources` | Complete selected source table, described below |
| `spans` | Complete ordered nonempty partition, described below |
| `manual_ancestor` | Boolean equal to target graph ancestry and aggregate span ancestry |

`GEOMETRY_FACES` uses positive `elements`, zero width/height and zero-based face
ordering. `MATERIAL_TEXELS` uses positive width/height (each at most 32,768) with
`elements = width * height`, checked before multiplication. Texels flatten as
`y * width + x`; integer pixel centers and image/UV conventions remain explicit
in the selected image-grid identity. No mesh or texture is parsed here: the caller
must independently select the actual domain/order rather than trust a sidecar's
own dimensions. Counts measure elements, not physical area or visible coverage.

A source row has exactly `view`, `calibration_id`, `image`, `calibration_sha256`,
`pose_sha256`, `image_grid_sha256`; `view` has exactly `capture_id`, `frame_id`.
`image` uses the same exact product-reference shape as `target`.

A span has exactly `first`, `count`, `class`, `confidence`, `parents`, `support`,
`manual_ancestor`. Positive counts form a contiguous partition from zero through
all elements: no overlap, gap, trailing omission or out-of-range interval.
`parents` are unique exact direct dependencies of the target. `support` contains
unique selected view identities. Every source-table row must be used by a span.

Classes are `UNKNOWN`, `OBSERVED`, `INFERRED`, `GENERATED`, `MANUALLY_EDITED`.
Confidence is independently null or a finite number in [0,1]; high confidence
never changes class. UNKNOWN requires null confidence and no support claims;
it can retain known transformation parents/manual ancestry. OBSERVED requires
at least one source view; this checks a declaration, not actual measurement.
Other classes may retain source support without becoming observed.

## Declared manual ancestry

A MANUALLY_EDITED span requires a declared action on the target graph node.
Every declared target action requires at least one such span in this restricted
profile. A span's manual flag equals its local manual class OR manual ancestry
of any listed parent/support image. Automated descendants retain that flag while
keeping their current evidence class. The record's flag equals whether any span
has manual ancestry and must equal the target's already validated graph flag.
Thus a record cannot clear an entire declared manual ancestry or invent one
without lineage. Supporting manually derived image bytes also propagates ancestry.

The graph and these region bindings still describe declarations. They do not
prove that every affected element was mapped correctly, detect omitted external
human actions or authenticate a journal. Trusted manual commands and checked
surface/texel transfer maps remain separate work. No automatic-yield approval or
measured Q5/N2/walking permission follows from a passing record.

## Bounds, outputs and failures

Limits: 4 MiB encoded record, 16,777,216 elements, 16,384 spans, 64 source views,
64 parents and 64 support references per span, 65,536 combined parent/support
references total. Existing receipt/graph bounds remain. Strict parsing rejects
duplicate keys, raw NUL, extra/missing fields, unknown enums/versions, negative/
fractional sizes and excessive nesting. Arithmetic is checked before accumulation.

The owned result retains record/receipt/graph/content identities, class counts
in enum order (UNKNOWN through MANUALLY_EDITED), and a separate manual-ancestor
element count. Sources, parents and support are returned in sorted identity order;
span order is semantic and preserved. Byte identity still selects exact source
JSON, so equivalent reordered tables can have different content hashes.

Malformed partitions/classes/lineage return `INVALID_DATA` (unknown schema/enums
`UNSUPPORTED_OPERATION`); stale context/target/recipe/domain/source bindings or
nonexact target-parent refs `RESYNC_REQUIRED`; missing selected products
`NOT_FOUND`; invalid/duplicate caller view IDs or malformed caller metadata
digests `INVALID_ARGUMENT`; count/byte
excess (including a zero-element domain) `RESOURCE_EXHAUSTED`; cancellation `CANCELLED`. Underlying receipt errors
propagate. Failed admission preserves caller selections, files and snapshots;
there is no write, partial publication, process, network or provisioning operation.
