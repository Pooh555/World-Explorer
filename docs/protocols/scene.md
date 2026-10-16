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
and draws UI. This is not linear HDR conversion or relightable albedo; a hybrid
mesh pass is still absent. Precision/profile rationale: [ADR 006](../adr/006-fp32-capture-compositing.md).

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

An unpacked `.wexplore` bundle uses JSON manifest schema 1, SHA-256-addressed
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
