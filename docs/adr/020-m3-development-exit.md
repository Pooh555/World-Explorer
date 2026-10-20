# ADR 020: M3 local development exit without sample_1 measurement

Date: 2026-10-21, Asia/Hong_Kong. Decision: accepted at the user's request.

The retained `sample_1` is internet-sourced video. The user cannot provide an
independent distance or up/gravity reference for it. Requiring that reference to
close M3's software-development phase would block implementation without making
the existing reconstruction measurable. Inventing a scale, floor or gravity
reference would make the evidence unsound.

Adopt the M3 local development exit in [plan §8.3](../technical_plan.md#83-m3-local-development-exit-and-deferred-reconstruction-qualification).
After its software workflow and failure/recovery contracts are implemented and
reviewed with current local evidence, M3 may hand off to A0–A5 without a
measurement for `sample_1`. The review must include native scene handoff and
synthetic collision checks, label synthetic evidence, preserve unknown-space
blocking, and retain actual commands, identities, results and limits.

This is a development-scope decision, not a quality or geometry result. Every
scene derived from `sample_1` remains arbitrary-scale. Q5's measured-surface and
independent-scale requirements and N2's certified walk-region/100-path criteria
remain unchanged and open until prescribed evidence exists. No synthetic floor,
collider or support fixture can satisfy those real gates or enable measured
walking. A1's independent-scale evidence for its own real pilot also remains
required. M3 stays IN_PROGRESS until its local checklist is completed and
reviewed; accepting this decision alone does not close it.

Alternatives were blocking the software handoff until an unavailable reference
could be obtained, or treating synthetic/sample-derived scale as measured. The
chosen scope keeps implementation moving while preserving all thresholds and
evidence rules. Revisit only if suitable independent sample_1 measurement data
becomes available or the M3 software checklist proves infeasible.
