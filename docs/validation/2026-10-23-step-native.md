# Native selected-step acceptance — 2026-10-23 (Asia/Hong_Kong)

**VERIFIED: local Linux/NVIDIA synthetic selected-step input.** This completes
the native acceptance left open by [controller integration](2026-10-23-step-controller.md).
The user unlocked the desktop; the unchanged prepared harness passes. M3 remains
**IN_PROGRESS** pending its dated plan §8.3 software exit review. Q5/N2, general
mixed-surface walking, real scale, A0 and platform qualification do not advance.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Implementation commit:
`3fc304d` (parent of this evidence-only update). Ignored evidence root
`R=artifacts/validation/m3-step-controller-20261023`. All nine source hashes and
eight input pins match that report's receipts. Executed viewer SHA-256:
`42680cd4dca0c217db37e4b610324d198cd923b3a749ff94f4607523e961c7d3`.
The 16-file `R/native-receipt.sha256` pins viewer, harness, commands/results,
three input traces/logs/readbacks and the retained-real view; its SHA-256 is
`21380653fb81061ad8da4b1ed5fae3c53218504ea2c42a07f2353f1ca6c3064c`.

```sh
python3 R/native_steps.py
```

Executed with native GPU/display/compositor access, exit **0**. Harness SHA-256
remains `39a86c8866d56fbed2810e2f1802d63a934b26c34289141b0e6a960dcaca0be0`.
`native-checks-unlocked.log`, `native-verified-commands.json` and
`native-verified-result.json` record **12/12** assertions. Targeted local input
checks explicit synthetic state; slow ascent; airborne Escape/dialog pauses
that retain feet and ticks; resumed ascent; terminal Stop with retained feet;
descent to the lower landing; occupied-corner blocking while ticks advance;
and clean closure of all three owned windows. Focus/cursor are restored.

| Native route | Observed accepted/blocking feet | Observed tick | Final viewer frames |
|---|---|---:|---:|
| Slow ascent after pause/resume | `(4.265000343, 0.299999982, 3.5)` | 496 | 835 |
| Descent | `(3.714999914, 0, 3.5)` | 62 | 122 |
| Occupied corner | `(3.725001335, 0, 3.849999905)` | 53 | 119 |

All viewers exit 0 on the **NVIDIA GeForce RTX 4060 Laptop GPU**, with six
source mesh triangles, twelve vertices, zero splats and **zero Vulkan validation
errors**. These are functional input checks, not latency/FPS qualification.
FFmpeg conversion and inspection of `verified-up-810`, `verified-down-110` and
`verified-corner-105` readbacks confirm readable controls, synthetic alignment,
active/stopped status and `Walk unavailable`. Movement/blocking assertions come
from the input traces; the corner screenshot follows key release, when a held
zero-input tick legitimately reports clear diagnostic space.

Four additional shared CLI/native startup checks exit **2**, as expected:
unsupported short spawn and a V4 support document with an added inclined-plane
field. `R/native-admission.json` retains exact argument arrays, request pins,
errors and results; the altered schema lives only in `R/negative-native/`.
This verifies mixed-profile schema rejection, not mixed-surface traversal.
The prior CPU source-geometry mutation test separately rejects actual inclined
step geometry. No original fixture/source/support byte is modified.

Current retained-real handoff regression:

```sh
timeout 60s build/dev/we_viewer --scene artifacts/validation/m3-mesh-job-20261020/full-job/stages/bake-1/output --frames 30 --settings R/real-settings.json --screenshot R/real-retained.ppm
```

Exit **0**; asynchronous load completes with **225 actual frames**, all
**39,373 vertices / 49,213 triangles**, zero splats and zero Vulkan errors
(`R/real-retained.log`). Manifest SHA-256 stays
`0b873804a3d5aa2a02c95245a43c550ed4d2ba2240d3af1e4325e23c198c6ee2`.
Inspected `real-retained.png` remains visibly fragmented with captured unlit
color, masked unknowns, arbitrary scale and disabled walking. No solver,
fusion, cleanup, bake or export is rerun; all 5,184 real space cells stay unknown.

The two earlier locked-desktop attempts and preflight remain historical failed
input evidence in `attempt1-*`, `attempt2-*` and `native-preflight.log`; they are
not reclassified. No C++/Python production code changed after the integration
report's 486 dev / 461 C++23 tests, 51 sanitizer cases and scoped style gate.
Documentation links and whitespace are checked for this update. Available LLVM
23.1.3 versus required exact 23.1.1 remains an explicit review gap.


Starting the M3 exit review, the following command exits **2**:

```sh
python3 tools/check_dependency_manifest.py --manifest third_party/dependency-audit-repair-20261020.json --local-evidence
```

`artifacts/validation/m3-exit-review-20261023/local-evidence.log` reports
`Changed evidence: cmake/dependencies.cmake`. Post-Jolt selected dependency and
runtime receipts need an inspected graph refresh with source/notice ownership,
not a blind hash replacement. Historical rights gaps remain open. This is the
next local repair before finishing the five-item software exit review.
