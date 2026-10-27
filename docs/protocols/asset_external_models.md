# External MODEL file observations v2

[`external_models.hpp`](../../include/world_explorer/asset_catalog/external_models.hpp)
adds a separate read-only observation to [recipe inspection](asset_recipes.md).
Large external payloads stay outside the small owned [receipt](asset_receipts.md).
All v1 receipt, [component evidence](asset_component_evidence.md) and
[installed-tree](asset_installed_components.md) limits/encodings remain unchanged.
A0/AP0 remain **IN_PROGRESS**; this profile starts no installer or worker.
The [shared request/CLI](external_model_inspection.md) now reopens independently
hash-selected snapshots and explicit file selections through this same observer.

## Independent selection and metadata

`inspect_asset_external_models` accepts two admitted snapshots, product ID,
expected epoch/pose, observed host capacities, explicit MODEL file/review
selections and independently chosen byte ceilings. Both snapshot graphs and the
original recipe/context/input pins are revalidated. Exactly every recipe MODEL
must be selected, with no unrelated or duplicate component. CODE, ENVIRONMENT,
INPUT and configuration/prompt bytes are not verified by this profile.

Each MODEL component selects a strict manifest by its recipe SHA-256:

| Field | Contract |
|---|---|
| `schema_version`, `kind`, `role` | Integer `2`, `EXTERNAL_MODEL_FILES`, `MODEL` |
| `archive` | Null; archive acquisition/extraction/correspondence is unsupported |
| `installed_bytes` | Exact file-size sum, matching the recipe's known cost |
| `files` | Nonempty rows, exactly `path`, `sha256`, `bytes`; every size positive |

The caller independently supplies the component hash and exact logical path,
SHA-256, size and native file path for every row. Selection order is irrelevant;
the result follows recipe/manifest order. Logical paths retain the portable v1
rules: ASCII, 240 bytes/128 per segment, no traversal/device names, duplicates,
case aliases or file/parent collisions. Native file locations are explicit and
need not reproduce logical paths. They must be canonical absolute normalized
paths, at most 4,096 UTF-8 generic-string bytes/64 segments, without embedded NUL.
Leaves must be regular, nonsymlink files with exactly one hard link; every parent
must be a nonsymlink directory. Equivalent selected files across components reject.
Neighbouring unselected files are outside this observation's scope.

Each MODEL needs pinned terms and a strict v1 review record in the small evidence
snapshot. Role/component/terms/statuses must match the original recipe. Optional
independent `AssetReviewSelection` records must be MODEL-only, related, unique,
and match exact review hash/reviewer. Absence leaves `m_review_selected=false`;
matching records bind the caller's selection without authenticating a person or
granting legal authority. Rights statuses retain all recipe blockers.

## Bounds, streaming and failure

Bounds are 16 models, 64 files/model, 256 files total, 256 KiB/manifest and
64 KiB/terms or review. File/aggregate ceilings must be explicitly positive,
file ≤aggregate, at most 4 GiB/file and 8 GiB/pass; the file ceiling must also fit
`size_t`. Checked arithmetic accounts for every file independently, including
identical contents. The diagnostic retains metadata, not payload bytes.

After all joins, record each file's size/modification time and check identity/
links/parents. Two passes hash every file with the existing 64 KiB streaming
reader and its exact declared size as ceiling. Check stamps before and after
hashing, after the optional progress callback, and across all files at each pass
boundary. Progress reports pass 1/2, zero-based flattened file index and cumulative
hashed bytes. Cancellation is checked during parsing, filesystem walks and each
hash chunk; callback exceptions fail the call. No partial result is returned.
The observer never writes caller files. Callback effects are caller-owned.

Malformed schema/path/content is `INVALID_DATA`, unsupported profiles
`UNSUPPORTED_OPERATION`, bound excess `RESOURCE_EXHAUSTED`, selection duplication
or unrelated/invalid native paths `INVALID_ARGUMENT`, stale context/joins or
changed stamps `RESYNC_REQUIRED`, cancellation `CANCELLED`, filesystem errors
`IO_ERROR`, and other callback/backend exceptions `BACKEND_FAILURE`. Missing
snapshot blobs retain `NOT_FOUND`. A concurrently grown file may cause the hash
reader to read one extra 64 KiB chunk before refusing its byte ceiling.

This is a cooperative-filesystem observation: no OS handle/lock or immutable
external payload survives the call. Hostile races, restored/coarse timestamps
and post-return mutation remain outside its guarantee. Revalidation is required
before any later use. Global component, installation, review, runtime-closure
and worker-limit blockers are always retained/added, alongside original host,
unknown-cost and rights blockers. Full environment closure, enforced budgets,
acquisition authority and actual inference remain separate prerequisites.
