# Asset recipe lock inspection v1

Native `asset_catalog` inspects bounded recipe declarations from an
[owned byte receipt](asset_receipts.md). Public types and entry point are in
[`recipe.hpp`](../../include/world_explorer/asset_catalog/recipe.hpp).
A0/AP0 remain **IN_PROGRESS**. This is contract inspection, with no installer,
worker launch, rights approval or execution authorization token.

## Selection, identity and ownership

`inspect_asset_recipe` takes an admitted receipt, selected product ID,
independently expected epoch/pose context, caller-observed host capacities and
cancellation. The graph is revalidated; the node selects the exact recipe hash.
`copy_blob` obtains an independent owned copy of at most 64 KiB from the immutable
snapshot, without filesystem access. Later source edits/removal cannot alter
inspection. Results own parsed values and pin product, receipt, graph, recipe
and context identities.

The JSON root has exactly:

| Field | Contract |
|---|---|
| `schema_version`, `stage` | Integer 1; stage has 1–64 lowercase ASCII letters, digits or underscores; it does not certify an available implementation |
| `world_epoch`, `pose_revision` | Unsigned integers matching independently selected context; pose revision is nonzero |
| `inputs` | Exact node dependency references (`product_id`, `revision`, `content_sha256`), order independent; at most 64 |
| `configuration_sha256`, `prompt_sha256`, `seed` | Lowercase SHA-256 declarations and uint64 seed (zero allowed); explicit identities even for no-prompt configurations |
| `components`, `budget` | Bounded declarations defined below |

JSON fields/types are exact. Duplicate keys, raw NUL, depth greater than 16,
negative/boolean/floating integers and malformed hashes reject. Unknown schema
or enum returns `UNSUPPORTED_OPERATION`; malformed declarations `INVALID_DATA`;
invalid caller selection `INVALID_ARGUMENT`; unknown product/blob `NOT_FOUND`;
stale context/dependencies/input hashes `RESYNC_REQUIRED`; excess copy/JSON/
component budget or byte overflow `RESOURCE_EXHAUSTED`; cancellation `CANCELLED`.
Failed inspection changes neither source nor admitted bytes. Const calls may
share a snapshot; receipt handles, host and expected context must remain stable
during use.

Changing any frozen recipe field changes recipe, graph and enclosing receipt
identities. Old snapshots remain independently inspectable. No mutable catalog
head or implicit stale-product reuse is introduced.

## Components and rights declarations

Each component has exactly `role`, `sha256`, `archive_bytes`, `installed_bytes`
and `rights`. There are at most 64 components: exactly one `CODE` and
`ENVIRONMENT`, at least one `INPUT`, and zero to 16 `MODEL` entries. Duplicate
`(role, sha256)` pairs reject; distinct roles may share a digest. Hashes identify
exact payload or inventory-manifest bytes, not versions/names. Archive and
installed sizes are uint64 or explicit null (unknown). Archive size zero means
no archive is required; installed payload size must be positive. A zero archive
never substitutes for an unknown size, and preexisting caches are not assumed.

`INPUT` hashes must equal the deduplicated content-hash set of selected
dependencies; distinct dependency IDs/revisions remain in `inputs`. A root
import has no dependencies and binds its own admitted content hash as input.
Those content bytes have passed receipt admission. This does not establish
independent capture calibration or source rights.

`rights` has exactly `terms_sha256`, `review_sha256`, `local_use`, `distribution`.
Hashes are lowercase SHA-256 or null. Status values are `UNREVIEWED`,
`DECLARED_PERMITTED`, `RESTRICTED`. Either permitted declaration requires both
terms/review identities. Local evaluation and distribution are separate;
permission for one does not grant the other.

Code/environment/model payloads and installed inventories, configuration/prompt
receipts and terms/review payloads are **not opened or verified** by this API.
Their hashes/sizes/rights remain declarations. Every result carries
`COMPONENT_BYTES_UNVERIFIED`; self-declared permission never clears rights or
qualifies reproducibility. Trusted review selection, payload admission and
installed-file reconciliation remain required before provisioning/execution.

## Resource assessment and refusal

`budget` contains exactly:

| Fields | Schema limits |
|---|---|
| `device`, `device_vram_bytes` | `CPU` requires zero VRAM; `CUDA` requires positive uint64 VRAM; neither qualifies a device |
| `host_ram_bytes`, `scratch_bytes` | Positive uint64 declarations |
| `wall_time_ms` | 1–604,800,000 |
| `cpu_threads`, `queued_jobs` | 1–1,024; 1–64 |
| `batch_size`, `candidate_limit` | 1–4,096; 1–1,000,000 |
| `width`, `height` | Each 1–32,768 |

These are format bounds, not AP8 thresholds, measured memory estimates or
implemented worker enforcement. The lock must contain the complete frozen
configuration/input inventory described in plan §11.3; hashes alone do not
verify payload schemas or implementation behavior.

All archive plus installed sizes are conservatively added with checked uint64
arithmetic and no assumed cache sharing. Scratch is added to reserve disk. Any
unknown size leaves installation/reservation totals null and adds
`UNKNOWN_INSTALLATION_SIZE`; known partial sums still undergo overflow and
capacity checks. Unknown costs cannot become a zero-cost provisioning pass.

Caller-observed RAM/VRAM/free disk/thread/queue capacities are compared with
requirements; insufficient capacity adds `HOST_CAPACITY_EXCEEDED`. Zero means
unavailable. `m_contracts_only` defaults true and adds `CONTRACTS_ONLY_HOST`;
the current 8 GiB development host retains that setting. Unreviewed/restricted
local use and uncleared distribution add separate blockers. Blockers are
deduplicated in enum order. Even a hypothetical fit with permitted declarations
retains unverified external bytes and authorizes no action. No model download,
installation, fallback, hardware purchase or inference occurs.
