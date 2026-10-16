# Resident chunk copies and P6 replay — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/P4/P6 IN_PROGRESS**. Local Linux Wayland/NVIDIA RTX 4060
Laptop, driver 615.78.08; GCC 16.2.1 C++23 Release for timing. Parent `3c120ce`;
implementation is the commit containing this report. Cwd: repository root.
Ignored evidence root **R**: `artifacts/validation/m2-completion-20261016`.
The retained sample_1 scene has 295,952 splats; its manifest and content digests
remain those in the [completed staged-run audit](2026-10-16-m2-staged-lifetime.md).
The replay still adds/replaces/evicts a synthetic 4,096-splat marker at scheduled
10 Hz, with the same source camera, 1280×720 extent and no LOD reduction.

The rebuilt original path displays all 100 revisions but misses the unchanged
2 ms per-frame upload target at **2.090304 ms**. The renderer now reuses already
resident bytes for identical immutable chunk objects at the same floating origin.
Changed chunks are CPU-packed; a changed origin forces fresh packing. Source and
destination offsets follow their respective snapshots, including after insertion/
deletion. Nearby-camera updates retain the existing origin. Both transfer kinds
share the existing 15 MiB frame cap, final atomic publication and timeline-based
retirement. Staging remains full-sized and charged. Active staging is now also
included in `staging_bytes` diagnostics.

Resident buffers carry transfer-source usage. A
[transfer memory dependency](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryBarrier2.html)
exposes their earlier transfer writes to copy reads; the final transfer-to-compute
dependency and retirement protect the replacement and old source. No new GPU
idle wait is introduced. Completed frame timings record host/device copy bytes.

| Sequential native Release replay | Revisions | Enqueue p95 | GPU upload p95 / max | Visible updates/s | Local thresholds |
|---|---:|---:|---:|---:|---|
| Original rebuilt path | 100 | 0.012609 ms | 1.656288 / **2.090304 ms** | 10.009878 | Fail |
| Resident prototype | 100 | 0.004818 ms | 0.189600 / 0.203104 ms | 10.012617 | Pass |
| Final source, 10 s | 100 | **0.008271 ms** | **0.188800 / 0.194304 ms** | **10.012257** | Pass |
| Final source, 60 s | 600 | **0.008282 ms** | **0.188736 / 0.228992 ms** | **10.002008** | Pass |

Final runs have 500/3,000 upload-frame samples, zero retries, zero interactive
idle waits and zero Vulkan errors. Tracked owned allocations peak at
**400,240,832 bytes**, below 536,870,912. The 600-revision run copies
**419,430,400 host bytes / 45,458,227,200 resident bytes**; exact marker/source
byte counts and revision sequences were independently checked. Preparation
p50/p95 is 8.961511/9.365932 ms for the short run and 7.393349/10.462417 ms for
the long run. Final GPU timing runs had no competing project build, test or GPU
job; normal desktop activity was not isolated. These are development measurements,
not H_V/corpus P6, P2/P3 or physical scanout certification.

Actual commands (R is expanded to the path above):

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure
cmake --build build/m2-no-fault -j 4 --target we_gpu_update_stream we_gpu_resident_reuse we_gpu_lifetime_stress we_gpu_viewer_regression
build/m2-no-fault/we_gpu_update_stream artifacts/sample_1/scene.wexplore artifacts/validation/m2-p6-staged15-rebuilt-final-20261016 10
build/m2-no-fault/we_gpu_update_stream artifacts/sample_1/scene.wexplore artifacts/validation/m2-completion-20261016/resident-final-10s 10
build/m2-no-fault/we_gpu_update_stream artifacts/sample_1/scene.wexplore artifacts/validation/m2-completion-20261016/resident-final-60s 60
```

All commands above exit **0**, including the original timing miss (its summary
correctly says `local_p6_thresholds_met=false`). CPU checks pass **270/270 dev /
252/252 C++23 CPU**. `we_gpu_resident_reuse R/reuse-fixture-final` and the same
target in `build/m2-vma-clang` each exit 0: **24 synthetic phases**, exact CPU/
resident readbacks, old revision retained during partial copies, explicit transfer
byte counts, insert/delete offsets, transforms, empty scenes and origin changes.
The independent compiler is Clang 23.1.1 C++23; this is a native diagnostic pass.
`we_gpu_viewer_regression` on the full saved scene passes windowed/4K/fullscreen/
restore with zero Vulkan errors and **510,820,928** peak tracked bytes. The strict
X11 smoke also exits 0, including queued-update/minimize/restore and exact pixels.
Logs and exact runner arguments remain under R. Format/tidy 23.1.1, boundaries,
documentation and diff checks pass without weakening warnings.

The 60-second native full/full/empty pilot exits **0**: 60.242 s, three visible
updates, two resizes, two identical readbacks, 75,763,712 observed resident-copy
bytes and zero Vulkan errors. It precedes only equivalent unsigned Vulkan-flag
typing corrections. A fresh **7,200-second** run is started by
`python3 R/supervise_lifetime.py`; `resident-full-launch.json` binds the final
source/binary/manifest hashes, and `resident-full-exit.json` will record actual
termination. No long-run result is claimed before that receipt and artifacts
are audited. The old full/empty workload remains available separately; pending
updates awaited at duration expiry are now counted accurately.

Final replay binary SHA-256:
`7eedd965043b06eb2746b4ccc59a6b2ab9e2846295fd02b7c6f4d61440e4585b`.
Short/long summary SHA-256:
`5d9886c16b8ff9ba2b18c6ab20cfc9f20b921feb7584ea77d4a7f090ae328cbc` /
`3b3921876b2e0db80fcc6bf7db7e226a507db0460cd54e6d25b41a5fb9aeacd0`.
All **2,088 protected files** match the prior M1 inventory (`R/preservation.json`).
No reconstruction, environment provisioning, saved-scene publication or fixture
change occurred. Rebuilt executables make the historical dependency inventory's
current-byte check exit **2** (`R/dependency-current.log`); fresh runtime receipts
remain due. All-new/rebased snapshots still require host transfers. Full lifetime,
Q6 packing, reference hardware, other scenes/platforms and full gates stay open.
