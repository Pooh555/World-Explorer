# Observed-depth space diagnostics — 2026-10-20

Scope: **M3 IN_PROGRESS**, Q5/N2 unqualified. The separate
[whole-cell diagnostic](../protocols/depth_space.md) now consumes all retained
pose-bound depth/RGB inputs and preserves unknown/conflicting observations.
It does not create measured free space, collision, floors or walking. A0–A6/AP
remain NOT_STARTED; platform qualification has not begun.

Default cwd is the repository root. Parent: `3cbce7d81c889451b2cb97900c801ae902406171`.
Evidence root `R=artifacts/validation/m3-depth-space-20261020` contains exact
arguments, exits, timings, binary hashes, products and independent scripts.
`R/final-source-identity.json` pins the implementation, scripts and binaries;
`R/committed-source-identity.json` binds the resulting local commit.

## Implementation and real result

The shared application/CLI command streams checked views through a bounded
native range pyramid. It projects every cell corner, requires support throughout
an outward-enclosing pixel footprint and retains all view/cell relations.
The versioned terminal pins the complete source receipt, recipe/grid, binary
ledger and native diagnostic. The native bundle retains all raw mesh chunks and
adds every cell with an explicit diagnostic legend. Existing synthetic support
admission, alignment and geometry/appearance formats are unchanged.

The exact retained request is
`artifacts/validation/m3-color-sequence-20261020/run/sequence-request.json`, SHA-256
`825c893f46a4db2246690d6010fa96fca98ed5e0ab3107b4f56364e4684edfd5`;
its complete 63-view cache remains
`a0b311978cfa7e934e3d029db61ca8e4e9dc37198b12f000d0729faae0d52599`.
No MVS/fusion/refit/bake ran. Full source bounds yield **18 × 9 × 32 = 5,184**
cells of size **0.4641830176115036 arbitrary pose units**; the diagnostic guard
is **0.02534787786127088** source units. All 63 views lack sigma; only 15.9265%
of input depth pixels are valid.

All **326,592** attempts remain explicit: **181,362 outside-view**, **145,230
unknown-depth**, zero front/band/behind. Consequently all 5,184 cells are unknown.
The independent CPU oracle reproduces every relation, view total and aggregate
category, verifies the complete original bounds and directly scans **4,168**
selected rectangles. Even tight rectangles have zero fully supported attempts;
the conservative mip overcoverage did not cause this result. It identifies
**2,217** center-only front tests without complete-cell support. Those tests
cannot justify free-space labels. This fixed diagnostic is not a room-completeness
benchmark, physical ray-certification or permission to tune the evaluation grid.

## Actual checks

| Command/reference | Result |
|---|---|
| `cmake --build --preset dev -j 4`; `R/build-dev-final.log` | Exit 0, GCC 16.2.1 C++26 native viewer/CLI/tests |
| `python3 R/local.py`; `R/local.json` | Real production exit 0 in 48.387 s; validation exit 0; SIGINT after 2 s returns 130 in 2.008 s; existing destination and stale pin exit 2 |
| `python3 R/repeat.py` | Final binary exit 0 in 49.074 s; all seven output files byte-identical |
| `PYTHONDONTWRITEBYTECODE=1 artifacts/environments/fusion-cpu/bin/python R/oracle.py` | Exit 0; complete independent NumPy/pixel oracle above, 4.645 s |
| `python3 R/profiles.py`; `R/profiles.json` | Full dev **436/436**, explicit C++23 **412/412**; focused **49 cases / 10,096 assertions** on each profile; separate ASan build exit 0 |
| Native `python3 R/native.py`; `R/native.json` | RTX 4060 Laptop GPU: diagnostic **90 frames, 5,184 splats + 39,683 vertices / 49,833 triangles**, full owned bake **90 frames, 39,373 / 49,213**; zero Vulkan errors. GPU mesh regression exit 0 |
| Native `python3 R/sanitizers.py`; `R/sanitizers.json` | ASan/UBSan/LSan focused contracts and full 63-view production; all seven outputs match normal bytes |
| `python3 R/preserve.py`, `python3 R/preserve_selected.py`, `python3 R/finish.py` | 24,457 protected files, 152 sequence inputs, eight original raw/bake inputs and prior products unchanged; existing 3,982 external-header drifts remain separate |
| Pinned style gate on `R/cpp-paths.txt`, Black, docs and diff checks | Exit 0; eight C++ files with clang-format/clang-tidy 23.1.1; no warning suppression or weakened gates |

Native captures `R/diagnostic-visible.png` and `R/real-visible.png` were inspected:
the complete blue unknown grid is visible alongside retained raw geometry; the
owned captured-color room remains fragmented. Timings above are single command
observations with concurrent checks, not performance qualification.

Initial build failed on a test comparing two `expected` objects whose error type
has no equality. The test now checks successful values. Initial style analysis
found constant names and a Catch2 `REQUIRE_FALSE` flag-enum diagnostic; names and
the assertion spelling were corrected, preserving checks and warning policy.
Failing build/style logs remain in `R`.

Output terminal SHA-256:
`28b470c06d83a203f9c85cb2b5c33cf0da0c1df44abd880b21dd65545a4d2573`;
ledger: `f6f68a72b32c55255b4bd329492b31a6361549628d9ff01ba85171d2d8fba5d3`;
native manifest: `aa6c94d26b388a0a1cf73606c325fa206835aa852e048ce1d096d257cb5b1c12`.

Typed diagnostic admission/reload and stale-context invalidation are next.
Independent scale/up measurement is unavailable for sample_1. Complete supported
geometry, measured collision, Jolt/controller behavior and Q5/N2 remain open.
