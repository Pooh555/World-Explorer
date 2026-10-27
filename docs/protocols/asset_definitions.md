# Typed asset definitions v1/v2 and instance sets v1

Native [`asset_definition.hpp`](../../include/world_explorer/asset_catalog/asset_definition.hpp)
and [`asset_instances.hpp`](../../include/world_explorer/scene_store/asset_instances.hpp)
implement bounded typed metadata admission and canonical JSON encoding.
A0/AP0 remain **IN_PROGRESS**. Separate [byte I/O and owned publication](asset_metadata_io.md)
now decode these canonical records and enforce per-directory committed revision
ownership. Neither layer is a live scene update, exporter or asset producer.

The [action-bound asset edit adapter](asset_definition_edits.md) now adds actual
bounded generated mesh/constant-material payload checks, native region validation,
attempt history and atomic attached before/after definitions. Its restricted
profile is separate from this general metadata admission and from full asset
quality, material inference and renderer/export qualification.

## Ownership, identity and immutable handles

`core` owns distinct `AssetId`, `AssetInstanceId`, `AssetMaterialVariantId` and
existing `AssetProductId` types. Semantic identities never merge because geometry
bytes match. `admit_asset_definition` in `asset_catalog`
validates reusable definitions. `scene_store` owns `admit_asset_instances` and now
has an explicit public CMake/include dependency on `asset_catalog`; the boundary
gate admits this single edge. Neither library introduces engine/framework headers.

Both APIs accept an independently expected world epoch/pose revision and an
[admitted receipt](asset_receipts.md). Context is checked against its closed DAG.
All product references must equal the selected product/revision/content hash.
Caller-owned strings are bound before copying/encoding them. No file is opened or
written, worker launched, dependency downloaded or scene store mutated.

Success creates a privately constructed immutable shared handle. Const accessors
expose normalized metadata, canonical bytes and SHA-256; copies retain lifetime
and do not empty the source handle. Failure returns no partial handle and preserves
caller data. These handles certify this metadata contract, never legal rights,
physical measurement, material quality or runtime safety.

A definition reference is `(asset_id, revision, sha256)`, separate from input
product IDs. Instance sets select these exact references; two instances reuse one
selected handle. At most one revision per asset ID is selected in a set. This
admission layer has no global history. The separate owned publisher rejects
conflicting bytes for committed semantic ID/revisions and binds operation IDs
within its metadata directory; runtime transactions remain separate. Draft metadata admission alone does
not authorize replacement of a committed revision.

## Definition fields and checks

| Typed field | Contract |
|---|---|
| Schema/identity/context | Definition version 1 or 2, instance version 1, positive asset ID/revision, exact expected context |
| Kind | PROP or STRUCTURAL |
| Sources | 1–64 unique exact product references |
| Geometry/materials | One geometry product, 1–16 positive unique material variant IDs/products; lighting declared separately |
| LODs/collider | 0–8 ordered distinct LOD products; optional distinct collider, UNVERIFIED or SYNTHETIC_GAMEPLAY only |
| Units | ARBITRARY with no conversion/measurement, or DECLARED_METRIC with finite positive metres per unit and a selected measurement reference |
| Bounds | Finite ordered asset-frame bounds, not a single point; intentional thin surfaces can have zero extent on an axis |
| Pivot | Finite rigid `asset_from_geometry`, wxyz rotation, unit scale, explicit policy and orientation ambiguity |
| Provenance | V1: one selected sidecar per geometry/material/LOD/collider, at most 26. V2: original per-channel sidecars, at most 106; see [channel bindings](asset_channel_provenance.md) |
| Rights/manual ancestry | Exact selected rights-product reference; manual flag equals ancestry of every bound product, including override/measurement references |

Geometry, material, LOD and collider roles cannot alias one product ID. Automatic
PROP pivot policies are SUPPORT_BASE_CENTER or ATTACHMENT_ORIGIN; STRUCTURAL uses
STRUCTURAL_FRAME. OVERRIDE requires a selected action product with declared
manual ancestry. Actual pivot/support correctness and action authenticity remain
unverified. Definitions contain no instance placement or capture-light parameters.

Independent `AssetProvenanceSelection` inputs bind exact sidecar refs and
[region selections](asset_region_provenance.md), including domain/source metadata.
They are supplied outside the definition, never selected by its supplied masks.
Admission runs the region inspector for every output: geometry/LOD/collider use
face domains; materials use texel domains. The handle retains class/manual-count
summaries. CAPTURED_APPEARANCE forbids inferred/generated/edited texels;
ESTIMATED_PBR forbids generated/edited texels. GENERATED_PBR can retain mixed
classes with the required masks. These consistency checks do not prove relighting
or transform radiance into albedo.

Units and rights remain declarations. DECLARED_METRIC never becomes the existing
METRIC_MEASURED runtime status. Rights/measurement bytes and mesh/material/LOD/
collider payloads are owned by the receipt but are not semantically interpreted
here. No collider in this schema authorizes measured walking or Q5/N2. Actual
calibrated correspondence, surface transfer, trusted journaling, legal review and
payload schema/quality checks remain required.

## Instance-set fields and checks

Version 1 has positive scene ID/revision, exact context, one unit frame, optional
selected manual-action product, instances and an aggregate manual flag. Each
instance has positive unique ID; exact definition reference; `world_from_asset`;
permitted material-variant ID; visibility; optional exact room/support product
references; source-instance lineage and a manual flag.

All definitions must use this exact input receipt and unit declaration; implicit
unit conversion is refused. Rotations use the existing normalized-quaternion
check; translations and uniform positive scale must be finite. Negative/zero
scales reject. Every transformed bounds corner must remain finite. Room/support
membership and lineage-container contents remain declarations: references are
byte/context bound, but structure and previous-assembly schemas are not decoded.

A lineage entry contains positive prior instance ID and exact product reference;
duplicate `(product ID, instance ID)` pairs reject. Instance manual ancestry is
its definition, room/support/lineage ancestry OR the set's explicit action/unit
ancestry. The aggregate set flag cannot clear these declarations. No actual human
edit can be authenticated by hashing a declared action.

Limits: 64 definitions, 4,096 instances, 16 lineage entries/instance and 16,384
lineage entries total. Every selected definition must be used. Empty instance
sets with no definitions are valid. Definitions encode to at most 64 KiB and
instance sets to at most 4 MiB. Existing receipt/graph and region limits remain.

## Encoding, errors and next boundary

Canonical JSON uses compact sorted keys, no trailing newline and explicit nulls.
It includes schema/context and input receipt hash. Definition sources/provenance,
material variants, selected definitions and instances sort by semantic identity;
LOD order remains meaningful. Instance lineage sorts by product and prior instance
ID. Numeric values are preserved; equivalent quaternion signs or signed zeros
are not normalized into mathematical equivalence. SHA-256 identifies these exact
bytes. Every definition embeds once in the selected handle list, without copying
geometry/material bytes into instance metadata. Unrelated input-receipt changes
conservatively change metadata identity; independent reuse is not inferred.

Wrong schema/enums return UNSUPPORTED_OPERATION; invalid values/duplicates/manual
flags INVALID_DATA; stale context/revision/hash/snapshot/unit bindings
RESYNC_REQUIRED; missing/duplicate independent provenance selections
INVALID_ARGUMENT; count/byte excess RESOURCE_EXHAUSTED; cancellation CANCELLED.
Region inspection errors propagate. Strict byte decoding and immutable owned
publication support these records;
runtime adoption still requires the separate typed payload converters. Existing
runtime scene/update formats are unchanged by metadata admission.

[Schema-2 channel bindings](asset_channel_provenance.md) now admit independent
material-channel masks without a root summary. V1 canonical encoding and its
lighting restrictions remain unchanged; owned decoding/publication supports both.
