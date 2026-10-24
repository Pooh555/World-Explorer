# Asset dependency graph v1

This is the first implemented A0 contract, owned by native `asset_catalog`:
[`dependency_graph.hpp`](../../include/world_explorer/asset_catalog/dependency_graph.hpp).
It is a CPU graph contract, with a separate implemented
[byte receipt adapter](asset_receipts.md). It is not an asset producer, mutable
catalog, worker recipe, export format or viewer feature. Existing scene/worker schemas
are unchanged. A0 and AP0 remain **IN_PROGRESS**, not qualified.

## Identity, bounds and admission

An `AssetProductId` identifies a semantic intermediate product in a selected
closed dependency graph. It is distinct from chunk IDs, future object/definition
IDs, revision numbers, filenames and content hashes. Two IDs may share content
bytes without merging identities. Each `AssetRevisionRef` contains the product
ID, positive uint64 revision and lowercase SHA-256 content declaration. There is
one selected revision per ID; dependencies bind all three fields exactly.

`validate_asset_dependencies` accepts schema version 1 and an independently
selected expected world epoch/pose revision. Epoch zero is valid; pose revision
must be selected/nonzero. Wrong context or a stale dependency revision/hash
returns `RESYNC_REQUIRED`. Malformed hashes, duplicate IDs/dependencies, missing
parents, self-edges, cycles or inconsistent manual ancestry return `INVALID_DATA`.
Unknown schema is `UNSUPPORTED_OPERATION`; an invalid caller context is
`INVALID_ARGUMENT`. Cancellation returns `CANCELLED` without changing callers.

The graph contains 1–256 products, at most 64 dependencies/product and 4,096
edges total. Excess is `RESOURCE_EXHAUSTED`, checked before constructing derived
adjacency/hash records. Each node declares a valid recipe SHA-256 and optionally
a manual-action SHA-256. The function validates these declarations; it neither
opens files nor proves their content, source rights, evidence classes or completeness.
The receipt adapter binds these declarations to owned file bytes and immutable
publication. The [recipe inspector](asset_recipes.md) checks declarations and
resource arithmetic; [component evidence](asset_component_evidence.md) binds
snapshot bytes/reviews. Installed-file and external rights qualification remain A0 work.

Manual ancestry is exactly the transitive closure of declared manual actions.
An action on a node or manual ancestry on any parent requires its
`manual_ancestor` flag. A true flag without such ancestry is also inconsistent.
Automated descendants cannot clear a declared manual ancestor. This does not
detect omitted/falsified external action records. The [owned journal](manual_actions.md)
now records bounded declarations and offers a scope-restricted inference binding;
graph admission does not yet require it or bind actual output mutations/human
operations. No automatic-success classification follows
from a validated graph.

## Deterministic identity

The result contains parent-before-child product IDs, choosing the smallest ready
ID first, and a canonical SHA-256. Input node/dependency ordering has no effect.
Canonical encoding is compact, sorted-key UTF-8 JSON with unsigned decimal
integers, native booleans, no extra whitespace and no trailing newline:

- Root keys: `schema_version`, `world_epoch`, `pose_revision`, `products`.
- Products sort by ID; each has `product`, `dependencies`, `recipe_sha256`,
  `manual_action_sha256` (string or null), and `manual_ancestor`.
- References have `product_id`, `revision`, `content_sha256`; dependencies sort
  by product ID. No duplicate ID is admitted.

The digest binds every selected revision/content/recipe/action and the context.
It is a cache-identity component, not a complete recipe or an idempotent operation
receipt. A context change conservatively invalidates the graph; independent reuse
or atomic Sim(3) conversion requires separate proof, not an implicit exception.

## A0 resource and rights proposal — qualification pending

The current host remains **contracts-only** for this slice. H_AP24/H_AP48 retain
their proposed capacities and limits in [plan §11.7](../technical_plan.md#117-hardware-and-milestone-plan);
these types/tests certify neither profile nor 8 GiB inference. The recipe inspector
now binds a declaration lock and reports explicit blockers. Before a worker
can run, A0 still needs verified exact code/environment/model and
input hashes, terms/rights receipts, stage/device/time/byte/thread/queue limits,
installation byte inventory and explicit capability/refusal policy. Unknown model
or environment sizes block provisioning; no implicit download or remote fallback.
The [dated candidate assessment](../research/offline_asset_pipeline.md) remains
research, not an installed or cleared recipe. A1 still requires its own consented,
independently scaled real pilot, evaluation manifests and actual native PBR output.
