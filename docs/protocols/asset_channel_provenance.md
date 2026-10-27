# Asset-definition channel provenance v2

Asset-definition **schema 2** adds explicit material-channel bindings to the
[typed metadata contract](asset_definitions.md). It supports independently
partitioned channels without manufacturing one material-wide mask. Schema-1
canonical bytes and admission remain supported; v1 still refuses heterogeneous
channel summaries at [native adoption](native_assets.md). A0/AP0 remain
**IN_PROGRESS**. No producer, observed correspondence, material recovery, rights
or real AP capability is qualified by these declarations.

## Definition and independent selection

Each schema-2 material has the existing `variant_id`, `product` and `lighting`, plus
`channels`, an array of exactly `role`, `product` and `provenance` per row. Product
references retain the canonical metadata fields `product_id`, `revision` and
`content_sha256`. The root is the original material product. Each channel selects
its original payload and original region sidecar, both exact direct dependencies
of that root in the admitted receipt graph. The shared `MaterialChannelRole` enum
now belongs to `asset_catalog`; its existing values and binary/export conventions
are unchanged.

| Lighting mode | Required channel roles |
|---|---|
| CAPTURED_APPEARANCE | CAPTURED_RGB only |
| ESTIMATED_PBR / GENERATED_PBR | BASE_COLOR, METALLIC, ROUGHNESS, NORMAL, OCCLUSION, EMISSION |

Roles and payload products are unique within a material. Different materials may
share a channel only with identical role, lighting mode and provenance binding.
Geometry, material roots, channels, LODs and colliders cannot alias incompatible
output roles. Every ref is bound to the independently admitted receipt before
caller strings are copied. Sorting operates on bounded borrowed channel refs;
owned canonical output sorts materials by variant and channels by role.

The definition's `provenance` list and independent `AssetProvenanceSelection` list
contain exactly one sidecar for geometry, each **distinct channel**, each LOD and
optional collider. Shared channels use one selection. Material-root aggregate
sidecars are absent in schema 2, rather than being treated as authoritative
summaries. Native region-v1 admission independently checks each selected target,
domain, complete partition, source table, confidence, parents and manual ancestry.
The channel binding must select that exact sidecar. Different channels may retain
different classes, spans, confidence and source/support tables.

Captured channels cannot claim inferred/generated/edited texels. Estimated PBR
channels cannot hide generated/edited texels. Generated PBR retains the original
per-channel classes, including observed/inferred regions; it does not relabel all
texels generated. Definition/instance manual ancestry remains the union of bound
products, with per-channel manual element counts retained separately. This is
declaration checking, not authentication of human actions or physical support.

Limits remain 16 materials/definition, at most six channels/material and 64 KiB
canonical definition bytes. The provenance-selection cap is **106** for v2
(geometry + 16×6 channels + eight LODs + collider); v1 retains 26. Existing graph,
region, source, instance and byte budgets remain. V1 rejects typed channel fields
and extra JSON fields; v2 requires them. Strict decoding rejects unknown versions,
roles, duplicate/extra/missing fields, wrong number types, noncanonical order and
stale independently selected hashes/context. Canonical v1 is never auto-upgraded.

## Native adoption, persistence and downstream scope

Metadata admission checks the graph and sidecars; it does not parse channel
payloads or establish that a role is physically correct. Native adoption requires
the original private `AdmittedMaterialProduct` and matches every v2 role, payload,
sidecar and complete provenance record to its independently admitted channel.
A self-consistent metadata swap cannot substitute another channel. Schema 1 keeps
its exact comparison against the material-root summary.

Existing blob-first/manifest-last metadata publication, semantic revision keys,
operation replay, cancellation and selected reload apply unchanged. Switching
schemas changes definition bytes/hash and requires a new semantic revision; it
does not overwrite an earlier committed definition or scene revision.

The existing native exporter retains the v2 canonical catalog and **exact original
channel mask bytes** in provenance sidecars. Its per-material channel mapping and
closed inventory bind their hashes. Native PBR compilation/bundle persistence
retain the exact definition hash, export manifest and channel-sidecar pins.
Runtime bundles do **not** embed those mask records or add provenance-inspection
UI; consult the original package for the masks. No shader, texture conversion,
scene-update protocol or bundle version changes in this slice. Real A1 output,
ordinary Godot import and full producer/recovery/rights closure remain open.
