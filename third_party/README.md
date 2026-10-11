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
| gsplat | Wrapper targets 1.5.3, exact commit in adapter | Upstream dependency/weights audit pending | Not provisioned or executed |

Archive hashes and immutable vcpkg registry identity live in the build configuration.
The system-development graph differs from the unqualified release-vcpkg graph.
No proprietary driver binaries, pretrained weights, personal captures or worker
environments are redistributed. Release SBOM, transitive notices and worker/model
rights remain M6 gates. Do not infer clearance from a wrapper's license.
