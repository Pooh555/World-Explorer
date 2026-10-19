# Implemented scene and update contracts (development schema 1)

World coordinates are right-handed, +Y up. Units remain arbitrary unless measured.
Optical camera axes are +X right/+Y down/+Z forward. Transform math uses doubles,
world-from-camera, wxyz quaternions and integer pixel centers. The COLMAP adapter
subtracts 0.5 from principal points and rotates the world by Rx(pi); this conventional
rotation is not a measured gravity alignment.

Runtime Gaussians contain float means, positive decoded scale, normalized wxyz
rotation, opacity in [0,1], SH degree 0–3 and 48 coefficient-major RGB scalars.
SH and alpha compositing use the captured display-encoded domain, with negative
evaluated colors clamped to zero and values above one preserved until presentation.
The raster path accumulates in a queried FP32 attachment, then clamps once to SDR
and draws UI. This is not linear HDR conversion or relightable albedo. The
separate observed-mesh diagnostic below does not implement hybrid occlusion. Precision/profile rationale: [ADR 006](../adr/006-fp32-capture-compositing.md).

Synthetic bundles declare provenance `source_kind=synthetic_diagnostic` and a named
`fixture`. The viewer classifies the active snapshot using this explicit marker;
without `source_kind`, earlier reference bundles' `diagnostic` text beginning
`synthetic ` remains supported. Paths and arbitrary fixture names never imply
classification. A present `source_kind` is authoritative.

`scene_store` copies mutable producer chunks into immutable revisions. Transactions
have scene ID, world epoch, base/new revision and idempotent operation ID. Creates,
replacements, deletes, transforms and geometry invalidation publish atomically.
Unknown operation kinds and scale statuses are rejected before publication;
rejected transactions do not consume their operation ID or alter retry receipts.
Transforms invalidate derived geometry and record the source pose revision.
Wrong base/epoch fails without changing visible state. A bounded replay window
returns a current snapshot with base/new revisions; older requests need full resync.
`map_store` applies the same immutable transaction pattern to observations, poses
and calibrations; [persistent checkpoints](maps.md) also preserve revisions and
retry receipts across restart.

An unpacked `.wexplore` bundle uses JSON manifest schema 1 for splat-only snapshots
and schema 2 when any chunk carries a mesh, with SHA-256-addressed
little-endian binary indexes/chunks, and an explicit provenance file. The loader
validates byte/count limits, conventions, checksums, path containment, finite
numbers, quaternion normalization and covariance scales before scene admission.
Chunk payloads are presently uncompressed. No general archive extraction is offered.
The controlling byte layout is the paired reader/writer in `bundle.cpp`; this
development format has not passed cross-platform schema qualification.

The gsplat PLY interchange reader accepts the explicit gsplat-1.5.3/srgb ASCII
profile, SH degrees 0–3 and reordered named float columns. Its header byte budget
includes the magic line and all raw bytes, including CRLF line endings. Vertex
and trailing lines are bounded to 16,384 raw bytes each; carriage returns cannot
bypass either limit. Log scales, opacity logits and channel-major SH input are
decoded into the canonical runtime representation before validation.

Publication writes and flushes immutable assets before replacing the manifest.
The old scene survives rejected publication. Local filesystem atomic rename and
fsync semantics are assumed; network filesystems are unqualified. Package writes
require exclusive writer ownership. Never write into an attacker-mutated directory.

`renderer_update_sink` is mutex-protected, multi-producer/single-consumer. Failed
enqueue preserves caller data; accepted updates move the immutable snapshot.
Duplicates are checked by metadata and content hash within a bounded 256-operation
window. New operations check queue/residency capacity before full validation;
validation and hashing run outside the sink mutex, followed by a locked revision
and budget recheck. Producers may call `prepare(batch)` before timed admission:
it validates the full immutable snapshot and builds a SHA-256 fingerprint from
scene metadata and per-chunk content digests. A bounded 4,096-entry weak-owner
cache reuses hashes only for the same still-live immutable chunk. This fingerprint
is for operation-ID comparison; the canonical `scene_content_hash` and bundle
hash are unchanged. `enqueue(prepared)` only checks current metadata, budget,
revision and capacity under the mutex. A budget change after preparation rejects
the attempt for fresh preparation. The legacy `enqueue(batch)` keeps its existing
validation behavior and fast queue-full rejection. A rejected retry remains owned
by its producer. Admission
charges pending, visible and not-yet-retired payloads using the validated visible
ticket's byte count. The GPU
adapter additionally budgets staging, old/new buffers, sort scratch and FP32
framebuffers using actual Vulkan allocation requirements. Temporary staging is
charged while allocating its replacement; insufficient residency fails before
device-memory allocation. `allocated_bytes` counts owned buffer/image allocations,
including retired buffers; `framebuffer_bytes` is its image subset. Driver/internal
pipeline/descriptor/swapchain allocations and other processes are excluded; this
is not a complete or peak VRAM measurement. Framebuffer dimensions identify the
actual render extent. Resize retires old images after device idle and rebuilds
presentation descriptors. Unsupported FP32 attachment blending fails explicitly;
there is no silent UNORM/FP16 fallback or portable-device qualification claim.
Cancellation drops queued work; already consumed GPU work finishes safely.
Accepted → assets available → uploaded → render-visible → retired is explicit.
Visibility means submitted atomically for rendering, not that display scanout is
complete. Timeline completion governs old resource retirement. Full snapshots use
this same path for initial load/resync. No reconstruction GPU pointer crosses it.

Large snapshots keep the previous GPU scene visible while at most 15 MiB of the
replacement payload is copied each render frame. An unchanged immutable chunk
at the same floating origin can copy its existing GPU bytes into the replacement;
new/changed chunks are packed into CPU staging. Chunk ID plus shared immutable
object identity must match; matching generations alone cannot enable reuse.
Source/destination offsets are recomputed in each snapshot's chunk order, so
insertion/deletion may move a reused region. The existing nearby floating origin
is retained until rebasing is required. A changed origin disables resident reuse.

All copies share the byte cap. Resident buffers carry transfer-source usage and
a transfer-write-to-transfer-read dependency before reuse. The replacement
becomes visible only after its final copy and transfer-to-compute barrier are
submitted. Its source scene buffer and staging retire on that completion
timeline. Staging remains full-sized and charged, including during an active
multi-frame upload; residency budgets have not been reduced. The initial scene
uses this same path. Floating-origin rebasing of an already visible scene still
uses a full host copy when no update is active. Diagnostic
`renderer_options.reuse_resident_chunks=false` forces CPU packing for comparison.

GPU storage is selected explicitly when creating a renderer. The default
`gpu_splat_storage::FP32` uses 256 bytes per splat. Experimental
`FP32_GEOMETRY_FP16_SH` uses 160: the same four FP32 geometry/opacity/rotation
vectors followed by 24 uint32 pairs of binary16 SH coefficients. Capabilities
report the selected profile and stride. Canonical scene assets, content hashes,
SH degree and coefficient-major RGB order stay unchanged. Profiles cannot change
within a renderer, so resident-copy offsets always use one declared stride.

Binary16 conversion rounds to nearest with ties to even, preserves signed zero
and supports subnormals. All 48 coefficients must be finite and within ±65504;
overflow fails the update with `INVALID_ARGUMENT`, retaining the visible scene
and caller snapshot. A valid full resync can recover. GLSL
[`unpackHalf2x16`](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html)
decodes low/high halves to FP32; projection, SH evaluation and composition remain
FP32. No FP16 arithmetic/storage feature is enabled. The optional profile needs
Q6 qualification before promotion to the default. Identical GPU geometry bytes
do not establish millimetre accuracy for an arbitrary-scale reconstruction.

Optional completed GPU timestamps include an upload pair around each frame's
host/device-to-replacement copies and final transfer-to-compute barrier. Completed
frame samples also expose `host_copy_bytes` and `resident_copy_bytes`. Frames without
a copy report zero upload time. `interactive_device_idle_waits` counts device-idle calls in the
swapchain refresh path; explicit shutdown/diagnostic idle calls are excluded.

Current limitations: preparation still runs synchronously on its producer, the
legacy enqueue still validates/hashes synchronously, snapshots require producer
immutability, and resize waits for device idle. The renderer
reports typed device loss; the viewer can recreate it from its retained scene,
while physical loss remains unverified. P6 and full M2 qualification are incomplete.

## Observed mesh runtime

Runtime `SceneChunk::m_mesh` holds owned vertices/normals and triangle indices.
`ObservedMeshSource` retains the admitted `.wefusion` SHA-256, typed pose context,
recipe/receipt/manifest pins and exact original manifest/receipt text, including
all coverage and uncertainty denominators. Voxel weights/support/sigma remain in
the pinned original asset; no per-vertex confidence or free-space map is invented.
Absent observed dependencies mean a synthetic diagnostic, requiring explicit
synthetic scene provenance. Observed mesh scenes retain arbitrary scale.

Scene admission checks finite bounded positions, unit-or-zero normals, distinct
in-range indices, source pins/metadata hashes, pose context and aggregate budgets:
4M vertices, 8M triangles, plus the existing payload limit. Mesh metadata counts
toward that byte budget. The store deep-copies mesh vectors with their producer
chunk; mutations through retained producer aliases cannot change a committed
snapshot. Mesh-aware content/transaction/renderer fingerprints cover geometry and
all source dependencies. A reserved zero-ID/version prefix distinguishes mesh
hash streams; splat-only hashes remain unchanged. Invalidated/transformed derived
geometry remains in its immutable historical snapshot but is omitted from mesh
uploads until admitted current geometry replaces it. Initial full load, replay,
replacement and deletion use the existing renderer update sink and retirement path.

For a [certified cleanup proxy](mesh_cleanup.md), `ObservedMeshSource` additionally
retains exact cleanup JSON and its SHA-256. These strings are owned with the chunk;
certificate text is charged to the metadata budget and checked against its pin.
Derived mesh hash streams use prefix version 2; raw mesh/splat hashes remain
unchanged. Application admission replays the certificate against the pinned parent
and current poses; scene validation checks the resulting structural/dependency
contract. Pose invalidation hides both raw and derived geometry.

`load_fusion_scene_request` checks an externally pinned, at-most-1-MiB UTF-8 JSON
request with exactly these fields:

```json
{
  "schema_version": 1,
  "asset": "INPUT.wefusion",
  "asset_sha256": "ASSET_SHA256",
  "manifest_sha256": "ORIGINAL_FUSION_MANIFEST_SHA256",
  "receipt_sha256": "ORIGINAL_NATIVE_DEPTH_RECEIPT_SHA256",
  "recipe_sha256": "FUSION_RECIPE_SHA256",
  "poses": "CURRENT.weposes",
  "pose_sha256": "POSE_SHA256"
}
```

Paths resolve relative to the request. Duplicate/unknown/nested fields, raw or
path-embedded NUL, bad version/hash and invalid fusion/pose dependencies reject.
An optional `cleanup` path and `cleanup_sha256` form the only supported ten-field
variant. Both must be present. It validates the derived certificate before
retaining the simplified geometry and its parent metadata in the same scene path.
The shared application command retains every raw vertex/triangle and selected
frame dependency, bounds the full mesh, and creates an immutable runtime snapshot;
empty real geometry rejects. The CLI and native viewer use it directly:

```sh
build/dev/we_process inspect-fusion-scene REQUEST.json REQUEST_SHA256
build/dev/we_viewer --fusion-request REQUEST.json --fusion-request-sha REQUEST_SHA256
```

Open scene → Observed geometry accepts the same saved request. Its owned loader
thread pins the selected request bytes and checks cancellation, preserving the
current scene on failure. Source-view reset remains tied to its existing capture
inspection metadata; ordinary mesh opening fits full world bounds independently.

`SceneRepresentation::OBSERVED_MESH` selects a two-sided normal-color diagnostic
with indexed vertex/index draws, optical camera projection, Vulkan zero-to-one
clipping and D32 depth testing. Per-frame FP32 color/depth targets are allocated
on demand, budgeted using actual VMA requirements, and reused/replaced only after
that frame slot completes. A checked copy feeds the existing FP32 presentation
buffer and native UI. Mesh and captured appearance are separate representations;
there is no automatic proxy occlusion of its own captured splats or hybrid clip
claim. The shared staging path retains its 15-MiB/frame cap, old-scene visibility,
floating origin, transfer dependencies and timeline retirement for both products.

The viewer labels partial observed surfaces, arbitrary units and unknown gaps;
normal colors are diagnostic, not captured texture, albedo or material recovery.
Walking remains disabled. [Native evidence](../validation/2026-10-19-observed-mesh-view.md)
covers both retained real meshes and synthetic projection/depth/update checks.

## Native mesh bundle persistence

Schema 2 uses the same `WEXIDX01` index records, provenance schema and atomic
publication path as schema 1. Splat-only snapshots still write exactly the old
schema-1 manifest/chunk/index/provenance bytes and retain their content hashes.
Mixed snapshots may contain legacy `WEXCHK01` chunks alongside mesh chunks;
hybrid chunks preserve both splats and mesh geometry. Schema 1 rejects mesh
chunks, and schema 2 requires at least one. Unknown major versions reject.

Schema-2 `geometry` declares `collision_status: UNAVAILABLE`,
`representation: indexed_position_normal`, `encoding: WEXCHK02/WEXMSH01` and
`source_validation: snapshot_dependencies`. Existing appearance conventions
describe any splats present; mesh-only scenes automatically select the normal
diagnostic representation when opened. The manifest/provenance parsers reject
duplicate keys, raw NUL and nesting deeper than eight, within their byte caps.

Each immutable `chunk_<sha256>.bin` is little-endian and uncompressed. A mesh
chunk has magic `WEXCHK02`, then the unchanged legacy header, frame IDs and
240-byte splat records. Its trailing mesh block is:

| Field | Encoding |
|---|---|
| Mesh version | Eight ASCII bytes `WEXMSH01` |
| Counts and source presence | Three uint64: vertices, triangles, observed-source flag (0/1) |
| Observed context, if present | Two uint64: world epoch, source pose revision |
| Observed text, if present | Nine strings, each uint32 byte length followed by exact bytes: manifest, receipt, recipe and pose SHA-256; parent asset SHA-256; original manifest and receipt JSON; cleanup SHA-256 and cleanup JSON |
| Vertices | Six float32 per vertex: position xyz, normal xyz |
| Triangles | Three uint32 indices per triangle |

Raw source cleanup strings are both empty. Synthetic diagnostics omit the source
block and require synthetic scene provenance. Pin strings are at most 64 bytes;
each exact source/certificate document is at most 4 MiB. Encoded aggregate bytes,
remaining aggregate geometry counts, exact record lengths and per-chunk limits
are checked before geometry allocation. The ordinary scene validator then checks
finite values, bounds, normals, indices, combined decoded budgets, source document
hashes and current geometry's epoch/pose dependencies. Trailing bytes reject.

The bundle is an immutable runtime snapshot. It retains authoring provenance and
exact certificates but does not contain the original TSDF voxel planes or pose
asset, or replay their fusion/cleanup proof. Re-authoring/re-certifying requires
the separately pinned parents through `load_fusion_scene_request`. A rehashed
snapshot is not independent quality evidence. No free-space, metric scale,
collision, material or Q5/N2 certification follows from bundle admission.

The shared application command validates the pinned request, source and optional
cleanup proof before publishing to a **new** directory:

```sh
build/dev/we_process compile-fusion REQUEST.json REQUEST_SHA256 artifacts/mesh.wexplore
build/dev/we_process validate artifacts/mesh.wexplore
build/dev/we_viewer --scene artifacts/mesh.wexplore
```

`validate` prints the scene content hash. Ordinary Open scene uses the same bundle
reader and initial immutable renderer update as other scenes; leave Observed
geometry unchecked for bundles. No solver or parent asset is needed to reopen.
Compilation checks cancellation during admission and before publication; atomic
bundle writing then finishes or reports failure. Existing destinations reject
without modification. Low-level `write_bundle` still supports transactional
replacement: assets flush before manifest replacement, and a pre-publication
failure leaves the previous manifest and acquired readers usable. Unreferenced
assets/partial new directories remain for inspection; this is not production job
resume or crash/reap qualification. See [actual round trips and native views](../validation/2026-10-19-mesh-bundles.md).
Bundle schema 1 explicitly rejects mesh snapshots before writing anything, rather
than dropping geometry. Persisted hybrid bundles, baked-color export, measured
geometry and collision remain separate work; local cleanup uses the contract above.
