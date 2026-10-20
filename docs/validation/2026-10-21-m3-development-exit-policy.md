# M3 local development-exit policy — 2026-10-21, Asia/Hong_Kong

Scope/status: user-requested M3 development-exit policy update, accepted in
[ADR 020](../adr/020-m3-development-exit.md). M3 remains IN_PROGRESS; this policy
decision does not close its checklist or certify Q1/Q3/Q5/P1/N2 or any full gate.

Identity: based on `cee79fcddd85adf864422fd161e9be140d3d7a4e`; this is a
documentation-only worktree change. No implementation, build product, saved
reconstruction, measurement or historical evidence was changed.

Decision: M3 may hand off to A0–A5 after the local software checklist in plan
§8.3 is reviewed, without independent distance/up evidence for internet-sourced
`sample_1`. Its scale remains ARBITRARY. Q5's independent measured-surface/scale
requirements and N2's certified-region/100-path requirements remain open under
their original definitions. Synthetic collision evidence can verify software
contracts only. A1's own real pilot and independent-scale requirements are
unchanged.

Checks: `python3 tools/check_docs.py` exited 0 (1,062 local targets in 209 files);
`git diff --check` exited 0. No code tests were run for this documentation-only
change.
