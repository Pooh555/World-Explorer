# Implemented scene and update contracts (development schema 1)

World coordinates are right-handed, +Y up. Units remain arbitrary unless measured.
Optical camera axes are +X right/+Y down/+Z forward. Transform math uses doubles,
world-from-camera, wxyz quaternions and integer pixel centers. The COLMAP adapter
subtracts 0.5 from principal points and rotates the world by Rx(pi); this conventional
rotation is not a measured gravity alignment.

Runtime Gaussians contain float means, positive decoded scale, normalized wxyz
rotation, opacity in [0,1], SH degree 0–3 and 48 coefficient-major RGB scalars.
SH and alpha compositing use the captured display-encoded domain. This initial
renderer has no linear HDR intermediate or hybrid mesh pass yet.

Synthetic bundles declare provenance `source_kind=synthetic_diagnostic` and a named
`fixture`. The viewer classifies the active snapshot using this explicit marker;
without `source_kind`, earlier reference bundles' `diagnostic` text beginning
`synthetic ` remains supported. Paths and arbitrary fixture names never imply
classification. A present `source_kind` is authoritative.

`scene_store` copies mutable producer chunks into immutable revisions. Transactions
have scene ID, world epoch, base/new revision and idempotent operation ID. Creates,
replacements, deletes, transforms and geometry invalidation publish atomically.
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

Publication writes and flushes immutable assets before replacing the manifest.
The old scene survives rejected publication. Local filesystem atomic rename and
fsync semantics are assumed; network filesystems are unqualified. Package writes
require exclusive writer ownership. Never write into an attacker-mutated directory.

`renderer_update_sink` is mutex-protected, multi-producer/single-consumer. Failed
enqueue preserves caller data; accepted updates move the immutable snapshot.
Duplicates are checked by metadata and content hash within a bounded 256-operation
window. Admission charges pending, visible and not-yet-retired payloads. The GPU
adapter additionally budgets staging, old/new buffers and sort scratch.
Cancellation drops queued work; already consumed GPU work finishes safely.
Accepted → assets available → uploaded → render-visible → retired is explicit.
Visibility means submitted atomically for rendering, not that display scanout is
complete. Timeline completion governs old resource retirement. Full snapshots use
this same path for initial load/resync. No reconstruction GPU pointer crosses it.

Current limitations: validation/content hashing run at admission, full-scene uploads
are not split across frames, snapshots require producer immutability, resize waits
for device idle, and device loss reports failure rather than automatically rebuilding.
P6 and full M2 qualification are therefore incomplete.
