# Bounded geometry-completion products v1

[`completion_product.hpp`](../../include/world_explorer/application/completion_product.hpp)
adds owned completion declarations and recomputed geometry consistency in
`application`. A0/AP0 remain **IN_PROGRESS**. This profile runs no completion
algorithm and supplies no asset adoption, observed-support authenticity, rights
approval, plausible hidden geometry, completed environment or AP/Q/N token.
Existing geometry/material/structure/static schemas remain unchanged.

## Independent inputs and root

The request supplies private source geometry, structure and capture handles,
original receipt/context, 1–8 independently admitted candidate geometry handles
and a separate product selection. Every handle must share the original receipt,
epoch and pose revision. Structure must reference the selected source geometry;
capture must be that geometry's original capture product. Candidate native scene/
chunk/source-binding/view pins must equal the source's. This profile permits no
pivot, unit or native-source replacement during completion. The complete root
dependency closure passes the capture inference firewall, including method inputs.

Strict root JSON keys are `schema_version=1`, `kind=COMPLETION_PRODUCT`,
`world_epoch`, `pose_revision`, `recipe_sha256`, `source`, `structure`, `capture`,
`scope=UNQUALIFIED_GEOMETRY_COMPLETION`, `subject`, `selected_candidate`,
`candidates`, `manual_ancestor`. References bind exact product ID/revision/content
SHA. Root dependencies are exactly source geometry, structure, every candidate
geometry and every selected configuration product. Capture is coordinator
metadata, outside the inference DAG. Unknown/duplicate fields, excessive depth,
NUL and nonfinite numeric values fail.

`subject={kind,surfaces}` matches an independent OBJECT_GEOMETRY or SHELL_GEOMETRY
selection. Object geometry has no surface IDs; shell geometry selects 1–32 distinct
existing source-structure surfaces. Region surface IDs must be a nonempty subset
of that shell selection (empty for object geometry). These are geometry subjects,
not admitted asset definitions or a proof of real object/shell identity. Full
asset-definition revision/adoption binding remains separate work.

Root bytes are bounded to 1 MiB, candidates to eight, protection rows to 8,192
across candidates, regions to 32 per candidate and region-face entries to 32,768
across candidates. Candidate geometry retains its own 8,192-face/vertex limits.
Used region coordinates have absolute value ≤1,000,000. Independent maximum region
extent is finite [0,1,000,000] arbitrary units, not an AP quality threshold.

## Observed protection and complete region accounting

Candidate row keys are `id`, `rank`, `geometry`, `configuration`, `rejection`,
`protected`, `regions`, `checks`. Positive unique candidate IDs match independent
selections. Candidate products are distinct and cannot alias the source product.
Private candidate handle order and selection order do not define rank.

Each protection row is `{source_face,result_face,rotation}`, with rotation 0–2.
Source faces must be OBSERVED; source and result indices cannot repeat in this
table. Every original observed face must be represented. Cyclic corner mapping
must preserve exact positions, normals, material group, native triangle/weight
anchors, source-view set and nullable uncertainty. Every observed output face
must come from this protected table. This strict one-to-one profile permits cyclic
corner/vertex-index changes, but no subdivision/remeshing correspondence, normal
refit or reassignment of source support. It protects admitted declarations; it
does not establish physical observation validity.

Region keys are `id`, `faces`, `class`, `confidence`, `surfaces`, `bounds`.
Positive region IDs are unique within a candidate; face lists are nonempty and
distinct within a region. Classes must equal actual candidate face-support
classes and cannot be OBSERVED. INFERRED methods allow INFERRED/UNKNOWN regions;
LIBRARY allows GENERATED/UNKNOWN/MANUALLY_EDITED; GENERATED permits any explicit
nonobserved class. Unknown confidence is null; other confidence is finite [0,1].
Manual ancestry comes from the original graph, independently of confidence.

Selected output faces must be covered exactly once by protection or region rows.
Rejected alternatives can retain failed partition checks; overlaps/gaps are
recomputed. Region bounds contain exact native vertex minima/
maxima, not declared estimates. The maximum axis extent must fit the independent
policy. Supplied bounds that differ from actual geometry fail metadata admission.
No volume closure, overlap/intersection, hidden-surface quality, plausibility or
photometric check is inferred from these bounds.

`checks` contains exactly `observed_anchors_preserved`, `complete_region_partition`,
`region_extent_within_policy`; each boolean must equal the recomputed result.
Ranks are unique in 1..candidate_count. The independently selected candidate must
have rank 1, rejection NONE, at least one region and all checks true. Other
candidates may retain failed checks: rejection must be ANCHOR_CHECK_FAILED when
anchor protection fails, otherwise REGION_CHECK_FAILED for partition/extent
failure, otherwise DECLARED_RANKED_ALTERNATIVE. Malformed indices, payloads,
provenance or dependencies are always errors, including for rejected candidates.
Rank is a declared ordering, not a verified optimizer score or quality judgment.

## Method, prompt, library and rights inputs

Each independent method selection pins method, uint64 seed, configuration,
algorithm and rights products plus optional prompt/library. Configuration JSON
keys are `schema_version=1`, `kind=COMPLETION_CONFIGURATION`, `world_epoch`,
`pose_revision`, `recipe_sha256`, `method`, `seed`, `algorithm`, `prompt`,
`library`, `rights`, `manual_ancestor`. Its exact direct dependencies are the
selected algorithm/rights and present prompt/library products; roles cannot alias
one product or configuration. Configurations and all these selected payloads are
copied from the original owned receipt.

| Method | Required optional inputs | Region interpretation |
|---|---|---|
| INFERRED | Prompt/library both null | Explicit admitted inferred/unknown geometry |
| GENERATED | Prompt required, library null | Explicit nonobserved classes |
| LIBRARY | Library required, prompt null, seed 0 | Generated/unknown/edited geometry |

Configuration is ≤16 KiB; algorithm and rights payloads ≤64 KiB each. Prompt is
strict JSON ≤8 KiB with exactly `schema_version=1`, `kind=COMPLETION_PROMPT`,
`text`; text is nonempty valid JSON UTF-8, ≤4,096 bytes and contains no decoded
NUL. Library bytes are nonempty and ≤1 MiB. Algorithm, rights and library bytes
remain opaque declarations: no code execution, license review, library decoding,
candidate/library geometric correspondence or historical influence is proved.
These fields do not issue provisioning/execution permission. Backend/model/
environment locks, reviewed rights and actual producer execution remain required.

## Ownership and publication

The private handle owns normalized candidates/regions/checks, selected admitted
geometry, original pins and all selected root/configuration/input bytes. It
survives caller/source lifetime. A single immutable batch (≤8 MiB, at most 33
records) publishes `completion-ID-REV` and deduplicated `completion-input-ID-REV`
records. Geometry/structure publication remains independent; they are required
private prerequisites rather than silently adopted runtime revisions here.

Publication revalidates original receipt/context and protects its source tree.
Cancellation preserves the previous committed manifest; exact retries resume the
same intent, and stale/conflicting operations fail. Reload freshly admits the
independent prerequisites, requires the selected current commit and checks every
root/method input key/content hash. Root alone cannot replace missing selected
method bytes. Existing immutable-store crash/locking semantics apply unchanged.
