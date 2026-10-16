# M2 development-exit scheduling — 2026-10-16, Asia/Hong_Kong

Scope: user-authorized roadmap/runbook revision, accepted in [ADR 016](../adr/016-m2-development-exit.md).
M2 remains IN_PROGRESS; no full Q/P/N/E or additional machine is verified.
Parent: `7d03d2eb24ef849fa32fb8a5a844107cb2152a90`; initial worktree clean.
All commands below ran from the repository root. No source/build/dependency,
saved reconstruction, capture or historical validation evidence was modified.

The six local exit items now have explicit evidence candidates:

| Local requirement | Retained evidence to review for closure |
|---|---|
| Real/synthetic projection and compositing | [80-camera parity](2026-10-15-all-camera-reference.md), [floating origin](2026-10-15-floating-origin.md), [clipping](2026-10-15-clipping-boundaries.md) |
| Revision/admission and memory | [Resident reuse](2026-10-16-m2-resident-reuse.md), [2M synthetic capacity](2026-10-16-m2-scale-fixture.md), CPU contract results |
| Bounded local update timings | [10 Hz resident replay](2026-10-16-m2-resident-reuse.md), existing stage observations; presented-FPS certification deferred |
| Local packing comparisons | [Frozen comparisons](2026-10-16-m2-sh-packing.md), [63/80 captured-target views](2026-10-16-m2-packing-quality.md); full Q6 remains open |
| Local input and recovery | [Input timing](2026-10-16-m2-input-latency.md), [injected device loss](2026-10-15-m2-device-loss.md), [isolated minimize](2026-10-16-m2-minimize.md) |
| Lifetime and applicable checks | [Completed FP32 stress](2026-10-16-m2-resident-lifetime.md), [ADR 015 packed acceptance](../adr/015-packed-lifetime-development-acceptance.md), latest scoped build/CPU/static-analysis reports |

These are review candidates, not a new execution or completed exit audit.
Next work is binding their identities and applicability in a local exit review,
then running only genuinely missing local checks. No reconstruction or two-hour
stress rerun was performed or requested. M4 inherits external renderer reference
and native-machine qualification; full corpus/release gates remain due by M6.

Identity check: a Python SHA-256 comparison passed for unchanged specification
§3, Appendix B and AGENTS.md. Before/after specification and runbook hashes are
recorded below and in ignored `artifacts/validation/m2-development-exit-20261016/identity.json`.

| File | Before SHA-256 | After SHA-256 |
|---|---|---|
| `docs/technical_plan.md` | `b58103f74ea5fa40e73082e5971b492d7cd93297853cec78d802beb087a9a7f5` | `954759d797e931b5331aad3178b4eea16ee20b6757736b2cf6c121d2ef831990` |
| `docs/execution_prompt.md` | `74b1067dedb6b567845ea8c757dcba4d3f5e0dfb9756868aa460f4760df8d615` | `65d13669814f522ff7bd799e8fab06fcef03a049d7ed18fd769d53921742fef8` |

Checks: `python3 tools/check_docs.py` exited 0: 745 local targets in 148 files passed. `git diff --check` exited 0 with no errors. No code tests were run for this documentation-only decision.
