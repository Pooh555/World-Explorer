# Acquisition-repair runtime refresh — 2026-10-16 Asia/Hong_Kong

Scope/status: selected dependency consistency **VERIFIED locally**; **M2/E2
IN_PROGRESS**, source/rights and release qualification open. Parent `964f2cf`;
records belong to the commit containing this report. Cwd: repository root.
Ignored raw evidence root **R**: `artifacts/validation/m2-acquire-runtime-20261016`.
No production/tool/test implementation, shader, environment or reconstruction
changes. [ADR 015](../adr/015-packed-lifetime-development-acceptance.md) separately
records the user's acceptance of the local packed lifetime check; no long rerun.

The preceding [acquire-repair report](2026-10-16-m2-acquire-timeout.md) retains
the local check's exit 2 on changed package-query-tool identity. Fresh capture
now binds current installed bytes and the rebuilt viewer. Historical selections,
summaries, raw observations and failed checks remain intact.

| New tracked record | SHA-256 |
|---|---|
| [Selection](../../third_party/runtime-selection-acquire-20261016.json) | `7a6aa1cd87ff1b96895fc62a9945cfcdb15875f600d09f4e565f336d5da88ce5` |
| [Loader summary](../../third_party/runtime-audit-acquire-20261016.json) | `ac2b13fcf055e0c6741dfc0b01c2396ad0468dfdfc48778400f1cbd563ea625f` |
| [Observed mappings](../../third_party/backend-bindings-acquire-20261016.json) | `db9225be107a4ad71e8d07b29af9b6ed5de7c9b2b14a4642467d167cb14ad315` |
| [Installed owners](../../third_party/binding-owners-acquire-20261016.json) | `ab20ed2886070007ec39d773471fe65b9c67b6b4c0757de1b20d9355ab7210a0` |
| [Selected inventory](../../third_party/dependency-audit-acquire-20261016.json) | `df0c69206a5377cac499335f80a03bc7e32ed45ecb6c843dc9ec00876c67b386` |

Actual commands/results:

- `python3 R/capture_loader.py`: **0**, **15.557 s**, bound invocation of
  `tools/audit_runtime_dependencies.py --selection
  third_party/runtime-selection-acquire-20261016.json --output R/runtime.json
  --require-resolved`. **15 roots / 213 ELF files / 644 inspections / 123 system
  packages / 96 notices / zero unresolved edges**. Only the selected viewer root
  changes its pin. Loader backing differences also include FreeType
  `6.20.6 → 6.20.7` and changed `libglycin-2.so.0` bytes; CLI bytes are unchanged.
  Total backing bytes **4,588,170,630** are an inventory sum, not RSS/VRAM.
- `python3 R/observe_viewer.py native-acquire`: **0**, **9.874 s**, native
  Linux/NVIDIA RTX 4060 Laptop using the existing GCC C++26 development binary.
  Its owned empty viewer completes **900 frames / zero resident splats / zero
  Vulkan errors**; **47 mapped ELF files** are observed after frame 1. Launcher,
  observer and binary identities remain unchanged through exit. No scene opens.
- `python3 R/prepare_summaries.py`: initial **1** on a stale assumption that the
  loader node set was unchanged, exposing FreeType's new pathname. The local
  recipe now records additions/removals explicitly; rerun **0**, verifying any
  already published summary for exact equality without replacing it. Five
  contexts retain **208 mapped files**, **137** beyond the fresh loader baseline,
  zero changed overlap. Versus the preceding packing mapping, **30 files are
  absent**, `libxcb-glx` is newly observed and only the viewer changes bytes.
  `summary-comparison.json` lists every difference. Four historical import
  receipts and current backing files are revalidated, without new worker imports.
- `python3 tools/audit_binding_owners.py --mapping
  third_party/backend-bindings-acquire-20261016.json --selection
  third_party/runtime-selection-acquire-20261016.json --output R/owners.json`:
  **0**, all **137 targets / 16 groups** (ten worker distributions, six system
  packages), **30 notices / 64 read-only queries**. `python3
  R/prepare_inventory.py` exits **0**, binding fresh summaries and preserving
  prior inputs/components/gaps; **230 evidence inputs**. Package-tool SHA is
  `590759eddb4c0cac6c5ad66f0f9bc093e15529675c0aa909eea396af5b07459d`.
- `python3 R/check_inventory.py`: **0**. The new manifest's default check exits
  **0**; `--local-evidence` exits **0** after backing-file/notice checks and fresh
  owner recapture; adding `--require-closure` exits expected **3**. Retains
  **84 components / seven profiles / 253 source/rights profile gaps**, with owner
  terms separately unqualified. Exact argv/cwd/exits/durations/log hashes are in
  `check-commands.json` (SHA `1ef51a6879a050c3dc7f4bb8ef8ab8952add42ca29b205fef4b159af3b1b071e`).

Active protocol commands select the new records. The narrower startup observation
does not remove earlier evidence or establish that absent libraries are never
loaded. It does not certify every lazy path, platform, complete SBOM or rights.
All **1,382 protected files** match the existing baseline (`R/preservation.json`),
including saved runs, sources, instructions and the specification/runbook.
Documentation and whitespace checks pass. No new build/CTest result is claimed
for this evidence-only slice; preceding renderer checks retain their own scope.
