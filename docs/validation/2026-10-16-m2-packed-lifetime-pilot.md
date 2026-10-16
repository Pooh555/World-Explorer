# Packed lifetime pilot and long-run launch — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/P4 IN_PROGRESS**. Experimental packed-SH lifetime selection
and short native regressions pass. The separately bound **7,200-second packed
run is running; long lifetime remains IMPLEMENTED_UNVERIFIED**. Parent `d2fc58c`;
implementation is the commit containing this report. Cwd: repository root.
Ignored evidence root **R**: `artifacts/validation/m2-packed-lifetime-20261016`.

Only the lifetime diagnostic and documentation change. Optional `--fp16-sh`
selects the existing experimental GPU profile; default remains FP32. Both this
flag and `--resident-reuse` may occur in either order, with duplicate/unknown
options and invalid integer durations rejected before SDL/output creation.
Start/summary metadata bind the actual storage profile, stride and payload.
Resident-mode completion now requires exact host/device transfer totals for the
full/full/empty revision sequence. Full payloads use the selected **160/256-byte
stride**; each empty revision retains its **16-byte zero sentinel**. Production
renderer, shaders, canonical assets, reconstruction and worker profiles stay
unchanged. No gate threshold is adjusted.

Fixture: complete retained **sample_1 / 295,952 splats**, no LOD reduction,
scene content SHA-256
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`,
manifest SHA-256
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`.
Updates retain or clear this immutable real appearance in memory; the alternating
empty revisions are **synthetic diagnostics**. Source-view spectator and 720p
readbacks are fixed; resizes cycle through the existing 1024×640, 1600×900,
1920×1080 and 1280×720 extents. Native Linux Wayland/NVIDIA RTX 4060 Laptop,
driver **615.78.08**; GCC **16.2.1 C++26 Debug**, independent Clang **23.1.1 C++23**.

Actual sequential native pilot results, all exit **0** with zero Vulkan errors:

| Profile / mode | Render-loop seconds | Visible updates / resizes | Host / resident copy bytes | Peak tracked owned bytes |
|---|---:|---:|---:|---:|
| GCC packed / full-full-empty | **60.314** | **3 / 2** | **94,704,656 / 47,352,320** | **310,904,640** |
| GCC FP32 / full-full-empty | **35.548** | **3 / 1** | **151,527,440 / 75,763,712** | **396,138,816** |
| GCC FP32 / legacy default | **10.232** | **0 / 0** | Timings disabled | **244,611,392** |
| Clang packed / full-empty | **10.243** | **0 / 0** | Timings disabled | **216,200,000** |

Every run retains **two matching captures**. Packed baseline/final hash:
`eb000612a21f9fbe894b1c09be9cf6e2c0dc6a5a380ecde1e8b594f93897a98b`.
FP32 images retain the preceding FP32 stress hash; packed/FP32 images are not
required to be byte-identical to one another. The packed pilot submits **2,695
frames**, process HWM **444,547,072 bytes**, under the unchanged 512 MiB owned
allocation cap. The 35-second run exercises empty-to-full restoration at expiry.
These durations/counts are bounded diagnostic observations, not P2/P3 performance
or whole-device-memory qualification; CPU checks overlap part of the pilots.

Actual commands/results:

- `cmake --build --preset dev -j 4`: **0**, `build-dev-final.log`.
- `ctest --preset dev --output-on-failure`: **273/273**, **70.25 s**,
  including 149 worker cases; `ctest-dev.log`.
- `cmake --build build/m2-vma-clang -j 4 --target we_gpu_lifetime_stress`:
  **0**, `build-clang.log`; its packed default run also passes above.
- `clang-format --dry-run --Werror tests/gpu/lifetime_stress.cpp` and
  `clang-tidy -p build/dev tests/gpu/lifetime_stress.cpp`: **0**, version 23.1.1,
  no first-party diagnostics; `format.log`, `tidy-final.log`.
- `python3 R/run_pilots.py`: **0**, four complete native argv/exits/durations,
  source/binary/shader/manifest identities in `pilot-launch.json` and
  `pilot-exit.json`; `pilot-runner.log` retains each result.
- `python3 R/audit_run.py pilots`: **0**, independent profile/stride/full-count,
  visible-revision/resize/transfer-total and every readback check;
  `pilots-audit.json`, `pilot-audit.log`. Eight pre-SDL exit-2 CLI probes retain
  exact arguments/errors and absence of output creation in `cli-rejections.json`.
- `python3 tools/check_dependency_manifest.py --manifest
  third_party/dependency-audit-packing-20261016.json --local-evidence`: **0**,
  `dependency-current.log`; diagnostic-only rebuild leaves refreshed receipts
  current. Documentation/boundaries/diff checks pass. No sanitizer or other
  platform pass is inferred.

The detached owned supervisor starts `build/dev/we_gpu_lifetime_stress
artifacts/sample_1/scene.wexplore R/packed-full 7200 --resident-reuse --fp16-sh`
at **2026-10-16 18:34:11 Asia/Hong_Kong**. `supervisor-start.json` records its
owned PIDs; `full-launch.json` binds actual argv/cwd, all tracked production
sources, diagnostic source, executable/archive, compiled shaders, saved-scene
files and the supervisor. `full-exit.json` will record actual termination,
timeout, log/launch hashes and changed-file detection. No completion is claimed
from its successful start or the pilot. The [handoff](../progress.md) requires
exit 0 and `python3 R/audit_run.py full` before any long-run qualification.
Do not rebuild its bound files while it runs.
The sandbox cannot see those native PIDs; a native owned-process check confirms
both supervisor and renderer alive at **18:36:44**, revision 11 observed and no
exit receipt (`running-status.json`). That namespace-limited check is not a
native renderer failure.

Full-launch SHA-256:
`12aa4452e32ef4b7c4e7853911496c0f6c294ad1378059bca8c40882164da743`.
Bound executable SHA-256:
`f7bb1b7901ffbd277500104dd7770984c8a8ec1f93defb37c406d7b387dcea26`.
Independent auditor SHA-256:
`d7f614827207f18dc83908b92fbec7067b85074580a22a477df0399ea22deb84`.

Protected saved runs, sources, instructions and existing user reports match the
**1,382-file** inventory; user edits remain outside the focused commit. No solve,
fit, optimizer checkpoint load, environment change, download or upload occurs.
Q6's prescribed fixed 100-view captured-target corpus, scale/platform/reference
qualification and full Q/P/N/E gates remain open.
