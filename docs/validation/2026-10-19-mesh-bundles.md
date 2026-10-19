# Native mesh bundles — 19 October 2026, Asia/Hong_Kong

M3 remains **IN_PROGRESS**. Schema 2 persists native raw/cleaned observed meshes,
synthetic diagnostics and hybrid mesh/splat chunks. The shared `compile-fusion`
command validates the pinned parent/poses and optional cleanup proof before
publishing a new bundle. Ordinary Open scene/`--scene` reopens it through the
existing immutable renderer update path. Splat-only schema-1 bytes and hashes
remain unchanged. [The format contract](../protocols/scene.md#native-mesh-bundle-persistence)
owns encoding, admission and publication semantics.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `32918dd`. Evidence root `R=artifacts/validation/m3-mesh-bundle-20261019`.
`R/source/` freezes all eight changed C++ files; `R/source-identity.json` has
SHA-256 `cd806f563d41bb9d2242dfc7c7f631de1773d50acb576eddd42d071a16506041`.
Final source differs only by the recorded closing-brace indentation correction
in `format-only.patch/json`; token sequences are identical.
`environment.json` and `binary-identity.json` bind actual tools/executables.
The host is Linux x86-64, GCC 16.2.1/C++26 and Clang 23.1.3/explicit C++23;
`ci-libcxx` uses isolated libc++ 23.1.1. Formatting/tidy use pinned 23.1.1.
Native graphics: RTX 4060 Laptop, 8,188 MiB, driver 615.78.08.

## Checks and retained failures

Exact argv, cwd, UTC, exit status, durations and logs are in
`{dev,clang,libcxx,asan,style}-commands.json`, `*-recheck-commands.json`,
`roundtrip-commands.json` and `native-commands.json` under R. Reproducible entry
points, each run with system `python3`:

```sh
python3 "$R/checks.py" dev       # final development build preceded this command
python3 "$R/checks.py" clang
python3 "$R/checks.py" libcxx
python3 "$R/checks.py" asan
python3 "$R/checks.py" style
python3 "$R/recheck.py" dev
python3 "$R/recheck.py" clang
python3 "$R/recheck.py" libcxx
python3 "$R/recheck.py" asan
python3 "$R/recheck.py" style
python3 "$R/roundtrips.py"
python3 "$R/native.py"
python3 "$R/preservation.py"
```

These evidence scripts require fresh output names; existing real artifacts are
never overwritten. Native GPU and ASan/UBSan/LSan runs used native access, with
`ASAN_OPTIONS=detect_leaks=1` for sanitizer tests.

The full CPU runs passed **347/348 GCC**, **347/348 Clang/libstdc++** and
**324/325 Clang/libc++** cases. Each failed only the old contract test that called
schema 2 unsupported. Schema 2 now exists; its incomplete manifest correctly
returned invalid data. The unsupported-version mutation now selects schema 3;
a separate case rejects a fully declared schema-2 manifest without any mesh.
All corrected bundle/mesh/cleanup cases and each CTest `--rerun-failed` pass on
all three profiles: **39 cases / 15,126 assertions** each, also passing native
ASan/UBSan/LSan without diagnostics. Unchanged passed tests were not repeated.
Logs preserve the
original failures and corrected results, rather than presenting a second full
suite run that did not occur.

Initial static analysis rejected a test binding's constant name and unchecked
optional accesses. Both were fixed. `style-final.log` passes seven files;
`style-corrected-tests.log` passes both later changed test files, including the
eighth file; `style-brace.log` checks the final indentation. No warning suppression,
standard reduction or dependency change was
introduced. Final documentation/link/boundary/whitespace checks pass.

Tests cover exact raw/derived/synthetic/hybrid and invalidated snapshot round
trips, mixed legacy/new chunks, schema downgrade/promotion rejection, exact
encoded and aggregate count budgets, hostile JSON, rehashed counts/overflow,
truncation/trailing bytes, nonfinite positions, normals, indices, metadata sizes,
source/certificate hashes and epoch/pose dependencies. Rejected admission leaves
caller files and retained snapshots unchanged; obstructed index publication
preserves the old manifest and permits corrected publication. Malformed/cancelled
compilation creates no destination; existing destinations reject unchanged.

## Full real integration

`roundtrips.py` compiles the retained [raw mesh requests](2026-10-19-observed-mesh-view.md)
and [certified proxies](2026-10-19-mesh-cleanup.md), then independently decodes the
saved little-endian files. `roundtrips.json` records exact request, source,
geometry, scene and output-file hashes. Original coverage/receipt text, every
source-frame ID and all dependency pins match; cleanup JSON is byte-identical.
Raw vertex/index bytes equal their original `.wefusion` payloads, and proxy
geometry matches each independently certified geometry digest. Every reopened
scene hash equals direct source admission. A pre-change executable and the new
writer produce byte-identical complete schema-1 diagnostic bundles.
`real_profiles.py` / `real-profile-commands.json` additionally record eight
successful full-bundle CLI validations across the two Clang profiles, all with
the same scene hashes.

| Retained scene | Vertices | Triangles | Native frames | Vulkan errors |
|---|---:|---:|---:|---:|
| Temporal raw | 32,074 | 47,559 | 90 | 0 |
| Temporal proxy | 31,456 | 46,323 | 90 | 0 |
| Parallax raw | 61,598 | 82,248 | 90 | 0 |
| Parallax proxy | 60,930 | 80,912 | 90 | 0 |

All four full bundles open through `we_viewer --scene`; resident counts match.
The PPM captures and lossless PNG encodings were inspected: fragmented observed
surfaces, unknown gaps, normal diagnostics, arbitrary units and disabled walking;
proxy labels survive persistence. These short runs make no performance claim.
`preservation.json` checks 24,457 protected files plus original fusion products;
`cleanup-preservation.json` checks the prior no-op/final cleanup products and
frozen sources. No solver/refit, environment modification, upload or trimming ran.

This certifies local snapshot persistence, not fresh proof replay from external
parents. Voxel planes/current pose assets remain separate authoring dependencies.
Baked capture-color fallback/export, production jobs/resume, compression, metric
alignment, collision/walking and Q5/N2 remain open. Windows/macOS/native release
qualification and the A0–A6/AP gates are unchanged.
