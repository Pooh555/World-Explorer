# Typed action-bound object mask edits

The shared [`object_mask_edit.hpp`](../../include/world_explorer/application/object_mask_edit.hpp)
command validates object-observation/mask replacements before publishing through
the [draft mutation journal](draft_mutations.md). A0/AP0 remain **IN_PROGRESS**.
This is local CPU admission/publication, not segmentation or an inference worker.

## Independent selection and admission

`ReplaceObjectMaskCommand` selects before/after observation records, a draft
directory and the original action/operation/base/targets. Callers supply admitted
before/after receipts, the pinned manual-action journal and independently admitted
capture selections for both receipts. Capture product references and encoded
selection bytes must remain identical: editing masks cannot also reassign roles.
The exact view must occur among each selection's inference frames. Both held-out
and evaluation-only views are rejected.

Both observations undergo existing [native admission](object_observations.md):
actual selected image/pose/calibration binding, context/revisions, contained grids,
finite fields, complete nonoverlapping mask runs and declared manual ancestry.
An `EDIT_MASK` declaration must target exactly the original selected mask. The
observation ID and product ID, mask product ID, view, evidence references,
class/association hypotheses and accepted/rejected evidence stay fixed. Semantic
observation revision advances exactly once without overflow. Grid and runs may
change within existing admission bounds. Split/merge and identity/evidence edits
require separate future adapters.

`AdmittedCaptureSelection::validate_inference_dependency` reuses the selection's
existing upstream role firewall. Its private forbidden-hash set covers selected
evaluation images/poses and annotation ancestry, including content, recipes and
action hashes, with the existing source-view stopping rule. It binds the exact
receipt/context and traverses every declared parent of an independently selected
product; fresh IDs do not hide byte aliases. Both old/new observation closures
must pass. This preserves the ban on evaluation annotations in inference frames;
edited inference masks use a distinct typed path. Undeclared external training
history, relabeled real sources and authentic consent remain unverified.

Every typed check runs before any durable intent. The generic draft publisher
then enforces exact journal/action/receipt bindings, revision +1/new bytes/manual
ancestry throughout the affected descendant closure, unchanged unrelated nodes,
source-directory protection and its 16-product/64 KiB-per-product limits. The
smaller draft byte limit applies even where native observation/mask admission
supports larger records. Other descendants remain opaque: this adapter qualifies
only the selected observation and mask, not geometry/material recomputation.

The adapter derives an admission hash from compact sorted-key UTF-8 JSON with
`schema_version: 1`, `adapter: OBJECT_MASK_EDIT`, `capture`,
`before_observation` and `after_observation`. The last three values are exact
product/revision/content-hash references. This hash selects the explicit v2 draft
intent. The original receipt pins bind the remaining graph and source bytes;
native admission binds the independently selected fields to their record hashes.
Callers cannot supply an admission hash to this typed command. A different
capture product identity, even one aliasing identical selection bytes, or another
observation changes the intent. Removing the binding cannot replay typed history.

## Committed inference input

`read_object_mask_edit` repeats typed checks and requires the independently selected
current draft commit, original request/receipts and journal pins. Generic history
admission must find the exact committed result and owned output bytes. A prepared
intent returns `WOULD_BLOCK`; no typed inference handle is constructed. Technical
failures/cancellation return errors. The [attempt commands](draft_attempts.md) now
add separate durable terminal outcomes after the same typed admission;
`read_object_mask_attempt` reads them with independent current journal pins.
Exact retries resume prepared publication or replay committed history unchanged.

Only this reader constructs `InferenceObjectMask`. Its immutable owned snapshot
contains observation selection references, grid/runs/coverage, the selected
inference frame's image/camera values, commit/result pin and derived manual ancestry.
It contains no evaluation data, observation label/evidence bodies, full pose asset
or receipt. The top-level ancestry flag includes the edit; the frame's own flag
still describes its source inputs. A downstream consumer must propagate the edit's
ancestry and count its output as assisted. Source/output removal after construction
does not change the handle. It is not authenticated human activity, a worker
completion, live scene transaction, automatic-yield result or measured geometry.

Legacy opaque draft publication remains available, but its result cannot create
this typed handle without passing all checks. Worker/asset command adoption and
runtime updates through the existing scene path remain future work. See the
[local validation](../validation/2026-10-25-object-mask-edits.md) for synthetic scope.
