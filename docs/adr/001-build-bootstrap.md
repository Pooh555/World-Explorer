# ADR 001: deterministic development bootstrap, separate release qualification

2026-10-11. Status: accepted for development; release graph unverified.

The new repository has no dependency manager checkout. Arch provides SDL 3.4.18,
Vulkan 1.4.363, OpenSSL 3.6.5, GCC 16.2.1 and Clang 23.1.1. Use these through
explicit `WE_SYSTEM_DEVELOPMENT_DEPS=ON` presets and SHA-256-bound source archives
for JSON 3.12.0, Catch2 3.8.1 and ImGui 1.91.9b. This allows immediate native work
without modifying global packages. It is not a reproducible release dependency graph.

`release-vcpkg` resolves one immutable upstream registry baseline. That graph and
its full transitive licenses must be built and audited before release. No silent
fallthrough between these modes. The development ImGui version predates the
texture API transition and has an inspected SDL3/Vulkan backend. Upgrade only with
native resize/HiDPI checks. Vulkan allocations initially use a direct RAII adapter;
VMA/volk adoption remains tracked, not claimed complete.

Primary sources: [JSON](https://github.com/nlohmann/json/releases/tag/v3.12.0),
[ImGui](https://github.com/ocornut/imgui/tree/v1.91.9b),
[Catch2](https://github.com/catchorg/Catch2/releases/tag/v3.8.1),
[vcpkg baseline](https://github.com/microsoft/vcpkg/tree/ce613c41372b23b1f51333815feb3edd87ef8a8b).
