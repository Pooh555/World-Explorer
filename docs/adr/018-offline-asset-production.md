# ADR 018: add offline reusable asset production after M6

Scheduling and the reconstruction-only M6 boundary are superseded by
[ADR 019](019-assets-before-platforms.md). The original decision below is retained
as history; its asset architecture, provenance and local-processing rules remain.

Date: 17 October 2026, Asia/Hong_Kong. Decision: user-approved product direction;
implementation NOT_STARTED. Supersedes only the final-goal completion/relighting
exclusion in plan v1.2; Phase 1 exclusions and all existing gates remain intact.

The requested final product requires editable objects and architectural structure,
plausible hidden surfaces/backgrounds, PBR materials and shared reusable exports.
The current appearance/geometry viewer does not satisfy that deliverable.

Considered expanding M3/M6, repurposing live M7/M8, adopting one scene-generation
model, or adding a separate offline phase. Criteria are preservation of accepted
work/gates, automatic local execution, measurable asset quality, component rights,
explicit uncertainty and the ability to replace research backends.

Choose Phase 2 A0–A6 after M6, specified in [plan §11](../technical_plan.md#11-phase-2-offline-asset-production).
M1–M6 retain their order and Q/P/N/E thresholds; M7–M8 remain DEFERRED_BY_USER.
Original calibrated views anchor observed geometry, identities and material/light
fitting. Splats remain an appearance reference and a separate surface experiment.
Use isolated workers, immutable asset definitions separate from instances, labeled
completion and revision-dependent exports. Engine packaging follows portable assets.

The [research comparison](../research/offline_asset_pipeline.md) finds no verified
single released component satisfying the whole pipeline. Generative candidates
have consistency/resource/rights gaps; permissive wrappers do not clear restricted
renderer dependencies. Stronger local hardware is allowed, with explicit model
provisioning and no remote fallback. Artist repair/replacement/review is optional,
with assisted results separate from automatic success.

Trade-off: a substantial second vision/asset-production effort, additional capture
and novel-light evaluation, and qualification on larger hardware. Generated
geometry cannot qualify measured reconstruction/walking. Revisit recipe/hardware
choices after the small real A1 pilot or changed controlling terms; preserve
failures and use another ADR for consequential scope/gate changes. Documentation
research alone verifies no new capability.
