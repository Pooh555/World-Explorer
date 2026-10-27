# Native geometry/material asset adoption

[`native_assets.hpp`](../../include/world_explorer/application/native_assets.hpp)
adds an application-level draft adoption path over existing privately admitted
[geometry](geometry_products.md), [material](material_products.md), optional
[completion](completion_products.md) and [definition/instance](asset_definitions.md)
contracts. It preserves their encodings, actual arrays, channel bytes and original
receipt. It neither converts products to generated-static v1 nor relabels captured
radiance as PBR. A0/AP0 remain **IN_PROGRESS**; this API supplies no worker,
renderer transaction, export, real completion/relighting or qualification token.

## Independent inputs and binding

`NativeAssetsRequest` selects the original receipt, epoch/pose, 1–16 definitions
and 1–64 scene instances. Each asset independently supplies typed definition
metadata, provenance selections, one geometry handle, 1–16 material handles, an
optional completion handle and the original source-instance ID. Definitions still
pass existing metadata admission. Every native handle must use the exact receipt
and current context; material geometry/product/variant/lighting references must
match. The set of actual per-face material-group IDs must exactly equal the
selected bindings: no missing, duplicate or unused group. The instance material
variant remains checked by existing metadata admission; this path does not bake
an override or alter per-face group assignments.

Sources are exactly the geometry's original capture product and native-source
binding, plus the selected completion product when present. Completion requires
its selected geometry, original receipt/context and object→prop or shell→structural
subject correspondence. Rejected candidates cannot substitute. Omitting completion
while retaining its definition source fails. No completion selection makes no
claim about completed geometry; this path does not infer producer history from
visual similarity or require every asset to have a completion product.

Only arbitrary units are supported. LOD/collider and instance room/support/lineage
adoption are explicitly unsupported. Structural identity remains metadata; no
room closure, support, gravity, measured scale or walking authority is inferred.
Rights are the existing exact opaque product binding, without legal clearance.

## Full face and texel binding

The independently selected face sidecar must cover exactly every runtime face
and retain the geometry's computed class counts. Each face's class and source-view
support must equal the native correspondence record, including observed/inferred/
generated/edited distinctions. Every span names exactly the original native-source
binding as its parent; confidence is null because native geometry admission
does not establish a face-confidence value. Source tables bind selected inference
capture/frame/calibration IDs, image products and native pose-asset hashes. Existing
region admission checks complete partitions, direct dependencies and manual
ancestry. Calibration/grid SHA declarations remain metadata, as in material
admission; this path does not recompute intrinsic/grid checksums or validate
physical correspondence. All selected sources must be used under region v1.

Definition v1 requires one material-root sidecar per binding. This profile accepts
that summary only if its domain, full source table, ancestry and complete spans
exactly match **every** admitted channel's provenance. Span boundaries, classes,
confidence, parents, support and ancestry must all match, even when aggregate
counts alone would agree. No lossy merge is invented. Different channel partitions
or evidence fail as unsupported in schema 1. Definition
[schema 2](asset_channel_provenance.md) instead binds every original channel and
its selected sidecar, preserving independent masks and evidence without a root
summary. Native adoption matches those bindings to the admitted material products.
The returned private handle retains every original channel and sidecar, fit-light/exposure input and geometry correspondence; the root summary
does not replace them. Existing estimated/captured lighting restrictions apply.

Manual ancestry propagates through existing receipt, definition and instance
admission. Edited summaries also need the existing region-v1 target-action
declaration. This reader verifies declarations, not a real human act, journal
authenticity or permission to edit. It adds no alternate manual mutation command;
existing action-bound draft-edit/attempt paths remain separate and unchanged.

## Pivot, bounds and placement

Bounds must exactly equal the AABB recomputed from all runtime vertices after
the declared rigid asset-from-geometry pivot. Semantic pivot policy and orientation
ambiguity retain their existing declarations; numerical placement checks do not
prove a physical attachment or support plane. Pivot, original placement and source
quaternion squared-length errors are bounded by 1e-12. The selected visible
source instance must use this exact definition. Source/instance scale differs by
at most 1e-12 relative; composed orientation axes differ by at most 1e-10 per
component independently of world translation. Origin, frame probes and every
actual vertex must preserve native world placement within 1e-10 times the maximum
of one and the absolute source-world coordinate components. Nonfinite transformed
positions fail. These are numerical admission tolerances in arbitrary units,
without a physical-accuracy claim. Additional instances may be independently
placed copies. Equal payloads never merge distinct semantic asset IDs.

## Owned metadata publication and reload

`publish_native_assets` accepts only the private adopted handle, rechecks original
receipt/context and uses existing [`publish_asset_instances`](asset_metadata_io.md)
to commit all definitions and the instance set atomically. Existing immutable
asset/scene keys, operation intents, blob-first/manifest-last ordering, cancellation,
exact replay, semantic key conflicts and protected source-tree rules apply.
There is no second catalog, mutable head or new persistent schema. Input payloads
are not recopied; this metadata store is not a standalone asset package.

`read_native_assets` freshly validates the complete independent native/metadata
request, requires the selected current physical store commit and checks exact
canonical bytes for every definition and the instance set. Historical semantic
revisions remain readable under a newly selected current store commit; the API
does not select a live-scene head. Missing/foreign/stale committed metadata fails.
Adoption remains a draft result. The separate
[native textured glTF profile](native_exports.md) converts these private handles
into shared portable resources. The [native PBR scene compiler](native_pbr_scene.md)
consumes admitted exports for bounded immutable runtime resources through the
existing scene-update path and diagnostic native viewing. Ordinary Godot import,
producer execution, real A1 output and AP qualification remain open.

The separate [native structure binder](native_structure.md) now joins admitted
graph elements to exact structural definitions and all their copies. Its explicit
record retains typed graph identities and placement; opaque instance room/support
refs remain unsupported here. Real structure production and physical support stay
unqualified.
