# Action-bound generated static asset edits

[`asset_definition_edit.hpp`](../../include/world_explorer/application/asset_definition_edit.hpp)
adds `attempt_asset_definition_edit` and `read_asset_definition_edit` to the shared
application. Both `EDIT_GEOMETRY` and `EDIT_MATERIAL` are supported for the bounded
profile below. A0/AP0 remain **IN_PROGRESS**. No scene transaction, worker,
renderer/export adapter or evaluated PBR product follows from this admission.

## Definition, selection and atomic publication

The command supplies original draft identity/directory and independent before/after
definition references, bytes and complete region selections. Both definitions
undergo existing [canonical native decoding](asset_metadata_io.md), exact receipt/
context binding and [region admission](asset_region_provenance.md). Actual mesh and
material bytes then undergo `inspect_static_asset_payloads`. All of these checks
precede any [attempt journal](draft_attempts.md) write.

Asset ID stays fixed and definition revision advances exactly once without
overflow. Kind, sources, rights, units, pivot, LOD/collider policy, material variant
and geometry/material/provenance product identities remain fixed. Actual bounds
are recomputed from all vertices through the declared rigid pivot and must match
exactly. An edit targets exactly the original geometry or material product.
Geometry edits must change mesh data while preserving material parameters;
material edits must change parameters while preserving the exact geometry.
No-op and cross-kind edits reject. Every element of the edited root must be
declared `MANUALLY_EDITED`; descendants retain native derived manual ancestry.

The adapter derives an admission hash from compact sorted-key JSON with
`schema_version: 1`, `adapter: GENERATED_STATIC_ASSET_EDIT`, `before` and `after`;
each reference contains `asset_id`, `revision`, `sha256`. Callers cannot supply
their own binding or attached records. Generic graph admission still enforces
the exact journal action, full transitive replacement closure, revision +1/new
bytes, fixed dependency IDs/recipes and unchanged unrelated products.

A definition includes its receipt hash, so it cannot be a product inside that
same receipt without a hash cycle. Explicit draft intent v3 instead attaches both
canonical definitions under `metadata-asset-<asset_id>-<revision>`. Intent identity
binds each key/size/hash; product, definition, action and result blobs share the
existing atomic result manifest. Prepared reads expose neither definition.
Existing attachment keys cannot change bytes. Before a new typed intent, the
selected definition must match the highest committed definition revision for
that asset in this directory; replay/collision uses the original full intent.
This ownership is local to the selected journal, not a global asset authority.

`read_asset_definition_edit` repeats admission and requires an independently
selected current draft commit and exact v3 intent/result/attached bytes.
Prepared history returns `WOULD_BLOCK`. Only committed admission constructs
`AdmittedAssetEdit`, an immutable owned definition, mesh, material, commit and
result hash. It survives source/output removal after construction. A historical
attempt outcome alone grants no adoption rights. Rejected input and cancellation
retain source data; new attempts resume prepared publication while preserving
old terminal outcomes. Other affected descendants remain byte/lineage checked
and explicitly unqualified.

## Generated static payload profile v1

The receipt owns separate geometry and material product bytes. Each document is
strict JSON ≤64 KiB, nesting ≤16, with duplicate keys/raw NUL/unknown fields
rejected. Positive selected product/revision and exact world epoch/pose revision
are checked before interpreting arrays. The definition has arbitrary units, one
`GENERATED_PBR` material, no LOD or collider. No captured radiance/albedo conversion
is supported. Sources remain declarations; this path is not an inference worker
or capture/evaluation-role freeze.

Both payloads contain `schema_version: 1`, `product_id`, `revision`, `world_epoch`
and `pose_revision`, plus exactly these fields:

| Payload | Fields / checks |
|---|---|
| Geometry | `kind: GENERATED_STATIC_MESH`; `frame: GEOMETRY_LOCAL_RIGHT_HANDED_Y_UP`; `surface: OPEN_SURFACE_ALLOWED`; positive `variant_id`; `positions`, `normals`, `uv`, `triangles` |
| Material | `kind: GENERATED_CONSTANT_PBR`; exact `geometry` product/revision/content hash; `brdf: METALLIC_ROUGHNESS_GGX`; `color_space: LINEAR_SRGB`; flat tangent `normal: [0,0,1]`; `base_color`, `metallic`, `roughness`, `emission`, `occlusion` |

Geometry has 3–512 finite vertex triples, equally many unit normal triples and
two-component UVs in [0,1], and 1–512 indexed triangles. All vertices are used;
indices are in range, repeated indices/duplicate faces reject, cross products
must be finite/nonzero, winding must agree with every corner normal and UV area
must be nonzero. Bounds use actual pivoted positions. Geometry dependencies are
exactly definition sources; material dependencies are exactly its geometry,
so geometry changes invalidate material identity as well as both sidecars.

Base color and metallic/roughness/occlusion are finite [0,1]; emission is finite
[0,64] in linear sRGB. These are explicit payload ranges, not measured light or
quality thresholds. Material support is a constant one-texel field. Texture maps,
normal perturbations, additional variants and measured/estimated/captured profiles
are unsupported. Generated scalar values do not prove physical material recovery,
BRDF rendering or changed-light fidelity.

Exactly selected provenance sidecars cover the actual face count and one material
texel (1×1). Only generated/manually edited regions are admitted in this profile;
observed/inferred support is unsupported. This permits explicit draft edits
without claiming observed-anchor protection or actual surface correspondence.
Self-intersection/manifold closure, UV packing/seams, complete geometry products,
material fit/rights and runtime/export qualification remain open. See the
[validation report](../validation/2026-10-25-asset-definition-edits.md).
