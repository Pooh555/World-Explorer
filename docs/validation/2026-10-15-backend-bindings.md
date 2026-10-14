# Observed imports and viewer mappings — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Actual selected backend imports and native
empty-viewer library observations now supplement the earlier loader graph. No
source/rights, reconstruction, reference-performance or other-platform gate is
promoted. Cwd: `/home/Pooh555/programming/World-Explorer`; parent `ef8106d`;
implementation is the local commit containing this report. Raw ignored evidence
is `artifacts/validation/live-bindings-20261014/` (below **R**); capture began on
14 October and this handoff completed on 15 October.

## Identity and observations

[Public summary](../../third_party/backend-bindings-20261015.json) SHA-256:
`2f15958705976ebad9ac342eeaa2fa08024cffb18f5bdb7e8d1a3f13348f5ba2`.
Observer SHA-256:
`5ec194dd8bfd74847a3d33c21a56d9d063b14707f3fd910634956a61e68faeb5`.
The summary binds its observer, preceding loader summary and five exact raw
receipts. `R/identity.json` records checker/test/manifest, runners and viewer log
identities. Existing ELF roots/builds remain byte-identical; no C++ changes.

| Actual observation / immutable receipt in R | Exit / result |
|---|---|
| `.venv-train` fixed imports; `training-native-checked.json` | **0**, nine imports / **110 ELF files**, torch 2.4.1+cu124, gsplat 1.5.3+pt24cu124, prebuilt csrc and Triton 3.0.0/libtriton; CUDA 12.4 build, available **true**, count **1**, PyTorch CUDA context initialized **false** |
| Same training imports; `training-sandbox-checked.json` | **0**, same **110** ELF fingerprints, availability **false**, count **0**, context initialized **false**; sandbox device visibility differs |
| `.venv-worker` NumPy/Pillow/pycolmap; `poses-sandbox-checked.json` | **0**, three imports / **27 ELF files**; NumPy 2.2.6, Pillow 11.3.0, pycolmap 4.2.1 |
| `.venv-metrics` torch/torchvision/TorchMetrics; `metrics-cpu-sandbox-checked.json` | **0**, three imports / **54 ELF files**; CPU torch 2.4.1, torchvision 0.19.1, TorchMetrics 1.8.2; no model/metric invocation |
| Owned empty native viewer; `viewer-native-checked.json` / `.log` | **0**, first-frame observation / **47 ELF files**, normal **900-frame** exit, **0 resident splats / 0 Vulkan validation errors**, NVIDIA GeForce RTX 4060 Laptop GPU |

The union contains **208 ELF files / 4,920,157,588 backing-file bytes**. **71**
overlap the earlier graph with unchanged bytes; **137** are newly observed: 70
training, 12 pose, 35 metric ELF files and 20 system files. Viewer mappings add
18 system files, including NVIDIA GLX/allocator/shader libraries, Khronos
validation, libdecor, Wayland, XCB and xkbcommon. Training additionally maps CUDA
driver/NVML libraries; availability remains a separate observation. These are
mapped backing files, not definitive plugin/symbol binding or GPU execution of
every imported extension. Initial attribution preflight matches all **117** new
worker ELF hashes to retained distribution RECORD receipts; public owner/notice
associations and the **20** new system files remain next work. Raw preflight:
`R/new-mapping-attribution-preflight.json`.

## Actual checks and retained failures

`R/run_probes.py PROFILE CONTEXT` defines fixed argv, fresh isolated work paths,
clean environment and 120-second child deadline; four `*-checked-command.json`
receipts retain actual commands/exits/source hash. `R/observe_viewer.py
native-checked` uses `build/dev/we_viewer --frames 900 --settings WORK/settings.json
--input-trace WORK/input.jsonl`, selected display environment, first-frame maps and
bounded wait/reap of its own child. Native runs used authorized device access.
The [protocol](../protocols/backend_bindings.md) defines fixed import selection,
Python diagnostic guard, mapping identity/publication checks and qualification
limits. No package is installed, checkpoint deserialized, source video processed,
JIT fallback invoked or optimizer started.

| Commands, from root | Exit / checks / logs in R |
|---|---|
| `python3 tools/check_backend_bindings.py`, then `--local-evidence` | Both **0**, exact raw projection and current mapped hashes; `bindings-{0,1}.log` |
| `python3 tools/check_dependency_manifest.py`, then `--local-evidence`, then local `--require-closure` | **0 / 0 / expected 3**; all original **249** source/rights gap entries retained; `manifest-{0..2}.log` |
| Probe targeting the existing native training receipt | Expected **2** before imports, unchanged receipt hash; `existing-output-command.json` / `.log` |
| `python3 R/run_checks.py focused`; `workers` | **23/23 new / 115/115 worker cases** each on Python **3.14.8 / 3.12.15 / 3.10.22**, no local skips; `focused-*` / `workers-*` logs |
| `cmake --preset dev`; dev build `-j 4`; dev CTest | All **0**, GCC 16.2.1 C++26, **260/260**, 59.68 s; `dev-{0..2}.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; compatibility build `-j 4`; compatibility CTest | All **0**, explicit C++23 CPU **242/242**, 58.72 s; `compat-{0..2}.log` |
| Docs / boundaries / diff | All **0**, `static-commands.json` / logs |

The initial sandbox viewer exits **2**, `No available video device`, with no
mapping capture; `viewer-sandbox-initial.json` / `.log` retain the failure.
Training sandbox logs retain NVML visibility warnings. Native viewer/import
success does not rewrite those results. Preliminary receipts retain earlier
observer hashes; only `*-checked` receipts feed the final summary. Synthetic
checks cover malformed/bounded maps, wrong inode/device, replacement during hash,
in-place changes after hash, vanished mappings, blocked Python actions, immediate
torch-load guard, source/receipt/current-byte mutation, omitted inventory and
failed-viewer evidence. Seven inode/device cases are explicitly Linux-only;
other-platform execution is not claimed.

All **2,088 protected files / 94 tracked src/include files** match the parent and
current saved-run baseline; `R/preservation.json` / `protected-after.json`.
Videos, both completed scenes, failed sample_2 stages, recipes and installed
environments are retained. Byte totals are file inventory, not RSS/VRAM. No timing
here certifies performance. C++ formatting/tidy/sanitizers, reconstructed viewer
images, other hardware/platforms and unselected lazy provider/codec/kernel paths
were not rerun. Rights, archives, prescribed M1 fixture and full Q/P/N/E gates
remain unresolved; the next executable task is in [progress](../progress.md).
