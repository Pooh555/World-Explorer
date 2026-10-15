# User-approved M1 exit revision — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1 remains IN_PROGRESS**. The user reports no physical Windows
or macOS machines and explicitly requests revising M1 and assigning physical
runs later. [ADR 012](../adr/012-m1-development-exit.md) and
[plan v1.1 §8.1](../technical_plan.md#81-m1-development-exit-and-hardware-qualification)
implement that decision. This is an authorized milestone-scheduling change,
not new execution evidence or a retrospective gate pass. Parent **e9b1d70**;
cwd repository root. Raw ignored root **R**:
`artifacts/validation/m1-exit-policy-20261015/`.

M1's development exit now uses the available Linux/NVIDIA host and actual hosted
Linux/Windows/macOS CPU build/test results. It retains the consented 20–30 s
textured-room chain, full ≤250k viewer fixture, zero Vulkan errors, native fly
checks, contract/replay behavior, selected version/license audit and recorded local
timings. Named reference-machine access is no longer an M1 prerequisite.
Selected redistribution gaps stay recorded; complete clearance remains due before
distribution. No capture, solver recipe, test or numerical acceptance target changes.

M4 now explicitly owns physical Windows/macOS viewer/GPU/display/input/HiDPI
qualification and the former M1 **≤250k / 1280×720 / ≥30 FPS on H_V/H_M** target.
Device access can be borrowed or remote; the user need not own the machines.
H_R P1 measurements stay with M3/M6, and renderer reference targets stay with
their assigned milestones. Full Q/P/N/E, benchmark workloads, consent rules,
rights and claimed release-profile requirements remain due. Hosted CPU passes
retain their narrower scope. M1 may close after its local checklist review;
M2 development then proceeds without physical Windows/macOS prerequisites.

## Specification and preservation identities

| Document | Before SHA-256 | Approved after SHA-256 |
|---|---|---|
| Technical plan, v1.0 → v1.1 | `2e9e8a5bea71e51c7416aa0be20d4dfad2310db0baeb511b4d2722e4cd3093c5` | `b58103f74ea5fa40e73082e5971b492d7cd93297853cec78d802beb087a9a7f5` |
| Execution prompt, aligned M1/M4 rows/provenance | `e162b63d8fed4507af8571914cde0bafcdc3abfb7fd9969edcb40c001d66cc12` | `74b1067dedb6b567845ea8c757dcba4d3f5e0dfb9756868aa460f4760df8d615` |

The original research cutoff, all of **§3** and **Appendix B** remain byte-identical.
The runbook preserves its original v1.0 hash and records the newly approved plan
identity. Existing AGENTS instructions, prior ADRs/reports, production, build/CI,
worker and test sources are unchanged. Progress and the requirements matrix
retain IN_PROGRESS/full-gate statuses, add M4 ownership and replace the previous
hardware-blocked handoff with a read-only local M1 evidence review. The viewer
fixture protocol now identifies M4 as the physical qualification owner.

`python3 R/check_policy.py before` / `after` compare **2,088 protected files**,
**211 implementation/config files** and **2,063 prior raw evidence files**. The
only authorized protected-file changes are the plan and runbook above; the
remaining **2,086**, all **94 src/include** files within the implementation set,
and all selected prior evidence retain exact sizes/hashes. Existing completed
scenes and failed stages are preserved; no reconstruction, hardware/CI run,
installation, download, push or upload is performed.

`R/protected-after.json` is the current preservation baseline for subsequent work.
It supersedes the old protected hashes for these two user-approved documents;
old snapshots and receipts remain unchanged. `R/preservation-before.json` /
`preservation-after.json` retain full inventories and unchanged §3/Appendix B
hashes. Commands, exits, check logs and policy/document identities are bound in
`R/checks.json`. Documentation links, include boundaries and `git diff --check`
pass. Code, CPU/GPU and performance suites were not rerun for this documentation-only
revision. No external qualification is inferred from the new criteria.
