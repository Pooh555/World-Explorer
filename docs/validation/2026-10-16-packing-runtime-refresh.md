# Renderer runtime receipt refresh — 2026-10-16 Asia/Hong_Kong

Scope/status: selected dependency consistency **VERIFIED locally**;
**M2/E2 IN_PROGRESS**, source/rights and release qualification open. Parent
`8d1f631`; records are the commit containing this report. Cwd: repository root.
Ignored raw evidence root **R**: `artifacts/validation/m2-packing-runtime-20261016`.
No production/tool/test implementation, shader, dependency environment, recipe
or reconstruction changes. This refresh supersedes active receipt references;
all historical selections, summaries, raw evidence and failures remain intact.

The preceding local inventory check exits **2**, `Current runtime payload
differs` (`previous-current.log`). A fresh selection changes only the actually
rebuilt `build/dev/we_viewer` and `build/dev/we_process` hashes. The native viewer
is observed again; four existing worker-import receipts are retained and checked
against their current backing files. No worker import or model load is requested.

| New tracked record | SHA-256 |
|---|---|
| [Selection](../../third_party/runtime-selection-packing-20261016.json) | `8106e51c01136c099e27547af3b67004d88f089c9833f99b648b2428eb54e4a2` |
| [Loader summary](../../third_party/runtime-audit-packing-20261016.json) | `e62440518c951d31ef9917762c0e15bc2c8da056e474ad6676d72d8a5be9f842` |
| [Observed mappings](../../third_party/backend-bindings-packing-20261016.json) | `62b10fdd76c188b21b3c19d2b73ab001a04286565f5076a975648d7f9615543d` |
| [Installed owners](../../third_party/binding-owners-packing-20261016.json) | `b7cebd26cd9f2265a32d5350601b39c42051fd38542e2c893d04ff0691d6e7f0` |
| [Selected inventory](../../third_party/dependency-audit-packing-20261016.json) | `cb6ee08ea6e4612cebb58cdc84077bc213a27f8d88336fc1a33727435d5f1b9f` |

Actual commands/results:

- `python3 R/capture_loader.py`: **0**, **14.354 s**. Its bound argument array
  invokes `tools/audit_runtime_dependencies.py --selection
  third_party/runtime-selection-packing-20261016.json --output R/runtime.json
  --require-resolved`. Results: **15 roots / 213 ELF files / 644 inspection
  commands / 123 system packages / 96 notices / zero unresolved edges**. The
  viewer/CLI are the only changed backing files; all **211** other files and
  the complete node inventory match the previous loader capture. Total backing
  bytes **4,588,079,446** are an inventory sum, not RSS or VRAM.
- `python3 R/observe_viewer.py native-packing`: **0**, **8.135 s**, native Linux
  Wayland/NVIDIA RTX 4060 Laptop, driver **615.78.08**, existing GCC 16.2.1
  C++26 development binary. Its isolated empty viewer completes **900 frames /
  zero resident splats / zero Vulkan errors**. The collector observes **76 ELF
  files** after a rendered frame, with launcher/observer/binary hashes unchanged
  through actual exit. It opens no saved scene and attaches only to its owned
  child. `viewer-native-packing.json/log` retain context and result.
- `python3 R/prepare_summaries.py`: **0**. The five-context mapping now includes
  **237 files**, **160** beyond the fresh loader baseline and zero changed
  overlap. Against the preceding mapping, **30 files are newly observed**, one
  former file (`libxcb-glx`) is absent at this observation, and only the viewer
  executable changes bytes. `summary-comparison.json` retains all filenames.
  The broader observation includes EGL/Mesa/LLVM libraries; their presence does
  not establish another rendering device/backend or AMD qualification.
- `python3 tools/audit_binding_owners.py --mapping
  third_party/backend-bindings-packing-20261016.json --selection
  third_party/runtime-selection-packing-20261016.json --output R/owners.json`:
  **0**. Every one of the **160 targets** has an installed association in
  **31 groups** (ten worker distributions, 21 system packages), with **41
  notices** and **170 read-only package queries**. Missing separate notices and
  source/bundled-term gaps remain explicit. `prepare_inventory.py` projects the
  raw owner receipt and binds all four new supplements without removing any
  original inventory input/component or profile gap.
- `python3 R/check_inventory.py`: **0**. Its exact argv/cwd/exits/durations/log
  hashes are in `check-commands.json`. The new manifest's default check exits
  **0**; `--local-evidence` exits **0** after fresh owner recapture and full
  backing-file/notice/receipt checking; adding `--require-closure` exits expected
  **3**. Results retain **84 components / seven profiles / 253 profile gaps**.
  Owner source/terms gaps remain separately recorded; no rights gate is upgraded.

The active protocol commands now select these receipts explicitly. Historical
tool defaults and old raw observations remain replayable with their original
scope. Documentation links and `git diff --check` pass. No new build/CTest pass
is claimed for evidence-only changes; the preceding
[packing-quality checks](2026-10-16-m2-packing-quality.md) retain their own scope.

All **1,382** current protected files match the preceding inventory, including
three saved runs, sources, instructions and existing user reports. Pre-existing
tracked edits remain outside the focused commit. No solver, optimizer/checkpoint,
provisioning, download, upload or push runs. A fresh mapping remains one observed
startup context, not every lazy kernel/codec path, a complete SBOM or platform
certification. Packed lifetime, prescribed Q6 corpus/scale and reference/platform
gates remain open; [progress](../progress.md) records the next executable check.
