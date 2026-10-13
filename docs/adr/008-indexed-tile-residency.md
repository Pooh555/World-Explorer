# ADR 008: indexed tiles and bounded framebuffer residency

Status: accepted for local M1 development; native/reference performance qualification pending.

The full retained 304,867-splat scene with `--max-splats 1000000` exposed
[ADR 007](007-tiled-batch-compositing.md)'s global scan cost (~6 completed render
frames/s at 720p) and fullscreen memory failure. Three extra FP32 swapchain
attachments duplicated completed FP32 pixel storage; maximum-count scratch was
reserved even for smaller scenes. Raising the budget alone would retain both costs.

Implement specification §4.6's overlap count/exclusive scan, reference emission,
stable radix and tile ranges. Stable depth order before emission, followed by
stable tile sorting, preserves logical tile/depth/tie order without a 64-bit key.
Parallelize bucket scans. Preserve covariance, color domain, exclusive cutoff and
shared 256-splat pixel batches.

Grow scratch by resident count. Charge actual allocation requirements, share
reference capacity between completed frame owners and reserve SDR readback space.
Count before emission; recursively split oversized work into disjoint tile
rectangles, preserving resolution and contributors. Fail explicitly if one tile
cannot fit. Direct storage-to-SDR presentation clamps once in subpass 0 with UI,
removing duplicate FP32 attachments.

[Evidence](../validation/2026-10-13-viewer-regression.md) covers unchanged image
fixtures, forced partitioning, budget rejection and real 4K/fullscreen restoration
under the unchanged 512 MiB budget. Neither trimming nor lower resolution is a fix.

Trade-off: each counted rectangle waits on a host-visible GPU result; high-overdraw
frames can require multiple passes. Whole-frame references can exceed the bounded
per-batch list and P2's certified workload. Revisit with GPU latency traces,
fully asynchronous admission, LOD/footprint work and native AMD/Apple qualification.
This decision does not implement geometry, device-loss recovery or live reconstruction.
