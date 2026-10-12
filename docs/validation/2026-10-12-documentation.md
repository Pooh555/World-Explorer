# Documentation consolidation — 2026-10-12, Asia/Hong_Kong

Parent: `f115d90`; implementation identity is the commit containing this report.
Cwd: repository root. Documentation/tooling only; M1 remains IN_PROGRESS and
M7–M8 remain deferred. No reconstruction, GPU or hosted CI qualification run.

## Change

Progress now contains current behavior, protected data, usable commands and next
work. Historical facts stay in indexed reports; previously handoff-only viewer and
environment details were explicitly carried into their existing reports. Seven
reports reference one dated six-profile command definition, retaining their own
counts, environments, failures, identities, logs and qualification limits.
The runbook delegates documentation policy to the new protocol. Local-link checks
are integrated into native CI; that configuration is not a hosted pass.

## Checks

| Actual command/check | Result |
|---|---|
| `python3 tools/check_docs.py` | exit 0; repository-local Markdown targets exist; fragments/external URLs not checked |
| `.deps/bin/actionlint .github/workflows/native.yml` | exit 0, existing actionlint 1.7.7 |
| `python3 tools/check_boundaries.py` | exit 0 |
| `git diff --check` | exit 0 |
| Temporary broken-link probe | Checker exits 1 for a nonexistent target; temporary file removed |
| `python3 artifacts/docs-refactor/audit.py` | All original Markdown SHA-256 values and fenced shell-command lines remain; changed historical reports differ only by command-table delegation and labeled carryover |

Audit baseline/snapshot/results: ignored `artifacts/docs-refactor/{before.json,before/,audit.json}`.
Existing specification, interface protocols, ADRs and validation JSON manifests
are byte-identical. `technical_plan.md` was not edited. Manual review checked
handoff facts against their reports and preserved native/synthetic distinctions.
No application code changed; previous software test evidence is not rerun or
promoted by this documentation check.
