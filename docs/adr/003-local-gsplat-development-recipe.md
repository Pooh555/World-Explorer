# ADR 003: bounded local gsplat development recipe

Status: accepted for M1 development; benchmark qualification remains incomplete.

The pinned upstream example imports an old `SceneManager` parser fork, viewer
packages and fused auxiliary CUDA extensions even when those features are disabled.
This host has CUDA 13.4/GCC 16, whereas the official prebuilt gsplat 1.5.3 wheel
supports the installed PyTorch 2.4.1/CUDA 12.4 tuple. Native forward/backward
execution of that wheel succeeds. Official pycolmap 3.12.6 reads the existing
63-frame undistorted reconstruction correctly.

Keep the upstream example adapter and its benchmark requirements. Add an explicitly
named `local_gsplat_v1` development worker using the same gsplat rasterization,
degree-3 SH, Adam parameters and `DefaultStrategy`, with project-owned bounded
image loading, separable Gaussian SSIM and complete optimizer/RNG checkpoints.
No C++ optimizer port or substitute sparse cloud is involved.

Resize is an explicit argument, recorded with per-image calibration transforms.
No target, workload or default benchmark resolution changes. Temporal holdout
images never supply training RGB or initial SH colors. Poses may use the complete
sequence; evaluation remains developmental, not independent-video Q3 evidence.
LPIPS is unavailable until the specified metric/backbone environment and rights
are locked; do not report a fake score or certify Q2/Q3.

Revisit after upstream dependencies compile in a qualified environment and
reference renders/metrics establish parity. A successful development scene does
not complete M1 native reference/platform qualification.
