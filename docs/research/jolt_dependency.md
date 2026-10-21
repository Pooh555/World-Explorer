# Jolt dependency selection — 2026-10-21

The [Jolt v5.3.0 release](https://github.com/jrouwe/JoltPhysics/releases/tag/v5.3.0)
is the version recorded by the repository's immutable vcpkg baseline
`ce613c41372b23b1f51333815feb3edd87ef8a8b` in its
[port metadata](https://github.com/microsoft/vcpkg/blob/ce613c41372b23b1f51333815feb3edd87ef8a8b/ports/joltphysics/vcpkg.json).
The [upstream license](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/LICENSE)
is MIT, with copyright and permission notice retention. The development source
archive is the official v5.3.0 tag archive, pinned in `cmake/dependencies.cmake`
as SHA-256 `e7f9621e480646c434150e1fbe3a9410f4ec4b04ffe54791e0678326b741b918`.
The ignored `.deps/` archive is a local build input, not a distributed asset.

The [upstream build](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/Build/CMakeLists.txt)
uses a C++17 Jolt target and lets the embedding project select CPU instruction
features, position precision, warnings, installation and interprocedural
optimization. This project builds Jolt as a static, single-precision, CPU-only
library with SSE2 as its x86 baseline. RTTI is enabled for Jolt in both
development and the vcpkg `rtti` feature so UBSan vptr checks can link; this
does not lower first-party C++26/C++23 targets. Jolt headers and target linkage
remain private to `collision`.

The pinned [vcpkg portfile](https://github.com/microsoft/vcpkg/blob/ce613c41372b23b1f51333815feb3edd87ef8a8b/ports/joltphysics/portfile.cmake)
builds a static target from the same tag and exports `Jolt::Jolt`, but leaves
upstream x86 instruction defaults in place. From the upstream defaults, the
release package may require AVX2 unless its recipe is constrained; this is an
inference from the portfile and is **not** a qualified release-profile result.
The portfile also disables its own warning option; first-party warning policy
is unchanged. Release-vcpkg and other native hosts remain unverified before M4.

The [upstream mesh settings](https://github.com/jrouwe/JoltPhysics/blob/v5.3.0/Jolt/Physics/Collision/Shape/MeshShape.h)
can sanitize degenerate/duplicate triangles and use one-sided simulation faces.
The adapter rejects any selected triangle loss and binds every shape to the
canonical scene/support digest. Ray queries are diagnostic until explicit wall
selection, known clearance and a kinematic controller are implemented.
