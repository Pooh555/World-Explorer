# Offline asset research and planning — 17 October 2026

Scope/status: documentation-only response to the expanded final goal.
Phase 2 A0–A6/AP0–AP9 remain NOT_STARTED. M3 remains IN_PROGRESS; M1/M2 retain
their scoped development exits, all full gates retain their prior statuses, and
M7–M8 remain DEFERRED_BY_USER. Research/document checks are not pipeline qualification.

Identity: parent `adddd642e3d3c10557cc2034f57b673e5d665b3f`, initially clean
working tree; this Markdown-only slice is identified by its Git diff/commit.
Baseline hashes/status and the original specification §3 text are retained in
ignored `artifacts/planning_asset_pipeline_20261017/baseline.json` and
`original_phase1_gates.txt`. No source-media inspection was needed.

The [candidate assessment](../research/offline_asset_pipeline.md) records dated
primary papers, author repositories, model cards, controlling terms, official
APIs and author benchmark contexts. It distinguishes visible source/checkpoint
links from downloaded payload verification, author claims from independent
reproduction, official hardware guidance from unverified lower-memory ports,
and permissive wrappers from restricted dependencies. No published experiment
was independently reproduced in this task. Inaccessible weight-host terms and
some documentation pages remain explicit gaps, not verified availability/rights.

[ADR 018](../adr/018-offline-asset-production.md) adds the approved offline phase
after M6. [Plan §11](../technical_plan.md#11-phase-2-offline-asset-production) owns
its proposed contracts, worker/dependency graph, resource profiles, milestones,
small real pilot, AP gates, automatic/partial/rejected/assisted outcomes and
conditional estimates. Requirements/runbook/navigation reference those owners.
Existing README's stale M1-incomplete banner is corrected to current M3 status;
historical validation records are unchanged.

Checks (cwd `/home/Pooh555/programming/World-Explorer`; Python 3.14.8 / Git 2.56.0, no native
build/device profile involved):

| Actual command | Exit / observation | Local raw evidence |
|---|---|---|
| `python3 tools/check_docs.py` | 0; 817 repository-local link targets in 156 Markdown files pass | `artifacts/planning_asset_pipeline_20261017/docs_check.log` |
| `git diff --check` | 0; no whitespace errors | `artifacts/planning_asset_pipeline_20261017/diff_check.log` |
| `git diff --cached --check` | 0; staged Markdown, including new files, has no whitespace errors | `artifacts/planning_asset_pipeline_20261017/staged_diff_check.log` |
| `python3 artifacts/planning_asset_pipeline_20261017/check_preservation.py` | 0; original §3 and M1–M6 roadmap rows byte-identical; every original progress/requirement line retained in order; historical reports unchanged; all 12 changed/new files Markdown and table widths consistent | `artifacts/planning_asset_pipeline_20261017/preservation_check.log`, `reviewed_markdown_hashes.json` |

The preservation helper initially matched valid pre-existing `,+y_down` coordinate
metadata as a prose typo; its corrected pattern checks accidental prose joins
without changing that metadata. Repository documentation checks were not weakened.

Failures/limits: no pipeline implementation, installs, weight downloads, video
processing, reconstruction, GPU execution, media upload, remote push or publishing.
Code/build/GPU tests are inapplicable to this Markdown-only slice. Proposed
H_AP24/H_AP48 capacities and AP budgets are not measured minimums/performance.
Real novel-light evidence, generic hidden completion, component rights and exact
resource/adapter compatibility remain unresolved before implementation.

Next implementation step remains M3 checked dense-input preparation from saved
sample_1 canonical poses/images, as specified in [progress](../progress.md).
Preserve existing CPU/training environments, failed sample_2 stages, saved scenes
and frozen fixtures. No unchanged solver/fit/long-stress rerun. Phase 2 starts
with A0 only after M6; this planning task executes neither step.
