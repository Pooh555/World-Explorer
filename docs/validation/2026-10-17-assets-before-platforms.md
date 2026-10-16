# Local asset goal before platform qualification — 17 October 2026

Scope/status: Markdown-only rescheduling requested by the user so the completed
editable environment can be visualized before native-platform generalization.
M3 remains IN_PROGRESS; M1/M2 development exits retain their prior scope;
A0–A6/AP0–AP9 remain NOT_STARTED and M7–M8 remain DEFERRED_BY_USER. No capability
is implemented or qualified by this change.

Identity: parent `2a5b8fe971eb4208d1efa24b37af2c3d261967c2`, initially clean
working tree. Original documents/hashes are retained in ignored
`artifacts/asset_before_platform_20261017/before/` and `baseline.json` in that
artifact directory. The local commit containing this report identifies the slice.

[ADR 019](../adr/019-assets-before-platforms.md) supersedes ADR 018's schedule and
reconstruction-only M6 release boundary. [Plan §8](../technical_plan.md#8-roadmap)
inserts A0–A5 after M3/before M4, with the later A6 adapter after M6.
[§11.8](../technical_plan.md#118-local-product-exit-before-native-platform-qualification)
requires real native PBR visualization at A1 and defines the scoped local A5 exit,
preserving original reference/native tests for M4/M6 and all intrinsic quality
obligations before A5 closure. M6 includes a qualified local asset-production
profile alongside original reconstruction/release requirements. Native viewer
parity is separate from advanced-worker parity across operating systems.

Research observations, camera/provenance/export contracts, numerical Q/P/N/E and
AP acceptance targets remain intact. Old reports remain unchanged; ADR 018 gains
only a supersession note, and its legacy section link still resolves through an
explicit anchor. Requirements/runbook/guidance/navigation and the concise planning
paragraph in progress follow the new schedule. The exact next M3 task is retained.

Checks (cwd `/home/Pooh555/programming/World-Explorer`; Python 3.14.8 / Git 2.56.0;
no native/device profile involved):

| Actual command | Exit / result | Local raw evidence |
|---|---|---|
| `python3 tools/check_docs.py` | 0; 831 local link targets in 158 Markdown files pass | `artifacts/asset_before_platform_20261017/docs.log` |
| `git diff --check` | 0; no whitespace errors | `artifacts/asset_before_platform_20261017/diff.log` |
| `git diff --cached --check` | 0; new/staged Markdown also has no whitespace errors | `artifacts/asset_before_platform_20261017/staged_diff.log` |
| `python3 artifacts/asset_before_platform_20261017/check_preservation.py` | 0; original §3 and M1–M3 rows byte-identical; original M4–M6 acceptance retained; AP acceptance cells/numeric targets unchanged apart from phase label; data/source contracts, historical reports/ADR body, next M3 step and old link anchor preserved; only 12 Markdown files changed, table shapes valid | `artifacts/asset_before_platform_20261017/preservation.log`, `reviewed_hashes.json` |

Limitations: no code edits, dependency installation, model downloads, source-media
inspection, reconstruction/GPU execution, uploads, publishing or remote pushes.
Code/build/device tests are inapplicable to this documentation-only slice.
Rescheduling brings product feedback forward but adds asset work before platform
and release work; it does not remove hardware, rights, capture or research risks.

Next executable implementation step remains checked M3 dense-input preparation
from saved sample_1 poses/images, including exact calibration/grid/recipe metadata
and pixel-center/resize/undistortion correspondence before any qualified MVS
worker. Preserve environments, saved scenes, failed sample_2 stages and frozen
fixtures; do not perform unchanged solves, fits or long-stress reruns. A0 begins
only after M3's exit, with no M4/M5/M6 implementation prerequisite.
