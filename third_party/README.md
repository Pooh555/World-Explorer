# Dependency inventory (development, not a release clearance)

| Component | Local selection | Source / notice | Qualification |
|---|---|---|---|
| Dear ImGui | 1.91.9b, SHA-bound archive | [MIT notice](notices/imgui-LICENSE.txt) | Native development use |
| Vulkan Memory Allocator | 3.1.0, SHA-bound archive | [MIT notice](notices/vma-LICENSE.txt) | Renderer buffer ownership on local Vulkan profile |
| nlohmann JSON | 3.12.0, SHA-bound archive | [MIT notice](notices/json-LICENSE.txt) | CPU tests |
| Catch2 | 3.8.1, SHA-bound archive | [BSL-1.0 notice](notices/catch2-LICENSE.txt) | Tests only |
| SDL3 | Arch 3.4.18 | System package, not redistributed | Linux only |
| Vulkan loader / headers | Arch 1.4.363 | System packages, not redistributed | RTX 4060 local validation |
| OpenSSL | Arch 3.6.5 | System package, not redistributed | SHA-256 |
| FFmpeg / ffprobe | Arch n9.0.2 | System executables, not redistributed | Decode only; build flags not release-audited |
| pycolmap | 4.2.1 Python wheel | Official package in local isolated environment | CPU pose attempts; transitive wheel rights pending |
| NumPy / Pillow | 2.2.6 / 11.3.0 | Isolated pose environment | Local inspection and worker support |
| gsplat | 1.5.3 commit `937e299`, official pt24cu124 wheel | [Apache-2.0 notice](notices/gsplat-LICENSE.txt) | Real local development fit and CUDA regression |
| PyTorch / torchvision | 2.4.1+cu124 / 0.19.1+cu124 | Local installed wheels; not redistributed | Local NVIDIA only; transitive NVIDIA wheel notices/rights not release-audited |
| pycolmap training parser | Official 3.12.6 wheel | Isolated from pose environment 4.2.1 | Reads the local undistorted model |
| SciPy | 1.15.3 | Local wheel; not redistributed | Nearest-neighbor initialization and independent numerical check |

Archive hashes and immutable vcpkg registry identity live in the build configuration.
The system-development graph differs from the unqualified release-vcpkg graph.
No proprietary driver binaries, pretrained weights, personal captures or worker
environments are redistributed. Release SBOM, transitive notices and worker/model
rights remain M6 gates. Do not infer clearance from a wrapper's license.

The [installed identity audit](../docs/protocols/worker_identity.md) checks 37
training and three pose distributions against these selected environment files.
The [2026-10-13 compact receipt](../workers/identity/local-development-20261013.json)
records metadata/RECORD/installed-file/notice hashes. All listed file hashes match;
76 notice files were identified. Triton 3.0.0 has no separate notice identified in
its installed RECORD. Original wheel archive hashes, transitive rights and complete
runtime/SBOM qualification remain pending; installed hashes are not archive locks.
That historical receipt's metric absence is superseded by the separate
[CPU metric profile](../workers/identity/metric-evaluation-cpu-20261014.json):
TorchMetrics 1.8.2, torch 2.4.1+cpu, torchvision 0.19.1+cpu and explicit AlexNet
bytes now have identities and independent synthetic parity. Training environments
remain unchanged; model/dataset/transitive rights and Q2/Q3 qualification remain
pending.

The [current selected-component inventory](dependency-audit-20261016.json) records
source/archive/notice gaps for 84 components across seven profiles. Its
[offline checker](../tools/check_dependency_manifest.py) rejects stale or incomplete
evidence; pass `--manifest third_party/dependency-audit-20261016.json` for the
current inventory. The checker's default remains the historical 2026-10-14
snapshot. All five native development archive hashes pass; their selected notices
match, with one explicitly recorded first-line trailing-space normalization in
JSON. The supplemental [Triton 3.0.0 source notice](notices/triton-3.0.0-LICENSE.txt)
traces the exact tag commit; installed-wheel notice/source binding and embedded
dependency terms remain unresolved. The observed system FFmpeg is GPL-enabled,
not the proposed minimal LGPL release build. The release-vcpkg graph remains
unresolved; this inventory grants no redistribution clearance. See the
[protocol](../docs/protocols/dependency_sources.md) for scope and checks.
The [VMA runtime refresh](../docs/validation/2026-10-16-vma-runtime-refresh.md)
recaptures both rebuilt development executables and the empty native viewer.
The full current `--local-evidence` check passes; strict closure exits 3 for
253 explicit source and rights gaps.

The [runtime supplement](runtime-audit-20261016.json) records fifteen selected
Linux loader contexts, 213 ELF fingerprints, 123 system package owners and 96
notice files. Its [selection](runtime-selection-20261014.json) pins roots and
explicit extension library paths. The existing local inventory check now rehashes
those ELF/notice/interpreter bytes against the raw receipt. This observes controlled
loader contexts; actual Python imports, lazy GPU/ICD/provider/codec loading,
original archives and source/bundled rights remain unqualified.
[Runtime protocol](../docs/protocols/runtime_dependencies.md).

`workers/gsplat/fit.py` adapts initialization/settings from the pinned gsplat
example. The upstream attribution is Copyright 2025 Nerfstudio Team; the complete
license is retained. This development recipe does not use learned checkpoints,
the original restricted 3DGS rasterizer or auxiliary fused example extensions.
