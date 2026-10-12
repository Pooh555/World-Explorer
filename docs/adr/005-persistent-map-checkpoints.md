# Persistent map checkpoints

2026-10-12. Accepted for the M1 development profile; release/platform qualification
is separate.

Persist the immutable map snapshot and bounded operation receipts in one
versioned FlatBuffers checkpoint. Publish its closed, checksummed immutable asset
first, then atomically replace a small JSON manifest. One cooperative OS writer
lock covers the store lifetime; in-process commits serialize on the existing
map mutex. Retain old assets and impose explicit byte/count limits instead of
silently deleting snapshots or retry history.

An append-only transaction log was considered. Full checkpoints avoid a second
transaction replay/parser implementation and make recovery independent of a
partially appended tail. They cost snapshot encoding and disk space per revision;
this is acceptable for the small batch M1 map, not a live performance claim.
Shared record validation/serialization also serves canonical pose interchange.
The initial canonical dependency is copied and verified so job/source paths do
not become mandatory for reopening a map.

Allocate candidate snapshots/receipts and stage files before replacing the
manifest; install RAM state only after successful publication. Ordinary failures
preserve the previous manifest and caller data. A manifest rename followed by
failed durability flush has an ambiguous outcome: return RESYNC_REQUIRED, reject
further writes and require close/reopen. Do not report it as an ordinary rollback.

Revisit checkpoint granularity/retention after measured large-map costs in M5,
or when a qualified live workflow is requested. Do not change consumer revision,
epoch, dependency or retry semantics to optimize storage. Native power-loss,
ENOSPC and Windows/macOS qualification remain separate evidence requirements.
