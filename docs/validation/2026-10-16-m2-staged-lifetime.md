# Staged-upload lifetime completion audit — 2026-10-16 Asia/Hong_Kong

Scope/status: completed-artifact audit **VERIFIED locally**; **M2 IN_PROGRESS**.
This is the full/empty staged-upload run started with the implementation recorded
in [the staged-upload report](2026-10-16-m2-p6-staged-uploads.md), before resident
chunk reuse. Audit parent `3c120ce`; cwd repository root. Its original process
has ended. The original launcher did not retain an exit receipt or launch-time
executable digest; neither fact is reconstructed from `passed=true`. This audit
checks the retained result, stdout, complete event stream and image bytes.

Original command, as recorded by its launch/handoff:

```sh
build/dev/we_gpu_lifetime_stress artifacts/sample_1/scene.wexplore artifacts/validation/m2-p6-staged15-full-20261016 7200 > artifacts/validation/m2-p6-staged15-full-20261016.log 2>&1
```

The retained summary reports completion on native Linux Wayland/NVIDIA GeForce
RTX 4060 Laptop GPU with Vulkan validation. Its final stdout JSON equals the
saved summary, and the flushed final event is `complete` at the same elapsed
time. `python3 artifacts/validation/m2-completion-20261016/audit_staged_lifetime.py`
exits **0**. Its complete assertions and SHA-256-bound result remain in that
ignored directory's `audit_staged_lifetime.py` / `lifetime-audit.json`.

| Observation | Audited value |
|---|---:|
| Requested / reported elapsed duration | 7,200 / **7,200.518344413 s** |
| Full retained sample_1 splats | **295,952** |
| Visible updates / final revision | **480 / 481** |
| Periodic revision records | **479**, consecutive revisions 2–480; final full revision is counted separately |
| Resizes | **359**, exact repeating four-extent sequence |
| Minute samples / submitted frames | **119 / 313,366** |
| Readbacks | **13**, all byte-identical |
| Reported Vulkan validation errors | **0** at every sample and completion |
| Peak tracked owned buffer allocations | **445,172,560 bytes**, below 536,870,912 |
| Process peak / final RSS | **466,423,808 / 288,473,088 bytes** |

The auditor checks monotonically advancing timelines/frame counts, exact
revision/residency parity, all event counts, matching stdout/summary, every
sample's allocation cap, the complete event and every PPM digest. The 13 PPMs
share SHA-256
`107ceeb0c2cb975041af83db2283e0d6f3673c06d8503f1109c9cd3955036613`.
The manifest remains
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`;
the scene content remains
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
Summary SHA-256 is
`8a77b6e13cffaf894804dcfa45d9e70f9048792266f66c11fde5b8cb3c9f8b9a`;
events SHA-256 is
`1c36ad7c21445be1eb264b37f649ce9840ffd77e812e7cd0a29b4ffdba030249`.

The original workload alternates full and synthetic empty revisions every 15
seconds, resizes every 20 seconds and captures approximately every ten minutes.
It exercises staged host transfers, rather than consecutive resident-chunk
reuse or the P6 10 Hz workload. Short native correctness and CPU checks overlapped
the early portion, as recorded in [the minimize report](2026-10-16-m2-minimize.md).
This is lifetime evidence, not a performance measurement or total VRAM measure.
No scene, fit, source, checkpoint or frozen fixture changed.

The [resident-copy continuation](2026-10-16-m2-resident-reuse.md) retains the
subsequent rebuilt P6 miss and the implementation/checks that follow it. Its new
full/full/empty stress records an actual process exit and launch identities.
Reference Q/P/N/E, physical loss, other platforms and full M2 remain unqualified.
