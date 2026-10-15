# 012 — M1 development exit on available hardware

Date: 2026-10-15, Asia/Hong_Kong. Status: accepted by user request.

The user has no physical Windows or macOS machines and explicitly authorized
revising M1 and moving their physical runs later. The original M1 made access to
H_R/H_V/H_M and three native viewer platforms prerequisites for a first vertical
slice. The available Linux/NVIDIA development host and hosted native CPU CI can
prove that slice while physical platform behavior remains unqualified.

Use [technical plan v1.1 §8.1](../technical_plan.md#81-m1-development-exit-and-hardware-qualification)
for M1: retain the real consented 20–30 s textured-room chain, four contracts,
synthetic replace/delete replay, frozen ≤250k viewer fixture, local native Linux
Vulkan/fly checks, selected version/license audit and recorded local timings.
Hosted Linux/Windows/macOS CPU build/test results preserve early portability
coverage. Record exact hardware/profile/commit; named reference devices are not
needed for this development exit. Dependency redistribution gaps stay explicit.

Move physical Windows/macOS viewer opening, GPU/display/input/HiDPI behavior and
the former M1 **≤250k / 720p / ≥30 FPS on H_V/H_M** qualification to M4. H_R
performance certification stays with P1/reconstruction qualification in M3/M6;
renderer reference-device gates stay with their assigned milestones. Device access
may be borrowed or remote; ownership is unnecessary. M6 still requires the full
Q/P/N/E evidence for every claimed release profile. No numeric target, workload,
metric, consent rule or test is weakened or removed.

This avoids blocking early development on unavailable devices. It accepts later
discovery of native rendering/input defects; keep hosted CPU CI green and add
viewer compilation there as SDK provisioning permits. Cross-platform GPU/display
claims require the M4 evidence. Historical reports retain their original scope;
this decision does not mark M1 or any full gate VERIFIED. Review the remaining
local M1 evidence before advancing to M2. Revisit at M4 entry, device access or a
platform-support/release decision.
