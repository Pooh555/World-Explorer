# Asset component evidence v1

Native [`component_evidence.hpp`](../../include/world_explorer/asset_catalog/component_evidence.hpp)
adds bounded byte/review verification to [recipe inspection](asset_recipes.md).
A0/AP0 remain **IN_PROGRESS**. Results are owned diagnostics, never installation,
rights approval or worker-execution tokens.

## Selection and trust boundary

`inspect_asset_recipe_evidence` accepts two independently selected/admitted
[receipt snapshots](asset_receipts.md), a product ID, independently expected
epoch/pose context, host capacities and zero to 64 independent review selections.
The first snapshot owns the recipe and source products; the second owns evidence
blobs. Both graphs are revalidated against expected context. Evidence blobs may
be root import products with opaque import recipes; byte admission does not
claim semantic qualification of those import recipes.

The existing recipe inspector first validates the exact recipe/context/input
references. Configuration and prompt records must then exist by the recipe's
selected SHA-256. Their bytes remain opaque: this does not validate their domain
schemas or prove implementation behavior. Verification uses only immutable
snapshot copies, so later source-file edits/removal cannot redirect it.

An `AssetReviewSelection` identifies role, component SHA-256, review SHA-256 and
expected reviewer identifier. Callers select these outside the evidence receipt;
the receipt cannot appoint its own trusted reviews. Duplicate/unrelated selections
reject; missing selections remain explicit blockers. The identifier is a bounded
ASCII label, not cryptographic identity or proof of legal authority. An explicitly
selected record binds the caller's chosen review; this library does not interpret
license terms, authenticate reviewers or independently grant permissions.

## Component manifests and payloads

For `CODE`, `ENVIRONMENT` and `MODEL`, the recipe component hash selects exact
manifest bytes. JSON has exactly:

| Field | Contract |
|---|---|
| `schema_version`, `role` | Integer 1; role matches the selected recipe component |
| `archive` | Null for no archive; otherwise exactly `sha256`, `bytes`, selecting a nonempty evidence blob |
| `installed_bytes` | Sum of declared file bytes, equal to the recipe's known installed size |
| `files` | 1–1,024 rows, each exactly `path`, `sha256`, `bytes` |

All nonempty file/archive payloads must exist in the admitted evidence snapshot
with exact sizes; SHA-256 was checked during byte admission. Empty file rows
require the exact SHA-256 of empty bytes and no blob; they still consume one file
entry. Empty archives are represented by null, not an empty archive payload.
Files at different paths count independently even when content is shared.
The archive's bytes and inventory bytes are independently bound; archives are
never extracted and correspondence between an archive's contents and its listed
inventory remains unverified.

`INPUT` components directly select admitted source-content bytes in the first
snapshot, with no archive and the exact source size. Their hashes still equal
the recipe's deduplicated input set, with distinct product/revision references
retained. No component may silently replace unknown sizes or missing terms/review
identities: incomplete evidence rejects rather than inventing costs/permission.

Paths in this inspector are logical installation-relative names only; they are
never materialized or used to open files. Exact validated rows are retained for
the separate [installed-tree observation](asset_installed_components.md). Allowed
characters are ASCII letters/digits and `._-/`;
length is 1–240 bytes, segment length at most 128. Reject absolute paths, empty/
dot/dot-dot segments, trailing dots, Windows device names, case aliases, duplicate
paths and file/parent collisions. Backslashes, colons, spaces and NUL are excluded.
This restricted portable-file profile does not represent links, arbitrary native
names or a full Python installation. Existing worker RECORD inventories remain a
separate format with their own [scope](worker_identity.md).

## Terms and review records

Every component selects nonempty terms bytes and strict review JSON by the hashes
in its recipe. A review has exactly `schema_version` (integer 1), `role`,
`component_sha256`, `terms_sha256`, `reviewer`, `local_use`, `distribution`.
Role/component/terms and both statuses must match the recipe. Reviewer labels
have 1–128 ASCII letters/digits or `._:@-`. Statuses retain the recipe's
`UNREVIEWED`, `DECLARED_PERMITTED`, `RESTRICTED` semantics.

Matching independent review selections set per-component `m_review_selected`.
A selection with a different hash/reviewer rejects; absent selections add
`REVIEW_NOT_SELECTED`. Selected unreviewed/restricted records retain their local
rights blockers; local use and distribution remain distinct. Reading terms or
matching a hash never upgrades their legal status.

## Bounds, failures and remaining gates

Component manifests are at most 256 KiB; config/prompt/terms/review records at
most 64 KiB. There are at most 4,096 file entries across all components, including
inputs and empty files. File payloads/archives retain the receipt's 16 MiB/file,
64 MiB aggregate and graph/file-count bounds. These constraints do not qualify
large model packages or full environments. Shared payload sizes are cached within
the call; arithmetic is checked and no process, installer, network or file write
is started. Required inventories are closed declarations, not proof that external
packages contain no unlisted dependencies/files.

Strict JSON rules are shared with recipe inspection: exact fields, no duplicate
keys/raw NUL, bounded nesting and unsigned integer sizes. Unsupported schema or
enum returns `UNSUPPORTED_OPERATION`; malformed paths/sums/records or incomplete
cost identities `INVALID_DATA`; missing blobs `NOT_FOUND`; size/count excess or
overflow `RESOURCE_EXHAUSTED`; stale context, component costs or selected review
bindings `RESYNC_REQUIRED`; duplicate/unrelated caller selections
`INVALID_ARGUMENT`; cancellation `CANCELLED`. Failed verification preserves all
caller data/snapshots; there is no partial publication or partial success result.

Only complete byte verification replaces `COMPONENT_BYTES_UNVERIFIED` with
`INSTALLED_FILES_UNVERIFIED`. Host capacity/contracts-only and rights blockers
remain. Installed roots have a separate [bounded observation](asset_installed_components.md);
archive extraction, transitive/runtime closure,
trusted human review, measured/enforced worker limits and real inference/output
still need separate qualification. No result clears AP0 or permits provisioning.
