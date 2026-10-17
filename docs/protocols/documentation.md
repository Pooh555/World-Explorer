# Development documentation protocol

## Ownership and reading

Use the [documentation map](../README.md); read by task. Each fact has one owner:

| Record | Content / update rule |
|---|---|
| `technical_plan.md` | Product specification, targets, roadmap, research provenance; change only for necessary specification decisions, never to obtain a pass |
| `research/` | Dated primary-source comparisons and component terms/availability/limits; link from the plan, which owns proposed contracts/targets; validation owns qualification |
| `execution_prompt.md` / `AGENTS.md` | Execution policy / concise repository rules; reference specification/protocols, omit milestone history |
| `progress.md` | Current milestone, usable behavior, blockers, exact next step; replace stale handoff after each coherent slice and link historical evidence |
| `requirements.md` | Gate → implementation → status → evidence; update affected rows, preserve thresholds/approved deviations |
| `protocols/` | Implemented contracts/invariants/compatibility/failure semantics; update with code, label proposals |
| `adr/` | Decision, alternatives, criterion, choice, evidence, trade-off, revisit trigger; preserve history and link superseding decisions |
| `validation/` | Dated commands, identities, observations, failures, results, scope; one report per coherent verification slice, explicit corrections |
| Repository `README.md` / guides | Supported quickstart, dependencies, import/view controls, limits, troubleshooting; update with supported behavior |
| Ignored `artifacts/` / `build/` | Logs, traces, captures, outputs, probes; reference authorized location/hash, never commit private media/secrets |

Future work belongs in specification milestones/requirement rows. Progress keeps
only immediate work and genuine blockers. Merge scratch notes into their owner or
remove them at handoff; do not create another permanent status source.

## Concision without evidence loss

Lead with outcome/scope; use tables for comparisons and prose for rationale.
Link contracts, command definitions and reports instead of copying them. Preserve
every unique constraint, decision, measurement, failure, identity and qualification
limit. Do not move verbose copies into tracked archives to shrink the handoff.

Soft budgets: AGENTS ~60–100 useful lines; progress ~500 words; validation
~300–700 words plus necessary commands/identities; ADR ~150–300 words. These
prompt review, never omission. Read long specifications/contracts by section.
Avoid routine transcripts, repeated environments, conversational history, plans
presented as results and retrospective historical-status rewrites.

## Status and evidence

Statuses: `NOT_STARTED`, `IN_PROGRESS`, `IMPLEMENTED_UNVERIFIED`, `VERIFIED`,
`BLOCKED`, `DEFERRED_BY_USER`. `VERIFIED` requires current relevant evidence,
scoped to actual profile/hardware/fixture; a requirement can pass on one platform
and remain blocked on another. Skips, configured CI, mocks, cross-compiles and
software GPUs cannot certify untested native targets. Label synthetic results.
Phase 1 gate changes require the specification's decision process.

Minimum report fields:

```text
# <Slice> — <date/timezone>
Scope/status: gate IDs, implemented behavior, qualification limits.
Identity: parent/implementation commit or dirty-source identity; fixture/config hashes.
Checks: actual command or versioned command-reference ID; cwd; exit; result/count;
        toolchain/profile/device; raw logs.
Failures: cause, correction, retained failing evidence, skips/blockers.
Real integration: input/output identities, measured behavior, preserved data.
Remaining: unverified boundaries/next check; current task lives in progress.
```

Declare repository root explicitly to use it as default cwd. Command references
must identify immutable dated definitions; record overrides, results/counts/logs
in the report. Use [2026-10-12 commands](../validation/checks-2026-10-12.md) only
for runs that used them. Later code/profiles/passes never upgrade old reports.

Performance evidence also needs warmup/repetitions, p50/p95/p99, resolution,
scene/LOD/overdraw, peak-memory definition, competing workloads and whole-video
versus training scope. Retain per-scene failures/metrics. Private artifact paths
are local reproducibility references, not public downloads.

## Handoff and maintenance

Use repository-relative references/reproducible commands; never include credentials.
If authorized, make/record focused commits; otherwise leave a reviewable diff with
its identity. Never invent commits or push remotely.

Before ending a substantive slice, update progress, affected requirements,
protocols and validation index; commit only authorized coherent work. Handoff
needs an exact command/concrete first task, output protection and missing
prerequisites. Report implemented behavior, working commands, observed checks,
limits and next work without re-narrating the session.

Before documentation refactoring, compare source facts/references; check local
links, commands/hashes, status scope and specification integrity. Preserve user
edits. Run `python3 tools/check_docs.py` and `git diff --check`; code checks are
needed only if code changed. New standards apply to future records; never invent
missing fields in historical evidence.
