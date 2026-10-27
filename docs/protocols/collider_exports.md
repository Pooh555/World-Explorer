# Native collider export companion

`NativeExportRequest.m_colliders` explicitly selects a
`NativeExportColliderPolicy`. Manifest schema **4** / `WE_NATIVE_COLLIDER_GLTF_V1`
then carries admitted [static structural collider input](native_colliders.md)
alongside the existing textured render resources. At least one definition must
have a collider; other definitions may remain render-only. Original schema-1–3
profiles retain their bytes and refuse collider definitions without this opt-in.
The existing 2-definition/4-material/64-instance, 64-file/8 MiB, 1 MiB/file limits
remain; a larger admitted base product can therefore refuse export.

## Retained inputs and mappings

Each collider exports these exact bytes, without re-cooking or remeshing:

- `colliders/<asset_id>/root.json`: original collider product.
- `colliders/<asset_id>/base.json`: original LOD0 geometry product, with its
  source identities and correspondence declarations; no original images are copied.
- `colliders/<asset_id>/triangles.bin`: original canonical binary64 cook input.
- Its existing `provenance/<product_id>-<revision>.json` face sidecar.

The authoritative `colliders.json` (schema 1 / `NATIVE_COLLIDER_COMPANION`) pins
the receipt, canonical instance set, epoch/pose, `LOD0`, `SYNTHETIC_GAMEPLAY`,
`NOT_COOKED`, walking false and the selected world-position residual limit.
Each definition row records exact definition/product/base references, all four
file hashes/paths, source face IDs, counts, body/unit/frame labels,
`asset_from_collider`, every instance ID/glTF node index and composed
`world_from_collider`. One canonical triangle stream serves every copy.
Collider coordinates remain in the base geometry frame; the asset pivot is
applied by the companion transform rather than baked into these bytes.

World placement composes the instance's positive uniform transform with the
rigid asset pivot, using wxyz quaternions. It is checked independently against
sequential application at every original collider vertex and origin/unit-axis
probes. The largest binary64 Euclidean residual must not exceed the separately
selected finite `m_world_position_units` (0–1 arbitrary world units, default
`1e-8`). This is a numerical composition check on those inputs/probes, not a
metric measurement, physical clearance proof or screen-space error certificate.
glTF node rotation remains explicitly xyzw; companion transforms remain wxyz.

The manifest pins `colliders.json` through `collider_companion` and records
`collider_limits`. Its selected render-asset mappings retain the collider summary
and all input hashes, without duplicating full instance/source-face tables.
Render-only definitions have a null collider summary. All files enter the closed
inventory. The portable package contains the original complete proofs and data;
custom glTF extras are not their only copy.

## LODs, structures and native display

The profile composes with explicit render LOD selection and structural sidecars.
Colliders and graph face IDs remain **LOD0-bound** even when the selected render
mesh is another level. Individual/assembled glTF resources still share the same
selected mesh/material/buffer/image definitions. No physics nodes, nonstandard
cross-document references, automatic LOD switching or collision extension are
invented. glTF documents identify the new profile explicitly.

The existing native compiler accepts this private package and keeps the companion,
root/base/cook/provenance pins in the immutable PBR source manifest. Bundle schema
6 and its binary layout are unchanged. Native admission checks the profile,
inventory pins, identifiers, frame/count/limit labels and walking false. Full
collider bytes and placement tables remain in the portable package; source pins
are integrity bindings, not authenticated production evidence.

Native scenes retain `UNCOOKED_GAMEPLAY_COMPANION` provenance, collision unavailable
and the existing walking barrier. They render ordinary PBR resources only; no
Jolt shape, navigation support ledger or engine physics body is installed.
Package admission rebuilds and compares every file from independent private
inputs; existing immutable publication/replay/cancellation/source protection and
selected-commit reload apply. Backend/convex cooking, engine import/packaging,
real production/scale/support and AP8 qualification remain open.
