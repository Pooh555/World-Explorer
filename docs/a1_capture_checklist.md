# A1 pilot capture checklist

Prepare one small, static room corner that you own or have permission to record
and use for this project. This prepares the real data required by
[plan §11.7](technical_plan.md#117-hardware-and-milestone-plan); it does not start
processing or qualify a reconstruction. The existing internet-sourced `sample_1`
does not provide this pilot's independent scale or reference captures.

## Before recording

- [ ] Include visible floor, two walls, a ceiling segment and an opening, plus
  **three distinct opaque props**. Keep one prop partly occluded in the baseline.
  Choose a manageable corner with visible surface texture and clear object edges.
- [ ] Record who captured it, when, source ownership/permission and permitted
  project uses. Keep personal information, people and unrelated private material
  out of the scene. Record any restrictions; do not infer distribution permission.
- [ ] Use one camera/lens and unchanged zoom and resolution for the baseline.
  Record device, lens, resolution, frame rate, stabilization and HDR/log/color mode.
  Lock focus, exposure and white balance where possible; otherwise note what
  remains automatic. Retain actual metadata rather than inventing missing values.
- [ ] Keep geometry and baseline lighting fixed. Inspect a short test for blur,
  clipping and focus before recording the complete passes.

For the baseline, move the camera through overlapping viewpoints rather than
only rotating in place; use different heights and revisit the starting region.
Keep each required element visible across overlapping views. These capture
suggestions follow [COLMAP's acquisition guidance](https://colmap.github.io/tutorial.html#structure-from-motion).
Do not add texture or rearrange objects between baseline views without recording
that as a different condition.

## Record the independent scale and up reference

- [ ] Choose two identifiable, fixed endpoints visible in the **inference pass**.
  Measure their separation with a tape or other independent instrument. Record
  endpoints, value in metres, method, instrument and uncertainty; photograph the
  setup/readout so the points can be identified later.
- [ ] Record an independent up reference: an IMU/gravity record, or a physically
  checked floor reference with the check documented. Note uncertainty and how it
  relates to the visible scene. A wall/floor predicted by reconstruction is not
  an independent reference.
- [ ] Keep measurement notes/photos separate from evaluation images. We will map
  the endpoints to source-derived geometry later; you do not need mesh triangle
  IDs now. Evaluation-only camera poses/images must not determine inference scale.

Unknown uncertainty or an unverified up reference stays unknown and leaves the
measured gate open. One known distance establishes scale metadata; it does not
supply Q5's independently measured surface ground truth. See the
[measurement contract](protocols/capture_measurements.md) for the later admission
boundary, not a form to fill with guessed coordinates.

## Capture passes in this order

| Pass | What to record | Later role |
|---|---|---|
| Baseline orbit and revisit | Original static arrangement under baseline light; cover the shell and all three props, including upper/lower viewpoints and opening edges | INFERENCE; only this designated material enters automatic reconstruction/fitting |
| Independent inspection path | Same arrangement/light from a separate path; record additional temporal blocks to hold out before tuning | HELD_OUT; evaluation poses remain separate from inference |
| Backside references | Additional views of concealed surfaces, keeping each subject in its original pose where accessible | EVALUATION_ONLY; never feed these views into completion selection or fitting |
| Changed light | Before moving any prop, keep camera/material/geometry fixed for comparable views, then record **at least three unseen lighting conditions**, including a near light | EVALUATION_ONLY; no material/light/exposure refit against these images |
| Empty background | Restore baseline lighting, then remove each required prop in turn and record exposed floor/wall/under-object regions; also record the unobstructed corner | EVALUATION_ONLY; note exactly which props were removed, replaced or left fixed |

For every reference, note its matching baseline subject/view, condition, time and
any camera, object or lighting movement. Do not rotate a prop for a backside shot
and label it unchanged; record the changed pose and any correspondence limitation.
If a required hidden surface cannot be photographed, keep that gap explicit.

For lighting, retain lamp positions/directions, settings and available measured
illumination/color-temperature or calibration records. Mark missing measurements
as unknown. Condition names alone do not establish calibrated light. Keep source
exposure/color metadata and original images; do not brighten or color-correct
held-out images to match an output. Missing real calibrated-light evidence leaves
real relighting unqualified.

The later software edit test must remove every prop and move it by at least its
own width. Record each prop's width and a feasible destination/inspection path
now. A real moved-prop reference is useful when available; label its changed pose
and keep it evaluation-only. Ordinary Godot import and the native completed/PBR
viewer are later product checks, not capture substitutes.

## Keep the handoff local and complete

Use a fresh folder such as `data/local/a1_corner_01/`, with original files grouped
by pass plus `measurements/` and `notes/`. Keep original camera filenames/bytes,
full videos, timestamps and sidecars. Do not transcode, crop, discard failed takes,
merge lighting conditions, or overwrite earlier sources. This follows the local
[source/artifact layout](protocols/artifacts.md); generated jobs remain separate
under ignored `artifacts/`. Do not commit or upload the capture.

- [ ] Provide a simple inventory of the three props and required structural
  elements, their positions/occlusion and the file/time ranges for each pass.
- [ ] Identify inference, held-out and evaluation-only material **before tuning**.
  Evaluation annotations, reference images and masks remain outside inference.
- [ ] Include measurement/permission/device/lighting notes and a missing-items
  list. We will hash original files and freeze exact frame/pose/recipe selections
  during admission; this folder is not already a sealed corpus.
- [ ] Keep a setup/action log. Do not hand-mask, click segmentation seeds, choose
  completion candidates or repair outputs and report the result as automatic.
  Such assistance must remain recorded separately.

When ready, identify the local folder; no upload is needed. The first software
step will inspect source identity, roles, coverage and missing prerequisites,
then freeze the bounded capture/evaluation selections. Completing this checklist
neither supplies an approved worker recipe nor clears the
[installation prerequisites](asset_installation_plan.md).

## Authorized baseline-only development

On 28 October the user supplied a replacement `sample_2.mp4`, declared it
baseline-only and authorized estimated references. The
[intake report](validation/2026-10-28-sample2-a1-intake.md) pins that source and a
proposed temporal split. This permits experimental development with explicitly
estimated/inferred/generated evidence; it does not convert those stand-ins into
measured scale or real held-out-path/background/changed-light ground truth.
The checklist above remains the preparation for full real A1 qualification.
Do not request the same physical references again as a prerequisite to the
already authorized estimated route. Model installation/recipe closure remains
separate; no source solve or completed asset is implied by intake.

The [29 October native-view check](validation/2026-10-29-sample2-native-views.md)
materializes all 48 inference and 12 held-out candidates without spatial resizing.
Their 564,064,383 PNG bytes exceed the current aggregate capture/receipt bounds;
the complete projected native evaluation masks also exceed their pixel budget.
These local view files are preparation evidence, not admitted capture inputs or
an A1 reconstruction. Preserve the frozen roles and all selected views while
resolving producer/ownership resources; do not reduce the checklist or gate scope
to make the existing limits pass.
