# Runtime receipts after VMA integration — 2026-10-16 Asia/Hong_Kong

Scope/status: selected dependency consistency **VERIFIED locally**;
**M2/E2 IN_PROGRESS**, rights unqualified. Parent `d26485e`; cwd repository
root. The rebuilt viewer and CLI had invalidated the earlier runtime byte
receipt. A fresh loader capture and native empty-viewer observation now restore
the full current local inventory check. Earlier selections, summaries, raw
receipts and reports remain untouched. Raw outputs: ignored
`artifacts/validation/m2-vma-runtime-20261016/` (**R**).

| New tracked record | SHA-256 / scope |
|---|---|
| [Selection](../../third_party/runtime-selection-20261016.json) | `3964575dde857fab89155d682970d78e32faa0e29f10eb47425e4cfaaae43954`; only viewer/CLI pins change |
| [Loader summary](../../third_party/runtime-audit-20261016.json) | `5e8ce0cc28b99323b6e70ebcbb685fffc5982424dd2ea64b60f3917b600a1977`; 15 roots, 213 ELF files, 123 system packages, 96 notices, 644 inspection commands, zero unresolved edges |
| [Mapping summary](../../third_party/backend-bindings-vma-20261016.json) | `1ced5a292fbc26fb6d48508aea24dc95b3f42c875a04ab5fd772fe80ded18efe`; five observations, 208 mapped ELF files, 137 beyond the current loader baseline, zero changed overlap |
| [Selected inventory](../../third_party/dependency-audit-20261016.json) | `65c06c092c06db71ab50bff1502ef07b5a8e0c5daee99ddf77cca1e6f9ae8fc7`; 84 components, seven profiles, 253 explicit source/rights gaps |

Actual observations:

- `python3 tools/audit_runtime_dependencies.py --selection third_party/runtime-selection-20261016.json --output R/runtime.json --require-resolved`: exit **0**, **20.519 s**. `R/capture-command.json` records the argument array, cwd, exit, elapsed time and unchanged auditor hash. Exactly two of 213 backing files differ from the previous loader receipt: `build/dev/we_viewer` and `build/dev/we_process`; no library was added or removed. All 211 remaining files retain their fingerprints. Total backing-file inventory is 4,587,592,166 bytes, not RSS/VRAM.
- `python3 R/observe_viewer.py native-vma`: exit **0**, **8.131 s**. Native Wayland/NVIDIA RTX 4060 Laptop, driver 615.78.08; **900 frames, zero splats, zero Vulkan errors, 47 mapped ELF files**. The log explicitly records an empty viewer. Its only changed mapped backing file is the rebuilt viewer executable. The initial native permission review timed out before launch; one retry succeeded.
- `python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-20261016.json`: exit **0**. Adding `--local-evidence` also exits **0** after rehashing the bound/current payloads. Adding `--local-evidence --require-closure` exits expected **3** for 253 open gaps; `R/manifest-strict.log` retains the result.

The four retained training/pose/metric import observations were not recaptured;
their raw receipts and all current backing bytes were rechecked. The existing
owner summary still binds its original mapping and the same unchanged 137 target
files, 16 groups and 30 notices; its full local recapture/check passes through
the inventory validator. No worker import, fit, model load, environment install
or reconstruction ran for this refresh.

Historical loader/mapping/owner summary hashes remain respectively
`1808e6d4b3cb8565bac8bf754bbce15f1dd32979b270316392d154f6e20bb789`,
`f17411105897c70710aaa190b8d342f33c174d9f67808add214d3c0dd5f04235`,
`806f224c7e5fe5c97edfcf0f45469459b437958f78ff6620c199b3bd908f16d1`.
Original sources, bundled/transitive rights, lazy unobserved paths, release-vcpkg,
other platforms and complete Q/P/N/E qualification remain open. This refresh
changes evidence records only; the VMA binary identity used by the completed
[two-hour stress](2026-10-16-m2-vma-lifetime.md) is preserved.
`python3 tools/check_docs.py` exits 0 (609 local targets/127 files), and
`git diff --check` exits 0. No production code or test implementation changed;
the existing 267-test/build evidence remains scoped to its recorded run.
