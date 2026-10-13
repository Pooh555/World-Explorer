# Dependency inventory (development, not a release clearance)

| Component | Local selection | Source / notice | Qualification |
|---|---|---|---|
| Dear ImGui | 1.91.9b, SHA-bound archive | [MIT notice](notices/imgui-LICENSE.txt) | Native development use |
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
TorchMetrics/LPIPS are absent and AlexNet bytes are not provisioned, so no metric
package/backbone lock or Q2/Q3 result is claimed.

`workers/gsplat/fit.py` adapts initialization/settings from the pinned gsplat
example. The upstream attribution is Copyright 2025 Nerfstudio Team; the complete
license is retained. This development recipe does not use learned checkpoints,
the original restricted 3DGS rasterizer or auxiliary fused example extensions.
