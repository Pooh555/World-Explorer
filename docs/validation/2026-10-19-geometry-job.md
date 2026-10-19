# Retained-cache geometry job — 2026-10-19 (Asia/Hong_Kong)

M3 stays **IN_PROGRESS**. A Linux development job now owns one pinned, retained
native visibility cache and an isolated partial TSDF stage. It checks source
planes and pose identity before work, binds the worker/coordinator/recipe, holds
a nonblocking lock, supervises cancellation, preserves attempts, and reuses an
exact completed stage. It does not produce fresh depth, publish a native scene,
establish measured scale, complete geometry, qualify Q5/N2 or start A0.

Repository root is the cwd. Parent `b810c2505f88c47c62a89f3a8fe5388536c578af`;
implementation file `tools/geometry_job.py` SHA-256
`a210421392ff12c090a9ad50ae95eb1155ee2f60497bc2ee53ae13bf182ff6ea`.
Linux 7.2.9 x86_64, isolated CPython 3.12.15, existing Open3D CPU environment.
No new packages, models, depth solves, fit, scene changes or downloads. Ignored
evidence is `R=artifacts/validation/m3-geometry-job-20261019`; its
`final-code-results.json` and named logs retain the exact timed exits and argv. Inputs are
the frozen [observed fusion requests](2026-10-18-observed-fusion.md), fixed recipe
SHA `2fa967039db8db0b67d0ea6903b05e933d0f3105ae20253137df273287349aea`,
temporal/parallax native cache receipts `c4b4ca96fe5668a012ac0477e3fffbeeb0a19559be73de2a4f1101b81d8a6cb0` /
`040064ada4d454820ec060c985974b9823dd62e6d83c7abacd54a9b23eace30d`,
and pose SHA `d0383cb0510d82c7c92cb8184f1214c688bac11619ab65b232c9ec3d66db3abf`.

The real command form for each run was:

```sh
artifacts/environments/fusion-cpu/bin/python tools/geometry_job.py \
  artifacts/validation/m3-observed-fusion-20261018/final/NAME-request.json \
  REQUEST_SHA \
  artifacts/validation/m3-depth-visibility-20261018/NAME-cache/poses.weposes \
  d0383cb0510d82c7c92cb8184f1214c688bac11619ab65b232c9ec3d66db3abf \
  artifacts/validation/m3-geometry-job-20261019/NAME-job-final-code
```

`NAME/REQUEST_SHA` were `temporal/520e1a86018a7c015f67e6bfe11ae5c6397dd22bc6eebcd92b5c1f76fceab746`
and `parallax/bf91fba1a2e95edc5333ce2d559bc1c11d8c862897db9cd7a0a87a1d6379ea41`.
The test harness used this exact argument array through `subprocess`, with
SIGTERM to the parallax coordinator at seven seconds. All outputs stayed under
`R`; no retained prior job was overwritten.

| Actual local check | Result |
|---|---|
| Temporal first/replay | Exit 0/0 in 6.333/0.916 s; same stage, 159,135 observed voxels, 32,074 vertices, 47,559 triangles |
| Foreign occupied output / wrong pose pin | `R/final-adverse-results.json`: exits 2/2; sole `keep.txt` entry and its SHA remained unchanged, no lock or owner created; all-zero pose pin rejected |
| Parallax cancel/resume/replay | Exit 130/0/0 in 8.018/16.712/2.270 s; no job terminal after cancel, supervisor reported cancellation and reaped child; resumed terminal has 256,592 observed voxels, 61,598 vertices, 82,248 triangles |
| Synthetic interrupted-stage replay | `R/final-adverse-results.json` records the exact command: exit 0, untouched marker in attempt 0, complete selected attempt 1; synthetic fixture, no claim that the cancelled real worker wrote a partial file |
| Synthetic corrupt completed stage | Same record: one copied stage asset byte changed; exit 2, altered asset left unchanged, no new attempt or receipt |
| Stage/owner pins | Temporal owner `b4d8e5b3a589d0f129960eea7ee72537e3d3ea030247f8886b0905eb81d5d5ee`, stage manifest `382f848a89a2654285ba4fa7dab7e9d4ecaa93ee0983d4bd7c8b6a02fa00ba37`; parallax owner `0f7ddd13371e4531099ec2abd3f49092ab06822b9623ef2d8191a708107b3295`, stage `0f31cc3c21c18e612468fd0e30005cfc743177c6a1315e8746f331280faadbf4` |
| Isolated fusion regressions | `artifacts/environments/fusion-cpu/bin/python -m unittest discover -s tests/integration -p 'worker_fusion_test.py' -v`: exit 0, 8/8, no skips |
| Independent native admission | `R/final-native-checks.json` contains exact converter and `build/dev/we_process inspect-fusion` argument arrays; both conversions and both pose-bound native inspections exit 0. New `.wefusion` SHA-256: temporal `9119213daeef47e76b1efab83c242bab29ddb333e4965eb29db7a4d0ef15f06d`, parallax `6c69bfca332da388bbf719bcd9b8ed3bf30f8df7ea7f7522ed96ce69ae98ccdd` |
| Source/document gates | `python3 -m py_compile tools/geometry_job.py`, `python3 tools/check_docs.py`, `git diff --check`: all exit 0; documentation links 914 local targets in 186 files |

The cancellation happened while fusion was running; the worker reported
`CANCELLED` and exit 130. In this run its output directory had not yet been
created, so restart used attempt 0. A separate labeled synthetic partial stage
exercised preservation and attempt 1; real populated partial-stage recovery
remains unobserved.
Those are single development observations, not performance qualification. The
original retained fusion runs remain separate evidence. The new stages convert
and pass independent native pose admission, but the job does not own that
conversion or hand off to the scene update path. Fresh depth solving and full
application/GUI ownership remain open.
