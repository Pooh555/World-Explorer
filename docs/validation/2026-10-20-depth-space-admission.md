# Typed depth-space admission — 2026-10-20

Scope: **M3 IN_PROGRESS**, Q5/N2 unqualified. Immutable diagnostic reload and
shared CLI/native viewing are implemented under the
[typed admission contract](../protocols/depth_space.md#typed-evidence-admission).
The reader checks consistency under independent pins; it does not recompute
depth, certify physical space or enable collision/walking. A0–A6/AP remain
NOT_STARTED; no platform qualification began.

Default cwd: repository root. Parent: `fb908aef96829d040574fe4f3b4581524c0892cc`.
Evidence root `R=artifacts/validation/m3-depth-space-admission-20261020` retains
exact command arrays, exits, timings, binary hashes, failing attempts and private
copies. `R/final-source-identity.json` pins final sources/scripts/binaries;
`R/committed-source-identity.json` binds the resulting local commit.

## Behavior and real integration

Admission checks the exact terminal, complete source receipt and pose/frame
identities, frozen recipe/full-source grid, uncertainty flags, every ledger code,
view/cell counts and categories. It reconstructs the expected displayed scene
and compares it with the independently pinned native bundle, using bounds derived
from the expected source/grid. The immutable owner rejects changed scene IDs,
epochs, revisions, geometry or corrected poses. The shared application first
checks the source bundle pin; viewer admission runs on its owned job thread and
uses the ordinary snapshot/update handoff. Synthetic fixtures keep their labels.

The retained source is
`artifacts/validation/m3-fresh-sequence-20261020/run/fresh/stages/geometry/stages/native/stages/attempt-0/scene.wexplore`,
manifest SHA-256
`6f84d9ce12f79a899e75d685698c0e58bd94b406eb58814da87ff9fa0c6084de`.
The unchanged diagnostic directory is
`artifacts/validation/m3-depth-space-20261020/real`, terminal
`28b470c06d83a203f9c85cb2b5c33cf0da0c1df44abd880b21dd65545a4d2573`;
its production request remains
`825c893f46a4db2246690d6010fa96fca98ed5e0ab3107b4f56364e4684edfd5`.
All **63 frames / 326,592 relations / 5,184 cells** reload. Every real cell
remains unknown; sigma is unavailable, scale arbitrary and walking disabled.
The [original independent classifier oracle](2026-10-20-depth-space.md) retains
its scope; no MVS/fusion/refit/bake or new reconstruction ran here.

A private real copy changes one outside/unknown relation to front, corrects its
view counts/category and rehashes its ledger/report. Reload rejects the unchanged
native cell display. Synthetic tests additionally permute cell labels while
preserving every view count and aggregate category, alter native overlays/raw
geometry/provenance, and exercise malformed schemas, duplicate/NUL/overdeep JSON,
wide IDs, dependencies, uncertainty, byte limits, links, cancellation and stale
context. Earlier admitted owners/source snapshots remain unchanged on failure.

## Actual checks

| Command/reference | Result |
|---|---|
| `cmake --build --preset dev -j 4`; `R/build-dev-confirmed.log` | Exit 0, GCC 16.2.1 C++26 viewer/CLI/core/UI tests |
| `python3 R/profiles_final.py`; `R/profiles-final.json` | Full dev **445/445**, explicit C++23 **420/420**; focused **35 cases / 7,254 assertions** on each; separate ASan build exit 0 |
| `python3 R/real_final.py`; `R/real-final.json` | Final normal reload exit 0 in 0.808 s; wrong source/report/request pins and rehashed false-front copy exit 2 |
| `python3 R/real.py`; `R/real.json` | Real SIGINT after 0.05 s returns 130 in 0.067 s; read-only source/diagnostic preservation |
| `python3 R/reproduce.py` | Refactored producer exit 0 in 106.066 s; all **seven** original diagnostic files byte-identical |
| Native `python3 R/native_final.py`; `R/native-final.json` | RTX 4060 Laptop GPU, **90 frames**, all **5,184 splats / 39,683 vertices / 49,833 triangles** resident, zero Vulkan errors |
| Native `python3 R/sanitizers.py`; `R/sanitizers.json` | ASan/UBSan/LSan focused contracts and full retained real reload exit 0; rehashed false-front copy exits 2; normal/sanitized summaries exact |
| `python3 R/preserve.py`, `python3 R/preserve_selected.py`, `python3 R/finish.py` | 24,457 protected files, 152 sequence inputs, eight original raw/bake inputs and prior products unchanged; pre-existing 3,982 external-header drifts retained |
| Pinned style gate on `R/cpp-paths.txt`; final Black/docs/diff receipts | Exit 0; 12 C++ files with clang-format/clang-tidy 23.1.1; no warnings weakened |

`R/admitted-final-visible.png` was inspected: the blue unknown grid, full legend,
arbitrary-scale warning and unavailable Walk control are visible. The default
appearance view is dominated by the grid overlay; unchanged mesh residency is
checked separately in the native receipt. This is a diagnostic display, not a
room-geometry quality pass. Timings are single observations with concurrent CPU
checks, not performance certification.

Initial style checks caught naming, switch-default and a Catch2 false-flag enum
diagnostic. Assertion spelling was changed without suppressions. A multiline
assertion repair briefly missed a closing parenthesis, failing native/C++23 test
compilation; the corrected code and final profiles pass. All failed logs remain
in `R`.

Canonical collision cooking on explicitly synthetic supported fixtures is next.
Independent sample_1 scale/up and qualified observed geometry remain absent;
Jolt/controller behavior and measured collision/walking remain open.
