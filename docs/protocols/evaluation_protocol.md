# Frozen asset evaluation protocol v1

[`evaluation_protocol.hpp`](../../include/world_explorer/map_store/evaluation_protocol.hpp)
adds coordinator-only evidence in `map_store`. A0/AP0 remain **IN_PROGRESS**.
It freezes declared sampling, complete eligible-mask tables, configuration identities,
fixed lights/seeds and a blinded review profile. It does not execute metrics,
rendering, sampling or review, authenticate a freeze before tuning, or approve a
recipe/product. No AP/Q/P/N/E threshold is configurable through this record.

## Independent selection and fixed profile

`inspect_evaluation_protocol` takes a selected product, independent sampling/reviewer
parameters, six configuration refs, mask/light pins, original capture/receipt and
expected world/map context. The product binds the capture, not a future corpus hash,
avoiding a seal/reference cycle. Corpus admission subsequently binds this exact handle.

Geometry uses AREA_WEIGHTED_SURFACE with 1–1,000,000 declared samples and a uint64
seed; UV sampling uses TEXEL_CENTERS with resolution 1–8,192. These are bounded
policy declarations, not a sampler implementation or geometry/UV quality pass.
Configuration roles are exactly GEOMETRY_SAMPLER, UV_SAMPLER, RENDERER, TONE_MAPPER,
METRICS and REVIEW_RUBRIC. Each ref binds actual nonempty owned bytes of at most
64 KiB. These are opaque configuration/identity artifacts, not installed code,
checkpoint bytes or proof that their implementations obey the profile.

The closed metric profile follows plan §3.3: display RGB [0,1], per-image PSNR and
equal-weight scene reduction, SSIM 11×11/sigma 1.5/K1 0.01/K2 0.03/data range 1,
AlexNet LPIPS with normalization, and no fitted test exposure/color correction.
Captured-appearance and PBR render columns are both required and stay separate.
The reviewer profile requires 3–32 declared minimum reviewers, blinded=true, scale
1–5 and GEOMETRY/MATERIAL/SCENE dimensions. Numeric quality thresholds stay in the
specification; unknown fields (including threshold overrides) fail. Freezing a
rubric neither supplies real ratings nor authorizes manual inference assistance.

There are 3–8 selected light configs, each nonempty and at most 64 KiB. Labels
use 1–64 ASCII lowercase letters/digits/underscore/hyphen; labels and content hashes
are unique. Each freezes a uint64 seed and near-light boolean; this v1 profile
requires at least one declared near light. Actual novelty, light physics/calibration,
feasibility and a real AP6 trial remain unverified. Changed bytes need new pins.

## Complete masks and observed/completed separation

Every selected noninference view has exactly one OBSERVED and one COMPLETED mask.
The profile supports 1–16 evaluation views (2–32 masks), full native calibrated
width/height and integer-center pixel grids. Inference/foreign/stale views, omitted
tracks/views and duplicate track/view keys fail. There is no resizing or crop
conversion. Each mask pins its full native view and exact source receipt/context.

A mask contains a complete ordered run partition: positive counts, contiguous first
indices starting at zero and exact grid coverage. Each run has a strict eligibility
boolean and UNKNOWN/OBSERVED/COMPLETED surface declaration. UNKNOWN is never eligible;
OBSERVED-track eligibility requires OBSERVED. COMPLETED-track eligibility may include
observed or completed surfaces, and its counts never enter observed metrics. Excluded
pixels remain in the denominator/area accounting. Empty eligible masks are preserved
and explicitly diagnosed; they cannot silently disappear from the table.

These are frozen declarations, not measured surface provenance, true visibility,
independent geometry, actual valid-image correspondence or ≥90% AP1 render coverage.
Per-track summaries own total pixels and eligible counts by declared surface class.
The existing rectangular-crop metric worker does not consume these masks; a runner
for this protocol remains unimplemented. Existing metric evidence is not upgraded.

## Schemas, dependency isolation and bounds

The root has exactly `schema_version=1`, `world_epoch`, `pose_revision`,
`recipe_sha256`, `capture`, `manual_ancestor`, `sampling`, `metrics`, `render_columns`,
`review`, `artifacts`, `masks`, `lights`. Sampling/metric/review keys are the fixed
profile above. Mask pins contain `track`, `view`, `product`; lights contain `label`,
`product`, `seed`, `near_light`. Full view/ref formats are unchanged.

Mask bodies have the same six header fields plus `view`, `track`, `width`, `height`,
`runs`; run keys are `first`, `count`, `eligible`, `surface`. Each mask depends exactly
on capture plus its image/pose products. The root depends exactly on capture and
all deduplicated config/mask/light refs. Actual graph recipe/manual ancestry must
agree. The root closure passes the [capture evaluation separation check](evaluation_inventory.md#schema-dependencies-and-role-separation),
including config/mask/light content/recipe/action aliases. Narrow inference inputs
remain unchanged. Undeclared historical influence and future workers are unverified.

Root/masks are at most 256 KiB each; aggregate mask bytes ≤4 MiB. Runs ≤16,384 per
mask /65,536 total, pixels ≤16 Mi per mask /64 Mi total. Values are checked before
allocation/indexing/count arithmetic; no per-pixel buffers are allocated. Existing
capture/receipt/DAG limits also apply, so not every independent maximum fits at once.
Strict JSON refuses raw NUL, duplicates, depth over 16, unknown fields/enums, invalid
integers/booleans/hashes and nonfinite numbers. Selection sizes/strings are bounded
before copies. Mask/light selection order normalizes without rewriting pinned bytes.

## Ownership, recovery and corpus binding

The private result owns normalized selections, root bytes and mask summaries. Copies
survive caller/source removal and cannot convert to inference inputs. Publishing
rechecks original receipt/context and atomically writes
`evaluation-protocol-<product_id>-<revision>` through the existing immutable registry,
protecting the original receipt tree. Staged cancellation exposes no committed root;
exact operation/base/byte replay recovers. Fresh reads require an independent current
commit, exact root hash and original capture/receipt, then repeat admission. Dependent
mask/config/light bytes stay in that owned receipt. No new crash mechanism, UI,
worker execution or scene/map transaction is added.

[Corpus schema 7](evaluation_corpus.md#typed-evaluation-protocol-in-schema-7) requires
this exact handle for present protocol refs. Sampling realization, implementation,
rendering, correspondence, lighting novelty, review and freeze timing remain open;
protocol admission cannot supply a score, an execution token or an AP pass.

## Explicit full native-grid accounting v2

`EvaluationProtocolSelection::m_schema_version` defaults to 1. Selecting 2 requires
root **and every mask** schema 2, admitted receipt CAPTURE_V2 and capture schema 2.
All exact fields, both complete tracks, native grid/view binding, eligibility,
dependency isolation, sampling/metrics/lights/review requirements and byte/run
limits remain unchanged. Aggregate pixels are bounded at **256 Mi pixels** for
schema 2; schema 1 retains **64 Mi pixels**. Per-mask pixels remain **16 Mi** and
the view limit remains 16. This is RLE declaration accounting with no per-pixel
allocation or sampler/metric implementation. It preserves every view, track and
excluded pixel; no AP threshold or evaluation denominator changes.

The [validation](../validation/2026-10-29-capture-storage.md) admits synthetic
12-view 3840×2160 partitions (24 masks, 199,065,600 pixels), checks aggregate
refusal and owned publication/reopen. These are synthetic declared surfaces,
not real eligibility/provenance masks. Native sample_2 storage observation alone
supplies neither pose/calibration nor measured mask correspondence. Existing
corpus-inspection JSON remains v1; no real corpus/recipe or AP gate is qualified.
