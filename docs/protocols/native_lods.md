# Certified native asset LOD admission

[`native_lod.hpp`](../../include/world_explorer/application/native_lod.hpp)
adds a bounded A0 contract over privately admitted geometry products. It checks
an independently selected, receipt-owned certificate against both actual meshes.
There is no simplification worker, distance-based runtime selection, streaming,
screen-space error measurement or AP8 performance qualification. Synthetic fixtures
retain their diagnostic label; no real reconstruction gate is completed.

## Exact fan profile

Version 1 / `NATIVE_EXACT_FAN_LOD` accepts only strict vertex/triangle reductions
with complete UVs/tangents. Base and candidate must share the exact native source,
capture, views, epoch/pose, receipt, surface declaration and manual ancestry.
The root's exact dependency set is the base and candidate geometry products.
The independently selected deviation ceiling and document ceiling must both be
zero. Original high-source correspondence is still admitted separately for each
mesh; a runtime vertex match alone cannot establish LOD quality.

The root contains exactly `schema_version`, `kind`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `base`, `candidate`, `max_deviation_units`, `vertex_sources`,
`faces`, `manual_ancestor`. References use the existing numeric product/revision
and lowercase SHA-256 encoding. Maximum certificate size is 1 MiB; both geometry
products retain their 8,192-vertex/face limits. Duplicate keys, unknown fields,
unsafe numeric values, excessive depth/counts and cancellation reject normally.

`vertex_sources` maps each candidate vertex to a distinct original runtime
vertex. Position, normal, UV and tangent components must copy exactly. Each output
face has `source_faces`, `center`, `weights`. Ascending source-face lists partition
**all** original faces exactly once, including every removed face:

- A singleton has null center/weights and preserves its oriented triangle.
- A three-face fan supplies the removed original center and three positive integer
  barycentric numerators summing to 256, ordered by the output triangle's corners.
  The center's complete incident face set must be exactly those three faces.
  Their oriented boundaries must form the output triangle's three-edge fan.
  The center cannot occur among retained vertices.

For a fan, every center component must equal the weighted interpolation of its
three retained boundary components. The initial exact arithmetic profile admits
components on the `2^-20` grid with absolute value at most `2^20`; conversion by
power-of-two scaling is exact, and signed 64-bit weighted sums cannot overflow.
Off-grid fan attributes refuse as unsupported. Retained identity-only attributes
do not need this additional grid restriction. The weights are strictly interior,
so the three original triangles partition the candidate triangle exactly. Affine
interpolation preserves every point, UV, unnormalized normal and tangent over the
whole patch in both directions, with zero surface displacement. This proof does
not measure error against physical ground truth or qualify arbitrary decimation.

Each output face must retain the material group, evidence class, source-view
support and uncertainty of every face it replaces. No hole bridging, component
deletion, material-boundary collapse or promotion to observed evidence is allowed.
The private handle owns both original geometry handles and the full face mapping.

## Definition identity, publication and downstream refusal

`NativeAssetSelection.m_lods` matches ordered `AssetDefinition.m_lods` geometry
references. Every certificate is relative to LOD0, and triangle counts strictly
decrease in declared order. The exact binding root is an additional definition
source, so changing its identity invalidates definition and instance hashes even
when geometry/material bytes stay unchanged. Every LOD needs its own complete
face-provenance sidecar matching its admitted correspondence and original views.

Existing immutable definition/scene publication stores these references and
replays idempotently. Reload requires independently re-admitted handles from the
same receipt and compares committed canonical metadata; failed admission/reload
preserves caller data. The original receipt owns root and geometry bytes.

Native textured export explicitly refuses definitions with LODs until an additive
companion manifest and resource mapping are implemented. The existing native PBR
viewer continues to consume supported export profiles; this admission API does
not silently discard LODs or start M5 streaming. Original no-LOD export bytes and
structure graphs' LOD0 face indices retain their meaning.
