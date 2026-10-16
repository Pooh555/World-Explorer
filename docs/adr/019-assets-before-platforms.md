# ADR 019: validate local asset production before platform qualification

Date: 17 October 2026, Asia/Hong_Kong. Decision: accepted in response to the
user's request to move the expanded goal before M4. All A/AP capabilities remain
NOT_STARTED; M3 stays active. Supersedes [ADR 018](018-offline-asset-production.md)
only for scheduling and its reconstruction-only M6 release boundary.

The user wants to visualize completed, editable environments before investing in
native platform generalization. The asset architecture consumes M3's calibrated
views/geometry and M2's immutable viewer updates; it does not require Windows,
macOS, AMD/Brush qualification or M5 multi-room streaming first. Stable contracts
and existing CPU portability CI remain necessary throughout.

Alternatives: keep assets after M6, move only the pilot, or move the local core
workflow. Keeping the old schedule delays validation of the actual product;
moving only a demonstration leaves completed room production behind portability.
Choose M1 → M2 → M3 → A0–A5 → M4 → M5 → M6, then the later A6 engine packaging
adapter. M1–M6 keep their relative order and identifiers; M7–M8 remain
DEFERRED_BY_USER. [Plan §§8 and 11](../technical_plan.md#8-roadmap) own the schedule.

A1 must display a real automatic three-prop completed PBR environment in the local
native viewer, with separate objects, changed-light and move/remove inspection,
plus individual/assembled glTF import evidence. A2–A5 complete and validate the
bounded local room workflow before M4. A5 closes a scoped local development exit;
unchanged H_V/cross-platform AP7/AP8 and dependent full AP9 qualification remain
open until M4/M6. Intrinsic asset-quality failures cannot be deferred as hardware
issues. M6 integrates reconstruction and at least one qualified asset-production
profile; A6 prefab packaging remains later.

Trade-off: platform/release dates move later, and local model/hardware/rights gaps
may block the asset track. No thresholds, corpus denominators, measured-geometry
rules or historical evidence change. Re-estimate after A1; revisit sequencing if
that pilot exposes a concrete dependency. This document change implements no
pipeline behavior or hardware qualification.
