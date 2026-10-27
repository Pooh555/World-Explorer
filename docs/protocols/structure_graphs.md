# Bounded structure graphs v1

[`structure_graph.hpp`](../../include/world_explorer/application/structure_graph.hpp)
adds owned partial-patch topology and declared spatial relations in `application`.
The request supplies an independent product selection, maximum plane/relation
residuals and private geometry/capture/receipt handles at the expected epoch/pose
revision. Both handles must belong to the original receipt; capture must equal
geometry's original capture product. Root dependencies are exactly the selected
geometry product, and the complete inference dependency closure passes the
capture firewall. Capture remains coordinator metadata, outside the worker DAG.

This is an additive CPU profile. A0/AP0 remain **IN_PROGRESS**. It adds no
structure estimator, closed room, measured scale/gravity, physical opening or
load-bearing support, collision permission, renderer transaction or AP/Q/N token.
The existing geometry/material/static encodings remain unchanged. Synthetic source
and transitive declared manual ancestry remain explicit in the private handle.

## Root and budgets

Strict JSON keys are `schema_version=1`, `kind=STRUCTURE_GRAPH`, `world_epoch`,
`pose_revision`, `recipe_sha256`, `geometry`, `capture`,
`frame=GEOMETRY_LOCAL_ARBITRARY_UNITS`, `scope=PARTIAL_PATCHES`, `surfaces`,
`openings`, `rooms`, `relations`, `manual_ancestor`. Product references retain
ID/revision/content SHA; metadata pins must equal original independently admitted
inputs. Duplicate/unknown fields, NUL, excessive depth and nonfinite numbers fail.

The root is at most 256 KiB: 1–32 surfaces, 0–32 rooms, 0–64 openings and 0–128
relations. Selected faces are distinct across all patches, bounded by the existing
8,192-face geometry profile. Each surface has 1–16 loops, at least three vertices
per loop, with at most 1,024 boundary vertices across the graph. Used local
coordinates and plane offsets have absolute value at most 1,000,000. Independent
residual limits are finite [0,1,000,000]; these are arbitrary-unit admission
policies, not AP/Q acceptance thresholds. Unselected geometry remains outside this
partial graph; no complete room/scene coverage is claimed.

## Native patches and loops

Surface keys are `id`, `kind`, `patch`, `faces`, `loops`, `plane`, `thickness`,
`coverage`. IDs are positive and unique within their typed surface namespace;
opening and room IDs have separate types/namespaces. Surface kinds WALL/FLOOR/
CEILING/OTHER are semantic declarations and never establish physical gravity.
Faces index actual admitted runtime triangles. A patch must be connected by
shared edges. Loops use runtime vertex indices, with implicit last→first closure;
repeating the first vertex at the end is invalid. All boundary vertices are
distinct within a surface. The complete directed loop-edge set must equal exactly
the selected triangle union's boundary; internal diagonals, missing holes and
reversed/duplicated edges fail. Closed boundaryless patches are outside this profile.

PLANE requires `plane={normal:[x,y,z],offset_units,residual_bound_units}`. Normal
squared length differs from 1 by at most 1e-8 and agrees with every native face's
winding. The actual maximum absolute vertex-to-plane equation residual is
computed and retained; it must not exceed the declared bound or independent
policy. Dominant-axis projection follows the declared normal's sign. Loop 0 has
positive signed area; remaining loops have negative area, lie strictly inside
the outer loop and do not nest. Collapsed/backtracking edges, nonadjacent touches,
self-crossings and inter-loop crossings fail. Predicates use bounded double
arithmetic, without an exact-predicate/self-intersection certificate for the
entire triangle surface. Coplanar coincident triangles and general mesh quality
remain separate geometry obligations.

MESH requires `plane=null`. It retains the same native connected-face and complete
oriented boundary checks, without planar polygon/void claims. It can represent a
nonplanar patch; projected openings are supported only on PLANE patches.

`coverage` is exactly five integer counts in UNKNOWN/OBSERVED/INFERRED/GENERATED/
MANUALLY_EDITED order, independently recomputed from original geometry face support.
It cannot turn inferred geometry into observed structure. Native face support is
still not proof of physical surface correspondence or opening free space.
`thickness={source,value_units}` supports UNKNOWN with null value, or
DECLARED_INFERRED/DECLARED_GENERATED with finite value (0,1,000,000]. These are
explicit declarations; measured/observed thickness is unsupported in this profile.
The original recipe pin and graph retain their source/algorithm/lineage bindings.

## Rooms, openings and relations

Room keys are `id`, `boundaries`, `contains`. Boundary surface IDs are distinct,
nonempty and present in this graph; a surface belongs to at most two room boundary
sets. These sets may be incomplete and do not establish an enclosed volume.
Each containment row has `surface_id`, `residual_bound_units`, with no duplicate
target in one room. Bounds come from all native vertices of its declared boundary
surfaces. The actual containment residual is the maximum Euclidean distance of
target vertices outside that AABB. Declared bounds must cover actual residuals
within independent relation policy. AABB containment is only a declared spatial
relation; it is not physical room containment or an occupancy certificate.

Opening keys are `id`, `surface_id`, `loop`, `rooms`, `class`, `confidence`.
Each positive opening ID selects one distinct planar hole (loop ≥1); every planar
hole must have one opening. Its polygon is that exact native loop, without a
second independently drifting polygon. `rooms=[first,second]` contains a valid
first room and a distinct second room or explicit exterior null. The set must
equal the host surface's room boundary memberships; this defines adjacency.
Only UNKNOWN (null confidence), INFERRED or GENERATED (confidence [0,1]) are
supported. Observed/manual void claims require further independently bound space/
action evidence and are rejected here. Observed surrounding triangles do not
establish that a hole is physically open. Manual ancestry still propagates from
the original dependency graph independently of opening class.

Relation keys are `kind`, `source`, `target`, `source_vertex`, `target_vertex`,
`residual_bound_units`. CONTACT/SUPPORT link distinct existing surfaces; each
native vertex anchor must occur in its selected patch. Actual Euclidean anchor
gap is computed and must be within declared and independent bounds. Duplicate
kind/source/target rows fail; CONTACT requires ascending surface IDs. Directed
SUPPORT edges must be acyclic. No closest-surface search, intersection, stability,
force direction, gravity or load calculation is implied by these anchor checks.

## Ownership and recovery

The private result owns normalized typed surfaces/openings/rooms/relations,
computed residuals, original root bytes, receipt/context and geometry product pin.
Caller/source lifetime cannot change it. Publication revalidates the original
receipt/context and writes one `structure-ID-REV` record through the existing
immutable store, protecting the receipt source tree. Cancellation leaves the
previous manifest intact; matching operation retries resume idempotently.
Conflicting operations, stale bases/context and protected-root overlap fail.
Reload freshly admits the independent inputs, requires the selected current
commit and checks the committed key/content hash. This is not asset adoption or
a live scene revision. Full structure production and quality remain open.

The separate [native structural adoption path](native_structure.md) now binds
these private graphs to exact native geometry/definition/instance snapshots and
publishes original graph roots with their metadata atomically. It preserves this
partial, declaration-only scope; graph admission alone still authorizes no scene
mutation or physical-support claim.
