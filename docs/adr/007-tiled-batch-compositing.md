# ADR 007: bounded front-to-back tile batches

Status: accepted for local M1 development; overlap-index/performance qualification pending.

[ADR 006](006-fp32-capture-compositing.md) preserved bright captured radiance, but
back-to-front blending included contributions excluded by pinned gsplat's
`next_T <= 1e-4` cutoff. Five unchanged near-opaque views exceeded the fixed
maximum error (12–15 color levels versus eight allowed).

Sort center depths stably front-to-back. Each 16×16 compute workgroup scans fixed
256-splat batches, stably compacts intersecting contributors into shared memory,
then accumulates pixels in FP32. Match the pinned exclusive cutoff, alpha clamp,
threshold, tile bounds and background. No contributor-count-dependent scratch or
per-splat dispatch loop is required. Per-frame pixel buffers are budgeted using
actual allocation requirements; resize retires them at the existing idle boundary.
Keep FP32-to-SDR presentation and overlay subpass 1. FP32 attachment blending is
no longer required. Retire the unused quad shaders.

[Native evidence](../validation/2026-10-12-tiled-composition.md) covers unchanged
fixtures, multiple batches, budget failures and the full retained scene. Clamping
individual colors, changing tolerances or patching the trainer would change the
comparison rather than fix composition.

Trade-off: this development path scans the sorted list per active tile and adds
two FP32 pixel buffers. It does not replace specification §4.6's planned bounded
overlap count/scan/reference index or certify P2/P3. Center-depth ordering remains
approximate. Revisit after calibrated real-scene comparison, overlap-index
implementation, performance traces and native AMD/Apple Silicon qualification.
