# Capture/evaluation selections v1

[`capture_selection.hpp`](../../include/world_explorer/map_store/capture_selection.hpp)
adds strict receipt-backed selection manifests and bounded owned inference inputs
in `map_store`. A0/AP0 remain **IN_PROGRESS**. This contract records selected roles
and known dependency separation; it does not freeze a complete real evaluation
corpus, authenticate consent, prove upstream fit history or qualify a recipe.

`validate_inference_dependency` also checks an independently selected auxiliary
product against this exact receipt/context and the same retained forbidden-hash
set, following all declared upstream products. The
[typed mask-edit path](object_mask_edits.md) uses it before publication and before
constructing committed inference-mask inputs. Evaluation annotations remain
forbidden on inference frames.

The [typed inventory reader](evaluation_inventory.md) also supplies explicit extra
evaluation roots to `validate_evaluation_separation`. That check extends forbidden
hashes locally and rechecks selected inference closures, without mutating the
original capture handle or its narrow inputs. Its boundaries and limits are owned
by that protocol. [Typed temporal/inspection evidence](evaluation_paths.md) supplies
both of its additional record roots through the same check; it also binds exact
original timestamps and decoded camera values without exposing them to new workers.
[Controlled prop references](controlled_references.md) likewise check their entire
declared root closure, including optional lighting observations, against selected
inference dependencies. [Capture envelopes](capture_envelope.md) also check their
additional root while retaining complete source/view metadata as coordinator data.

## Selection and byte admission

`inspect_capture_selection` takes an independently selected manifest product,
semantic `CaptureSelectionId`/revision, capture sources, frame views/roles,
annotation refs, optional measurement ref, original admitted receipt, independently
admitted [object-observation handles](object_observations.md), and current epoch/
map revision. Each source binds a capture ID, full source SHA-256 and rights-product
ref. The pose asset's actual source provenance must match this independent hash.
Rights and measurement bytes remain opaque declarations; no legal approval or
METRIC_MEASURED status is created.

The JSON document contains exactly `schema_version=1`, `selection_id`, `revision`,
`world_epoch`, `pose_revision`, node `recipe_sha256`, `sources`, `frames`,
`measurement` (null or ref), and `manual_ancestor`. Source rows contain exactly
`capture_id`, `source_sha256`, `rights`. Frame rows contain `view`, `role` and
`annotations`; views use the existing object-observation format. Roles are
INFERENCE, HELD_OUT and EVALUATION_ONLY. Source/frame/annotation order may vary;
the immutable handle normalizes it. Exact encoded bytes retain their source hash.

Admission rejects duplicate keys, raw NUL, depth above 16, foreign/missing fields,
wrong types, floating/negative integers and unsupported schemas/enums. Every
reference binds an exact graph node before copying. Direct manifest dependencies
must equal the deduplicated selected image/pose/annotation/rights/measurement refs.
There must be at least one inference and one noninference frame. Sources must all
be used. Capture/frame pairs are unique; identical image bytes cannot cross roles,
even under another product ID. The input document cannot select its own roles.

## Annotation and pose separation

Inference frames cannot carry annotation refs. Every selected annotation must be
supplied once as a private admitted handle from the same original receipt/context,
with the exact selected product, view and unique semantic observation ID.
Annotations on noninference frames retain masks, decisions and manual ancestry.
Evaluation labels never become candidate selection or inference inputs through
this API.

The inspector follows inference image/pose dependency closures. Known evaluation
image/pose hashes and annotation/mask/evidence bytes, recipes and declared actions
are forbidden there; hash comparisons also catch aliases with different IDs.
Annotation ancestry is followed until a selected source-view product is reached:
inference images may feed an evaluation pose without allowing labels back into
inference. Explicit annotation/mask/evidence roots remain forbidden even if they
alias a source view. Shared opaque annotation recipes are conservatively refused
in inference because this contract cannot prove them label-free.

Every selected pose asset is actually decoded with the existing bounded native
reader. Expected epoch/map revision, source provenance, capture/frame/calibration,
image hash, per-frame pose revision, TRACKING state and absence of invalidation
must agree. Every observation present in an inference pose snapshot must itself
be explicitly selected as INFERENCE. Hidden or merely unselected pose-fit inputs
are refused. These checks cannot discover undeclared historical influences,
labels encoded in opaque payloads or a dishonest producer's provenance.

## Ownership and worker-facing inputs

`AdmittedCaptureSelection` is coordinator/evaluation data: normalized selections,
evaluation image/annotation handles, exact source manifest bytes and receipt hash.
Its `inference()` method returns a distinct, privately constructed
`InferenceCaptureInputs` handle. That allocation owns **only** selected inference
frames, with encoded image bytes, selected observation/calibration/pose/world
transform, expected context, explicit ARBITRARY scale and input manual ancestry.
It contains no complete pose asset, original receipt, evaluation frame table,
labels, masks or annotation handles. There is no implicit conversion from the
coordinator handle. Future workers must accept this narrower input explicitly;
no worker/coordinator integration is claimed here.

Copies retain private lifetime after original handles, caller aliases and source
directories disappear. Evaluation annotation edits do not change inference
payloads. Manual evaluation ancestry remains in the full manifest; inference
frames carry ancestry of their own image/pose dependency closures. Neither field
alone classifies a job as automatic or assisted success. Failure/cancellation
returns no partial handle and consumes no caller data. No image decoding,
reconstruction, inference, downloads or map/scene transactions occur.

## Limits and owned publication

Bounds precede typed vector growth: 256 KiB manifest, 64 frames, 32 sources,
128 annotation handles, 16 annotation refs/frame, 32,768 aggregate annotation runs
and 67,108,864 annotated pixels. Existing DAG limits apply, including 64 direct
parents. Image bytes are at most 16 MiB each and 32 MiB retained in each of the
inference/evaluation groups, counting repeated selected copies. Pose assets are
at most 16 MiB each, 32 MiB decoded input bytes in aggregate, 4,096 frames and
64 calibrations/submaps each. Existing receipt aggregate limits remain. Compressed
annotation handles share owned storage; full pose snapshots stay internal.

`publish_capture_selection` revalidates source/context and stores exact manifest
bytes under `capture-<selection_id>-<revision>` through the existing
[immutable record registry](asset_metadata_io.md). Original source/output trees
must be disjoint. Committed key conflicts, operation collisions, matching replay,
stale bases, cancellation and durable manifest-last publication use that contract.
The publication is metadata only: reopening still requires independently selected
original receipt/annotation inputs. It is not an execution token or a trusted
sealed-corpus/measurement/rights journal. The [validation](../validation/2026-10-24-capture-selections.md)
retains synthetic isolation checks and a refusal of an incomplete selection over
real retained pose-fit inputs.
