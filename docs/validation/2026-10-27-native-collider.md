# Native static collider admission — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. This slice admits a distinct static
structural collider against exact base geometry, independent cook policy and
complete remapped face provenance. It emits actual portable triangle input and
publishes/reloads root and cook records atomically, then binds definition and
instance identity. The [protocol](../protocols/native_colliders.md) separates
gameplay purpose from observed surface evidence. No backend shape, support ledger,
measured walking, convex props, collider export or AP gate is qualified.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `182753744714568ec8978b762f029ddfc4f51afd`. CMake + seven C++ files:
`822e0d87d7852aa96ede68f06bd93bf9f6f6d75cfc7478a352e9e9e1a9904d1e`
(`R/source-identity.json` defines the sorted path/hash algorithm). Ignored commands, logs and
products are under `artifacts/validation/a0-native-collider-20261027/` (`R`).
`python3 R/run_check.py TAG COMMAND...` records actual argv/cwd/exit/elapsed time.

The synthetic asymmetric fixture remaps four vertices/two oriented faces, with
observed/inferred evidence and two shared asset copies. A separate eight-face
ring retains its hole and area 12 in arbitrary squared units; a bridge rejects.
Tests cover rehashed topology/authority/policy changes, strict root bytes,
independent limits, ancestry/context/cancellation, owned recovery and foreign
cook substitution. The policy-only case retains identical geometry but changes
cook/definition/instance identity; an immutable ID/revision conflict preserves the
previous committed records.

| Actual checks (exit 0) | Result / command reference |
|---|---|
| GNU16.2.1 C++26 native / C++23 CPU builds | `build-dev-final`, `build-compat-final`; binaries frozen during CTest |
| Full CPU/worker suites | `ctest --preset dev -j2`: **891/891**, 187.38 s; `ctest --preset compat-cxx23 -j2`: **866/866**, 185.04 s |
| Collider focus | `build/dev/we_core_tests '[native-collider]'`: **11 cases /10,814 assertions** (`focused-final`) |
| Native ASan/UBSan/default LSan | `build/asan/we_core_tests '[native-collider],[native-assets],[native-export],[native-lod],[native-lod-export],[native-structure]'`: **58 cases /55,775 assertions**, 173.82 s, no reported sanitizer/leak errors (`asan-native`); options unset |
| Exact LLVM23.1.1 format/tidy, docs/boundaries/whitespace | Seven C++ files pass (`style-final`) |
| Local libc++23.1.1 / Clang23.1.4 C++23 syntax | Collider reader, native asset reader and collider tests pass (`libcxx-collider`, `libcxx-assets`, `libcxx-tests`); no native macOS pass inferred |

`product-probe` runs the actual adoption test with
`WE_NATIVE_COLLIDER_PROBE_ROOT=$PWD/R/product`, retaining the original receipt,
root/cook store and definition/instance store. `product-oracle-run` independently
verifies receipt/record hashes, the full oriented vertex/face permutation,
observed/inferred provenance, canonical definition references and both arbitrary
world placements. Python `struct` reproduces all **487 canonical bytes** exactly
from original root/base/receipt data. No backend or navigation owner is created.

Root SHA: `a2b1ca0407f769592d163b6664b06cb85d966b2f61d9f11ed2c62f9d385a06bc`;
cook: `48f95acb5c5bd8e17fcd89b1e6f6c0821f41c18a1a9fdfa641e41c39d8dc2842`;
definition: `e5e42e67695afe37b386b21e0356ede589189f14245ad2b8b04b6f0b4f031de1`.
`product-oracle.json` records the complete identities and counts. Regenerated
legacy textured **24 files**, structured **21 files**, both **30-file** LOD
packages and both complete native PBR bundles match the preceding frozen outputs
byte-for-byte (`legacy-export-probe`, `structured-export-probe`,
`lod-export-probe-final`). Rendering code/source formats are unchanged; no GPU,
engine or physics qualification is inferred from this CPU input-admission slice.

Initial style findings: adjacent same-type source references and two unchecked
test optional accesses. Parameter separation and the existing selected-value
pattern repair them without suppressions. Native ASan build approval review
timed out before execution; the single allowed retry succeeded. The original
timeout is an approval-service result, not a compiler or sanitizer failure.
`lod-export-probe` exits 42 because its ignored output parent was absent; creating
that parent and rerunning as `lod-export-probe-final` passes. No production check,
warning, resource limit or failed case was weakened or removed.

Initial and final protected-data scans match all 378 hashes. Real capture/resource/recipe
prerequisites, general collider production, backend/engine execution and all full
AP/Q/N gates remain open. Native/reference platform qualification has not begun.
