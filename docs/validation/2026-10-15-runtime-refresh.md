# Runtime evidence after system drift — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Fresh controlled loader, fixed backend import,
empty native viewer and installed-owner captures restore the active full local
dependency check. No source/rights, reference-performance, reconstructed-scene or
other-platform qualification follows. Cwd: repository root; parent `463cd9d`;
implementation is the local commit containing this report. Raw ignored evidence:
`artifacts/validation/runtime-refresh-20261015/` (**R**).

## Compared identities

| Current public record | SHA-256 / observed scope |
|---|---|
| [Loader](../../third_party/runtime-audit-20261015.json) | `1808e6d4b3cb8565bac8bf754bbce15f1dd32979b270316392d154f6e20bb789`; **15 roots / 213 ELF files / 123 system packages / 96 notices / 644 inspection commands**, zero unresolved edges, **4,586,900,326 bytes** |
| [Mappings](../../third_party/backend-bindings-refresh-20261015.json) | `f17411105897c70710aaa190b8d342f33c174d9f67808add214d3c0dd5f04235`; **five observations / 208 ELF files / 4,920,161,684 bytes** |
| [Owners](../../third_party/binding-owners-refresh-20261015.json) | `806f224c7e5fe5c97edfcf0f45469459b437958f78ff6620c199b3bd908f16d1`; **137 ELF files / ten worker distributions + six system packages / 30 notices**, unchanged target hashes/associations |

All fifteen selected root pins and first-party/worker ELF bytes are unchanged.
Only the observed system changes differ from the preceding loader: snappy
**1.2.2-3 → 1.3.1-1**, replacing `libsnappy.so.1.2.2` with `libsnappy.so.1.3.1`,
and zlib **1:1.3.2-3 → 1:1.3.2-4**, changing bytes at `libz.so.1.3.2`.
**211** common loader files and **207** common mapped files retain their hashes.
`R/comparison.json` records exact added/removed/changed paths and summary hashes.

The new mapping summary retains the old loader as its comparison baseline:
**137 previously unseen files / one changed overlap**. That makes the zlib change
explicit. Fresh owner capture binds these current mappings and rechecks the
unchanged new targets. The active dependency manifest now references the three
fresh records; previous summaries, hashes and raw receipts remain untouched.
Its original **249 source-component gap entries**, plus supplemental association/
system gaps, remain unresolved; no rights flag or acceptance threshold changes.

## Actual observations and checks

`R/capture-command.json` records the loader command/cwd/exit/source identity;
elapsed time was not recorded. `R/run_probes.py` defines fixed clean environments,
owned fresh work paths and 120-second deadlines. Each `*-current-command.json`
records argv, cwd, exit and observer hash. `R/observe_viewer.py native-current`
uses the existing 900-frame empty-viewer recipe with isolated settings/trace.
Native device/display access was used for native observations. Source/recipe/log
identities are in `R/identity.json`; no auditing implementation changed.

| Commands / receipts | Actual result / logs in R |
|---|---|
| `python3 tools/audit_runtime_dependencies.py --selection third_party/runtime-selection-20261014.json --output R/runtime-current.json --require-resolved` | **0**, current snappy/zlib dependency contexts resolve; immutable raw receipt |
| Fixed training imports, native and sandbox | Both **0**, **110 ELF files each**, prebuilt gsplat/Triton imports. Native CUDA available/count **true/1**, sandbox **false/0**; CUDA build 12.4, PyTorch CUDA context initialized **false** in both; `training-*-current.*` |
| Fixed pose / CPU metric imports | Both **0**, **27 / 54 ELF files**; `poses-sandbox-current.*`, `metrics-cpu-sandbox-current.*` |
| Empty native viewer | **0**, **47 mapped ELF files**, **900 frames / zero resident splats / zero Vulkan validation errors**, NVIDIA GeForce RTX 4060 Laptop GPU; `viewer-native-current.json` / `.log` |
| Refreshed owner check, default/local | Both **0**, current metadata/RECORD/notices and repeated read-only package queries; `owners-{0,1}.log` |
| Active manifest check, default/local/local strict closure | **0 / 0 / expected 3**; raw/summary and all active current bytes agree; `manifest-{0..2}.log` |
| Same local check with retained `R/dependency-before.json` | Expected **2**, absent old snappy path; `historical-0.log`. Old failure remains reproducible |
| Dev configure/build `-j 4`/CTest | All **0**, GCC 16.2.1 C++26 **260/260**, 57.42 s; `dev-{0..2}.log` |
| `compat-cxx23 -DWE_BUILD_VIEWER=OFF` configure/build `-j 4`/CTest | All **0**, explicit C++23 CPU **242/242**, 56.74 s; `compat-{0..2}.log` |
| Docs / boundaries / diff | All **0**, `static-commands.json` / logs |

Both CTest runs include **133/133 worker cases** on Python **3.14.8**; detailed
output is retained in `ctest-{dev,compat-cxx23}-detail.log`. Other interpreter
suites, C++ formatting/tidy/sanitizers and performance repetitions were not rerun
for this evidence-only refresh. Byte totals are backing-file inventory, not
RSS/VRAM. The viewer's generic `fixture: user-selected` output is labeled **empty
viewer** here; it does not stand in for a real reconstruction gate.

All **2,088 protected files / 94 tracked src/include files**, the three historical
summaries and their bound raw receipts remain unchanged (`R/preservation.json`).
No source, job, failed stage, fit, scene, model, recipe or installed environment
was changed; no installation, checkpoint/model loading, optimizer, real video job,
upload or push occurred. Archives/rights, unobserved lazy paths, prescribed M1
fixture and native/reference platforms remain pending. The Windows roundtrip
correction still needs its native rerun. [Progress](../progress.md) owns next work.
