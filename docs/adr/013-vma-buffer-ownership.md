# ADR 013: VMA ownership for renderer buffers

2026-10-15. Status: accepted for M2 development; release qualification pending.

`render_vulkan` previously created each `VkBuffer`, selected a memory type,
allocated/bound `VkDeviceMemory`, and mapped host-visible buffers directly. The
M2 roadmap calls for VMA without changing the scene/update boundary or the
512 MiB residency contract. The immutable vcpkg baseline already resolves
`vulkan-memory-allocator` 3.1.0. Pin the same upstream release archive by
SHA-256 for system-library development builds; only `render_vulkan` links it.

One VMA allocator belongs to each Vulkan device. A move-only buffer owns its
`VkBuffer` and `VmaAllocation`; it is destroyed before the allocator/device.
Initial VMA allocations request dedicated memory. This retains the existing
per-buffer requirement-sized accounting and prevents allocator block slack from
escaping the 512 MiB budget. Preflight checks the Vulkan buffer requirements;
the returned VMA allocation size is checked again, with RAII cleanup on failure.
Host-visible coherent staging stays persistently mapped. Completed timeline
values still decide when retired buffers can be freed.

Shared VMA pools and defragmentation are deferred until a measured need and an
admission model for whole-block bytes exist. Dedicated allocations retain the
current allocation-count cost. Revisit pooling if large-scene traces show this
cost is material, then requalify budgets, rejection paths and the two-hour
lifetime stress. The current local system-dependency builds do not certify the
separate `release-vcpkg` profile or other devices.

Sources: [VMA release](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases/tag/v3.1.0),
[immutable vcpkg port](https://github.com/microsoft/vcpkg/blob/ce613c41372b23b1f51333815feb3edd87ef8a8b/ports/vulkan-memory-allocator/vcpkg.json).
