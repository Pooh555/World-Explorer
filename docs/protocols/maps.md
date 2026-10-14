# Persistent map snapshots and retry receipts

`map_store` retains its in-memory constructor and adds `open_persistent` with
`persistent_map_config`. All commits/reset/import calls serialize; acquired
snapshots are immutable and survive later commits, deletes, resets and store
closure. File IO is synchronous: producers call it off the render thread. Stop
tokens support cancellation before publication. Producers keep their input on
failure. Shared record validation also rejects invalid tracking/covariance and
world-transform overflow in the in-memory path.

## Storage and recovery

A store is a local directory with `writer.lock`, `manifest.json` and `assets/`.
The lifetime OS lock is nonblocking: a second cooperative writer receives
WOULD_BLOCK. POSIX uses `flock` on a regular, nonsymlink descriptor; Windows uses
an exclusive native handle. Never unlink the lock file. These locks and hashes
are integrity/cooperation mechanisms, not authentication against a malicious
owner who can alter the directory.
The Windows handle also prevents concurrent read opens of the held lock file.
Preservation observers inventory its regular entry while hashing manifests/assets;
they must keep writer ownership active and distinguish the exact lock path from
unrelated caller files with the same name.

Explicit initialization requires a fresh/empty directory and existing parent.
An existing valid store is loaded rather than reset. Missing/corrupt manifests,
linked assets or unreferenced assets without a manifest do not trigger an
implicit blank store. Initial creation also flushes the new directory entry on
POSIX. Native Windows/macOS power-loss qualification remains pending.

The manifest v1 has exactly `schema_version`, `epoch`, `revision`, `sha256`,
`bytes`; numbers are unsigned integers. It references
`assets/checkpoint-SHA256.fb`, derived from a checked lowercase 64-digit hash,
never a caller-controlled relative/absolute path. Recovery checks bounded JSON,
file size and SHA-256 on the bytes parsed, FlatBuffers structure, schema/digest/
coordinate/scale profiles, native records, dependencies and receipt ordering.
Unreferenced assets and leftover staging directories are ignored for selection;
there is no scan for a newer or older checkpoint and no corruption fallback.

[`checkpoint.fbs`](../../schemas/map/checkpoint.fbs) uses `WEMP`, schema 1 and
transaction digest profile 1, with the shared double-precision canonical pose
records. It supports empty maps, multiple calibrations/submaps, original
PTS/time bases, capture times, pose revisions and invalidation IDs for deleted
frames. Monocular scale remains ARBITRARY; no measured gravity, floor or collision
is inferred. Checksums detect corruption, not arbitrary producer dishonesty.

Each checkpoint stores a contiguous tail of acknowledged operation ID/digest/
revision receipts. Defaults: 1024 receipts (configurable 1–65536). Repeating the
same retained operation returns its original revision despite its old base;
changed content returns DUPLICATE_OPERATION. Expired retries request snapshot
resynchronization through WRONG_BASE; retry retention is bounded. Epoch reset
clears records/dependencies/receipts and persists the new epoch at revision zero.
Recovery rejects a revision-zero checkpoint that retains an initial canonical
dependency, even when its records and receipts are empty and the dependency is valid.
Reopening with a receipt budget smaller than the stored tail fails explicitly
rather than truncating it. Digest profile 1 hashes fixed little-endian 64-bit
fields, IEEE double bit patterns, count-prefixed record arrays, image digests and
optional initial canonical dependency; no native ABI bytes are serialized.

Default budgets: 256 MiB/checkpoint, 1M frames/poses/invalidations, 4096
calibrations/submaps, 8 GiB retained assets and at most 4096 asset entries. File
and record budgets can be lowered; hard format limits remain explicit. Admission
counts retained/orphan regular assets and reserves room for the next entry
before writing it, so a successful commit stays reloadable at the boundary.
Old assets are retained; there is no automatic garbage collection or unlimited
append growth. An exhausted store returns RESOURCE_EXHAUSTED without changing
the manifest. Encoded/disk limits do not certify peak process memory/performance.

## Publication and initial solve provenance

Publication stages into an owned sibling directory, closes and flushes the
asset, then atomically hard-links it without overwriting an existing entry.
Existing matching assets are checked before reuse. Asset paths must be regular
nonsymlink entries. After all candidate allocations succeed, the manifest is
staged/closed/flushed and atomically replaced (POSIX rename / Windows
MoveFileExW with replace/write-through); POSIX also flushes its parent directory.
Only then are snapshot and receipts installed in RAM with nonallocating swaps.
Before a new publication, the writer verifies that its manifest was not changed
outside the instance. Failed ordinary staging/admission/rename preserves the
prior manifest and RAM. Cancellation after a complete unreferenced asset can
leave that harmless orphan. Ordinary temporary files are removed; abrupt-process
staging leftovers are not mistaken for committed data.

A post-rename directory-flush failure is explicitly ambiguous: the store returns
RESYNC_REQUIRED and refuses further mutation until close/reopen. The old acquired
snapshot remains alive; it is not proof that the disk still contains the old
manifest. Power-loss and real ENOSPC/device failure qualification are pending.

`import_pose_asset` seeds epoch zero/revision one from a checked canonical file
and an explicit operation ID. The dependency is copied to
`assets/poses-SHA256.weposes` before checkpoint publication. Its hash is stored
as `initial_pose_asset_sha256`, verified again at recovery and retained across
corrections/deletions; reset clears the dependency. It identifies the initial
solve, not a claim that later corrected poses came from the unchanged backend.
External video/job/asset paths are unnecessary for map recovery; no pixels or
optimizer data are copied into the map. The importer never reruns reconstruction.

Shared application commands and CLI:

```sh
build/dev/we_process init-map NEW_MAP_DIRECTORY
build/dev/we_process import-map INPUT.weposes SHA256 MAP_DIRECTORY
build/dev/we_process inspect-map MAP_DIRECTORY
```

CLI import uses operation 1 and is idempotent after restart within its receipt
window. Inspection takes the writer lock briefly while validating/reloading and
returns an immutable snapshot. A producer can use `acquire_snapshot` within an
open store while consumers retain previous snapshots. Canonical interchange is
separate from recovery checkpoints; it contains no operation receipts. Native
import also accepts a checked in-memory canonical asset, serializing and retaining
it through the same byte/count admission and publication path. File import keeps
the exact supplied bytes and hash; it never substitutes a re-encoding.
Initial import requires epoch zero/revision one and no invalidated frames. A
complete dependency may remain as an unreferenced asset if later transaction or
checkpoint admission fails; the committed manifest, snapshot and retry receipts
remain unchanged. Correcting the admission failure permits an authentic retry.

`prepare-poses POSE_JOB VIDEO MAP_DIRECTORY [--decode-stage DIR]` verifies the
retained job and source, then creates/reopens the initial durable map without
running a decoder, solver or optimizer. It rejects overlapping inputs and
different/advanced dependencies, retaining operation 1 across restart. The shared
[reconstruction coordinator](jobs.md) now performs this preparation before fitting
and keeps exclusive map ownership through dependent scene publication. Full
video preflight/automatic solve chaining remain M1 work; batch command/control
now uses the checked [worker protocol](worker.md).
