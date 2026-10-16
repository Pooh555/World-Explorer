# Optional binary16 SH storage

Date: 2026-10-16. Status: accepted for experimental local diagnostics;
Q6 qualification and default promotion remain pending.

GPU splats currently occupy 256 bytes, including 192 bytes of FP32 spherical
harmonics. Reducing storage can lower residency and transfer costs, but arbitrary
monocular scale makes a millimetre geometry error claim unavailable for the saved
scenes. Quantizing geometry would add a separate unqualified error source.

Choose an explicit, immutable renderer profile with FP32 geometry/opacity/
rotations and binary16 SH storage, decoded and evaluated in FP32. Its 160-byte
record reduces payload by 37.5%. Canonical files remain FP32 and the default
renderer remains FP32. Half pairs use uint32 storage, avoiding a new device
feature requirement. Out-of-range coefficients fail explicitly rather than
saturating radiance; the prior visible scene survives.

The [local comparison](../validation/2026-10-16-m2-sh-packing.md) freezes 100
inspection views before rendering both profiles on each complete saved scene.
It measures pairwise distortion with pinned offline metrics. These images lack
captured ground truth, so their PSNR/LPIPS distances do not establish Q6's
≤0.2 dB quality loss / ≤0.01 LPIPS increase. Revisit default promotion only with
that quality comparison, qualified geometry scale, lifetime/budget checks and
the prescribed corpus/platform evidence. No threshold is changed.
