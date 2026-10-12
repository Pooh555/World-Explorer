# Development documentation protocol

## Ownership and reading

Use the [documentation map](../README.md). Keep one authoritative home per fact:

| Record | Contains | Update rule |
|---|---|---|
| `technical_plan.md` | Product specification, targets, roadmap, research provenance | Change only for a necessary specification decision; never to obtain a pass |
| `execution_prompt.md` / `AGENTS.md` | Execution policy / concise repository rules | Reference specification/protocols; no milestone history |
| `progress.md` | Current milestone, usable behavior, blockers, next executable step | Replace stale handoff text after each coherent slice; link historical evidence |
| `requirements.md` | Gate → implementation → status → evidence | Update affected rows; retain original thresholds and approved deviations |
| `protocols/` | Implemented contracts, invariants, compatibility, failure semantics | Update with code; distinguish proposed behavior explicitly |
| `adr/` | Consequential decision: alternatives, criterion, choice, evidence, trade-off, revisit trigger | Preserve decision history; link superseding decision |
| `validation/` | Dated observations, commands, identities, failures, results, scope | Add one report per coherent verification slice; correct errors explicitly |
| Repository `README.md` / user guides | Supported quickstart, dependencies, import/view controls, capability limits, troubleshooting | Update when supported behavior changes |
| Ignored `artifacts/` / `build/` | Raw logs, traces, captures, outputs, local probes | Reference by authorized location and hash; never commit private media/secrets |

Future work stays in specification milestones and requirement rows. Put only the
immediate executable task and genuine blockers in progress; avoid a second backlog.
A current-work scratch note must be merged into its owning record or removed at
handoff, not become another permanent source of status.

## Concision without evidence loss

Lead with outcome and scope. Use tables for comparable records; short prose for
rationale. Link existing contracts, command definitions and reports instead of
copying them. Preserve every unique constraint, decision, measurement, failure,
identity and qualification limit when consolidating. Do not move verbose copies
into a tracked archive merely to shrink the handoff.

Soft budgets: AGENTS.md ~60–100 useful lines; progress ~500 words; validation ~300–700 words plus necessary
commands/identities; ADR ~150–300 words. These are review prompts, never reasons
to omit details. Long specifications/contracts are read by section. Avoid routine
command transcripts, repeated environment descriptions, conversational history,
plans presented as results and retrospective rewrites of historical status.

## Status and evidence

Use `NOT_STARTED`, `IN_PROGRESS`, `IMPLEMENTED_UNVERIFIED`, `VERIFIED`, `BLOCKED`,
`DEFERRED_BY_USER`. Mark `VERIFIED` only with current relevant evidence; a requirement may pass on one
platform and remain blocked on another. Scope every verification to the actual profile/hardware and
fixture. Skips, configured CI, mocks, cross-compiles and software GPU execution
cannot certify an untested native target. Synthetic results must be labeled.
No Phase 1 gate changes without the specification's decision process.

Minimum report fields:

```text
# <Slice> — <date/timezone>
Scope/status: affected gate IDs; implemented behavior; qualification limits.
Identity: parent/implementation commit or dirty-source identity; fixture/config hashes.
Checks: actual command or versioned command-reference ID; cwd; exit; result/count;
        toolchain/profile/device; raw-log location.
Failures: observed cause, correction, retained failing evidence; skips/blockers.
Real integration: input/output identities; measured behavior; preserved data.
Remaining: unverified boundaries and next check (current task lives in progress).
```

Default cwd is repository root only when explicitly declared. A command reference
must identify an immutable dated definition; overrides are written in the report.
Reference [2026-10-12 check commands](../validation/checks-2026-10-12.md) only for
runs that actually used them. Keep results/counts/logs in each report. Historical
reports are not upgraded by later code, profile definitions or successful runs.

Performance records also require warmup/repetitions, p50/p95/p99, resolution,
scene/LOD/overdraw, peak-memory definition, competing workloads and whole-video
versus training scope. Retain per-scene failures/metrics. Private artifacts are
local reproducibility references, not downloadable public evidence.

## Handoff and maintenance

Keep project references repository-relative and commands reproducible. Never include
credentials. If commits are authorized, use focused commits and record them; otherwise
leave a reviewable diff with its identity. Do not invent commits or push remotely.

Update progress, affected requirements, protocols and validation index before
ending a substantive slice; commit only authorized coherent work. Progress gives
an exact command or concrete first task, output protection rules and missing
prerequisites. Report implemented behavior, working commands, observed checks,
limitations and next work; do not re-narrate the session.

Before a documentation refactor, compare source facts and references. Check local
links, commands/hashes, status scope and specification integrity. Preserve user
edits. Run `python3 tools/check_docs.py` and `git diff --check` after changes;
code checks are required only if code also changed. New standards apply to future
records; they do not invent missing fields in historical evidence.
