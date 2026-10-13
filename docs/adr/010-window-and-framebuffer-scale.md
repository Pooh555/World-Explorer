# 010 — Logical UI scale and immutable font atlas

Date: 2026-10-14, Asia/Hong_Kong. Status: accepted for M1 native viewer.

A native Wayland 2× density probe exposed doubled logical layout and blurred
fonts. The startup-only scale also ignored runtime changes. SDL distinguishes
physical display scale from window pixel density; ImGui's pinned SDL backend
uses window coordinates and separately sets framebuffer scale. Therefore layout
scale is display scale / pixel density. See [SDL high-DPI guidance](https://wiki.libsdl.org/SDL3/README-highdpi).

Choose an immutable atlas of 13 default-font sizes from 1–4× in quarter steps,
with compensating logical font scaling and absolute theme scaling each time the
layout ratio changes. This supports the bounded certified implementation range
without rebuilding GPU font textures. Alternatives were stretching one 13-pixel
font (blurred) and rebuilding the atlas on each change: pinned ImGui 1.91.9b's
Vulkan font recreation calls queue-idle, conflicting with responsive runtime
updates. The atlas costs additional bounded font memory at startup. Fractional
scales can still resample between prepared sizes. Invalid/unsupported scale
fails explicitly rather than silently clamping.

[Native evidence](../validation/2026-10-14-native-input.md) checks live Wayland
2→1.5→1→2 transitions and compact Import bounds on the retained real scene, with
zero Vulkan errors. A headless compositor output is a scale-delivery probe, not
physical Retina, Windows DPI or reference latency qualification. Revisit for
scales above 4×, accessibility fonts or a backend supporting incremental atlas
updates without a runtime idle wait.
