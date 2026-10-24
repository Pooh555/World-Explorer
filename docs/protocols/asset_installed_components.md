# Installed asset components v1

[`installed_components.hpp`](../../include/world_explorer/asset_catalog/installed_components.hpp)
adds a read-only filesystem observation to [component byte evidence](asset_component_evidence.md).
A0/AP0 remain **IN_PROGRESS**. No installer, worker, import, network request or
filesystem write is started. An inspection is an owned diagnostic, never a launch
or rights authorization.

## Inputs and selection

`inspect_asset_installed_components` accepts the same two admitted snapshots,
product, independently expected epoch/pose context, observed host capacities and
independent review selections as byte inspection, plus explicit component roots.
It calls byte inspection itself; caller-created diagnostic structs cannot bypass
recipe, payload, size or review validation. Unknown costs still reject here.

Each `AssetComponentRoot` selects role, exact component-manifest SHA-256 and an
absolute directory. Require exactly one root for each CODE/ENVIRONMENT/MODEL;
INPUT stays in the owned source snapshot and has no installed root. Duplicate,
unrelated, missing or more than 18 selections reject. Roots must exist, have no
symbolic root/ancestor, and match their normalized canonical path. Root overlap
is checked against filesystem identity along both ancestor chains, including
same-directory aliases; nested/shared roots reject rather than guessing ownership.
Only explicitly selected trees are visited. No environment or cache discovery occurs.

Byte evidence now retains `AssetComponentFile` rows (exact path spelling, SHA-256,
bytes) in each non-input component. The existing strict portable path, collision,
manifest and payload rules apply unchanged; no path is learned from a mutable
external manifest during filesystem verification. INPUT's file vector is empty.

## Closed-tree observation

Every selected tree must contain exactly the listed regular files and necessary
parent directories. Empty files are checked against their actual empty-content
SHA-256. Missing/changed files, different case, unlisted files, extra empty
directories, symbolic links (including dangling links), multiply linked regular
files and special entries reject. Permission/read failures reject. Existing
Python RECORD inventories remain a separate, broader file format; this portable
profile does not represent an arbitrary native installation or full environment.

Two complete scans independently enumerate and hash the trees. File size, single
link count, regular status, parent directories and modification time are checked
around hashing. Both observations must have identical path/time inventories;
all file digests must match the immutable manifest in both passes. Directory
iteration uses no link-following option. Cancellation is checked during evidence
admission, root/ancestor walks, enumeration and streamed hashing.

| Bound | Meaning |
|---|---|
| 16 | Maximum relative file-path segments, including filename |
| 8,192 | Total enumerated entries per pass across selected roots; root directories excluded |
| 64 MiB | Declared installed bytes/hash budget per pass, counting repeated content at each path |
| Existing evidence limits | Up to 1,024 files/component, 4,096 entries including inputs, 16 MiB/file and bounded snapshot admission |

Arithmetic and declared aggregate bytes are checked before reading trees. The
streamed hash reader may read one additional 64 KiB chunk before rejecting a
concurrently grown file. The entry budget also counts directories; it is independent of the manifest's file
budget. No partial result is returned. Success reports canonical roots in recipe
component order, exact installed file count and byte total (excluding INPUT).

This is a cooperative-filesystem check. Portable path-based operations do not
provide descriptor-based containment against hostile concurrent renames, mount
replacement or mutation with restored metadata. Passing scans describe the
selected observation only; files can change after return. Recheck before use.
No persistent lock, sandbox or execution admission is established by this API.

## Results and remaining work

Success removes only `INSTALLED_FILES_UNVERIFIED`, then appends
`RUNTIME_CLOSURE_UNVERIFIED` and `WORKER_LIMITS_UNVERIFIED`. Existing enum values
are preserved. Host, capacity, unselected-review and rights blockers remain.
Archives are not extracted; archive/inventory correspondence, transitive/runtime
closure, legal authority, measured/enforced limits, real inference and actual
asset output remain unqualified. This bounded observation cannot complete AP0.

Caller-selection errors return `INVALID_ARGUMENT`; malformed/linked/extra/missing
inventory entries and digest/size mismatch `INVALID_DATA`; depth/entry/byte excess
`RESOURCE_EXHAUSTED`; observed mid-scan change `RESYNC_REQUIRED`; filesystem
exceptions `IO_ERROR`; cancellation `CANCELLED`. Errors from underlying snapshot
inspection/hashing propagate. All failures preserve caller files and snapshots;
there is no repair, deletion, lock creation or partial publication.
