# Native structural graph bindings v1

`inspect_native_structure` joins private [native assets](native_assets.md) and
[admitted structure graphs](structure_graphs.md) at the original receipt and
expected epoch/pose. This bounded A0 path binds declared partial topology to actual
adopted geometry. It does not estimate structure, certify a physical opening,
closed room, gravity or support, enable collision/walking, or qualify real AP gates.

## Independent selection and identity

The request explicitly selects each structural definition reference, private graph,
complete surface/opening/room/relation identity sets and all instances of that
definition. Every STRUCTURAL definition in the selected native scene needs exactly
one binding; PROP definitions cannot receive one. Graph geometry must equal the
adopted geometry product exactly, including revision/content hash. A graph for an
older source mesh cannot be silently reused for completed or changed geometry.
Receipt, context and independent graph residual-policy pins remain exact.

Selections retain all graph elements, rather than discarding an opening or support
edge. Surfaces remain the graph's actual triangle patches, with original directed
loops and computed geometry-derived coverage; openings retain their original hole,
room adjacency and evidence class. Relations retain kind/source/target IDs and
original anchors/residuals. This binds partial graphs to a **whole definition**;
it does not extract independent per-patch meshes or establish that unselected
geometry belongs to the graph. Existing graph-v1 limits and physical-evidence
restrictions remain in force.

All copies of a structural definition must be selected and visible. Element
identity is qualified by scene, instance, graph reference and typed element ID.
Thus copied shells can retain room 21 without merging their room identities.
Distinct semantic assets remain distinct even when their geometry/graph bytes
match. Shared exact graph roots are stored once. Existing opaque instance `room`
and `support` product references remain unsupported by native asset adoption;
this join does not promote them into validated element IDs. Prop membership,
cross-graph support and instance lineage adoption remain outside this profile.

The graph frame is `GEOMETRY_LOCAL_BEFORE_PIVOT`. Each recorded `world_from_graph`
is the positive uniform instance transform composed with the definition's rigid
asset-from-geometry pivot. All transformed mesh vertices must remain finite. The
original source placement keeps native adoption's compensation checks. Plane and
relation residuals remain in original arbitrary geometry units; scene/world scale
does not become measured accuracy. A SUPPORT anchor gap is not a stability proof.

## Immutable publication and reload

The canonical `NATIVE_STRUCTURE_BINDINGS` schema-1 record binds scene ID/revision,
canonical instance-set SHA, original receipt/context, exact definition/geometry/
graph references, complete identity sets, graph policy limits and per-copy frames.
It labels `DECLARED_PARTIAL_PATCHES`. Ordering is canonical by definition and
identity; original graph bytes stay untouched. The aggregate manual flag includes
both native asset ancestry and declared structure-graph ancestry. A graph edit
never relabels geometry/material regions or authenticates a human action.

`publish_native_structure` atomically publishes original `structure-ID-REV` roots,
`native-structure-SCENE-REV`, and the existing asset/scene metadata records through
the same immutable store. Source paths are protected; blobs precede the manifest.
Exact retries are idempotent, cancellation preserves committed snapshots, and
conflicting bytes for a semantic key fail. A binding-policy or graph change needs
a new scene revision even if the instance bytes would otherwise be unchanged.

Reload freshly admits independent inputs, requires the selected current physical
commit and matches every selected graph/binding/asset/scene record. Historical
revisions remain readable from a newly selected current commit. The private result
owns the original graphs and native asset handles; caller mutation cannot redirect
it. Limits are 1–16 bound structural definitions/graphs, ≤64 visible instances,
≤1 MiB binding JSON and ≤256 KiB per original graph. Existing metadata batch/store
budgets apply. This does not create a second catalog or mutable scene head.

## Explicit structured export and runtime scope

`NativeExportRequest::m_structure` selects the private binding alongside the exact
native asset/instance snapshot. It must match the source receipt and instance-set
hash. This produces **manifest schema 2 / WE_NATIVE_STRUCTURED_GLTF_V1**, keeping
the existing geometry/material conversion and ≤2-definition/≤4-material/≤64-file/
8 MiB export limits. Geometry, textures, individual asset documents, catalog and
scene resources remain identical to the corresponding ordinary native export.
The assembled document additionally labels the structured profile/manual flag.

`structure/bindings.json` is the canonical join; `structure/ID-REV.json` contains
each exact original graph root. The manifest pins the binding and closed file
inventory. Primitive source-face maps and the pivot/frame declarations remain
authoritative for interpreting graph faces against compiled glTF resources.
Declared graph manual ancestry propagates to the assembly/manifest aggregate.

Native PBR compilation accepts this explicit profile/version and preserves it in
scene provenance. Existing schema-6 bundles retain the exact export manifest and
structure-sidecar hashes; they do not embed graph roots or add room/provenance UI.
Their validator rejects inconsistent profile/version/scope/binding pins. Rendering
still consumes the same immutable mesh/material/instance update path. No shader,
bundle encoding or old schema-1 export bytes change. Ordinary asset-only export
remains available without claiming structured semantics. Real A1 structure,
completion, ordinary Godot import and full quality/rights closure remain open.
