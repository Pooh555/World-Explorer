# ADR 006: FP32 captured-domain raster composition

Status: accepted for local M1 development; portable/native qualification pending.

The raster renderer blended directly into SDR UNORM. A 27-splat stress fixture
shows premature clamping of bright rear SH contributions before foreground
attenuation: all seven views fail, maximum error 176–202 color levels. Clamping
individual colors would change the pinned trainer's appearance conventions.

Accumulate back-to-front splats in `R32G32B32A32_SFLOAT`, then use an input-attachment
subpass to clamp once for SDR, followed by UI. Keep captured display-encoded RGB;
this introduces no exposure fitting, gamma reinterpretation or linear-training
claim. [Native comparisons](../validation/2026-10-12-compositing.md) pass the same
fixture/tolerances after the change. An explicit read/store dependency covers the
written intermediate's end-of-pass discard; [Vulkan store semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html)
permit writes even with `NONE` when the attachment was written earlier.

Alternatives: UNORM loses energy; FP16 has a smaller range and different rounding,
so it is not an automatic unqualified fallback. The planned tiled FP32 compute
path remains the route to pinned exclusive termination and may avoid the raster
format-blending dependency. It is not implemented by this decision.

Trade-off: per-swapchain-image FP32 allocations add residency and bandwidth. Query
[attachment and blending support](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatFeatureFlagBits.html),
charge actual allocation requirements before allocating, and fail explicitly on
unsupported devices/budgets. Current verification covers NVIDIA/Arch only; this
neither waives Apple Silicon/AMD/Windows requirements nor certifies their formats.
Revisit with native portable devices, P2/P4 traces, qualified precision comparisons
and the tiled renderer. The retained exclusive-termination failure must be fixed;
do not relax its tolerance or patch the trainer to obtain a pass.
