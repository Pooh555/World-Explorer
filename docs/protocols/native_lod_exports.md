# Explicit native LOD export and preview

The opt-in `NativeExportRequest.m_lods` selects exactly one level for every asset
definition. Level zero is the original runtime mesh; levels 1–8 are the ordered
[privately admitted LODs](native_lods.md). Missing, duplicated, foreign or
out-of-range selections refuse. All copies of a definition use the same selected
level. This is explicit packaging/preview selection, without automatic distance
selection, streaming, a simplification worker or AP8 qualification.

## Additive package profile

Manifest schema **3** / `WE_NATIVE_LOD_GLTF_V1` preserves the existing 2-definition,
4-material, 64-instance, 64-file, 8 MiB and 1 MiB/file limits. All admitted levels
are compiled and checked against the independently selected conversion limits,
even when not selected. Their exact geometry/UV/material assignments and original
face IDs retain the existing [native conversion rules](native_exports.md).
Certificates prove zero surface displacement before conversion; float32 loss is
measured separately after pivot baking and every instance's world transform.

Each definition has `assets/<id>/lod-<level>.gltf` and
`geometry-lod<level>.bin`. Texture PNGs are shared across levels and instances.
`asset.gltf` is a byte-identical alias of the selected level document. The assembled
`environment.gltf` expands that same selected mesh/material definition once and
references the same buffers/textures; all copies share it. Unselected levels
remain separate core glTF documents in the package, available through the
companion mapping. No cross-document node references or implicit glTF LOD
extension are invented, and no scene-specific remesh/material refit occurs.

The authoritative `lods.json` (schema 1 / `NATIVE_LOD_SELECTION`) pins receipt and
instance hashes, `EXPLICIT_PER_DEFINITION_ALL_INSTANCES`, and each definition's
selected level/assembly mesh index. Its complete level table records actual
geometry product, geometry/document paths, converted hash/errors, material groups,
source-face IDs, original face-provenance reference and LOD0 correspondence.
LOD0 maps each face to itself; each reduced face lists every original face it
replaces. Each reduction also includes the original receipt-owned certificate at
`lods/<asset_id>-<level>.json`, with exact product/path/SHA. Existing complete
per-LOD provenance sidecars are copied unchanged. The manifest's `assets` table
describes the selected resources and pins the base geometry reference.

The manifest pins `lods.json` with `lod_selection` containing `path`, `sha256`,
`scope`, `structure_geometry_scope`. The closed inventory hashes every file.
Optional structural bindings keep their original bytes and are explicitly
**LOD0-bound**; selecting another mesh never reinterprets graph face IDs. Original
schema-1 textured and schema-2 structured profiles retain their bytes and LOD
refusal unless this new selection is explicitly supplied.

## Native display, ownership and limits

The existing private package compiler loads the selected resources into ordinary
schema-6 PBR bundles. No binary field/layout is changed. The source manifest and
selected individual glTF remain pinned in each chunk; validation checks the new
profile/schema, selected level, companion/certificate inventory pins and actual
converted geometry/material bytes. Full unselected meshes, certificate contents
and provenance masks remain in the portable package; runtime bundles retain their
source pins, following the existing structural/channel provenance scope. These
bindings provide integrity, not authenticated production or physical evidence.

One shared geometry/image set per definition serves all instances. Selection
changes use existing scene transactions and renderer update lifetimes. The native
Project panel displays the selected level for each definition, alongside the
existing draft/synthetic labels and preview lights. `we_viewer --scene PATH`
opens a compiled bundle normally. This API slice does not add a live LOD picker.

Package admission rebuilds the full closed package from independent private
inputs, including unused levels and original certificates. Existing immutable
publication, idempotent operation replay, cancellation recovery, source protection
and selected-commit reload apply unchanged. Failed admission preserves caller data.
Real producers, general decimation, measured silhouette/runtime budgets, ordinary
Godot import and AP gates remain unqualified.
