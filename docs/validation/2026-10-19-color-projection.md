# Captured-color projection audit — 19 October 2026, Asia/Hong_Kong

M3 remains **IN_PROGRESS**. The shared `audit-mesh-colors` command now audits full
admitted raw/cleaned meshes against their exact retained depth/RGB receipt and
current pinned poses. It publishes bounded samples, a diagnostic contact sheet
and complete coverage/owner/rejection/seam ledgers. [The contract](../protocols/mesh_color.md)
owns the fixed numerical policy and binary layout. Usable textures, native
textured rendering and mesh export remain unimplemented; no Q/P/N/E/AP gate passes.

Default cwd: `/home/Pooh555/programming/World-Explorer`. Parent `21fe64a`.
Evidence root `R=artifacts/validation/m3-color-audit-20261019`.
`R/source/` freezes the eight changed C++ files and CMake definition;
`source-identity.json` SHA-256 is
`83f9aba5099b035e8488c17d88672636a0d40d61f5887a75eefc68e252a9d077`.
`environment.json` and `binary-identity.json` bind actual tools/executables.
Profiles: GCC 16.2.1/C++26; Clang 23.1.3/explicit C++23 with libstdc++ and
isolated libc++ 23.1.1; native GCC ASan/UBSan/LSan. Format/tidy: pinned 23.1.1.
Independent oracle: existing isolated Python 3.12.15, NumPy 2.2.6/Pillow 11.3.0.
Dependencies/environments and renderer behavior are unchanged; no reconstruction
reran and no new platform qualification is claimed.

## Commands and checks

Exact argv/cwd/UTC/exit/durations are retained in `commands.json` and
`{dev,clang,libcxx,asan}-commands.json`; logs retain initial failures.

```sh
cmake --build --preset dev -j 4
python3 "$R/checks.py" dev
python3 "$R/checks.py" clang
python3 "$R/checks.py" libcxx
python3 "$R/checks.py" asan
python3 "$R/run.py"
artifacts/environments/fusion-cpu/bin/python "$R/oracle.py"
python3 "$R/rejections.py"
python3 "$R/preservation.py"
python3 "$R/cleanup_preservation.py"
```

Evidence scripts use fresh owned output names. Sanitizer build/tests used native
access and `ASAN_OPTIONS=detect_leaks=1`. The full GCC development suite passes
**356/356** (98.23 s). All four profiles pass **22 focused cases / 411 assertions**;
8 new projection/decoder/request cases include 111 assertions. Analytic checks
cover optical axes/integer centers, nonsquare intrinsics, similarity scale,
sRGB transfer/linear interpolation, both sides of the frozen depth envelope,
unknown/zero-weight/unsupported corner rejection, borders, grazing, malformed
planes, cancellation and actual RGB dimensions rather than byte-count equivalence.

The first build rejected a missing default member initializer and ambiguous
JSON-number/null conditional; both were corrected. Initial tidy rejected an
ambiguous binary-width argument and a Catch macro's repeated move expression.
Typed integer encoders and a retained result resolve them. `style-final.log`
passes all eight C++ files without warning suppression or standard reduction.
Final docs, boundaries and diff checks pass. No unchanged renderer/stress rerun
was needed because no scene or rendering behavior changed.

## Full retained-source evidence

All four real raw/proxy scenes retain their previous bundle content hashes.
Requests pin the original [visibility caches](2026-10-18-depth-visibility.md) and
[raw/proxy source admissions](2026-10-19-mesh-bundles.md). `results.json` and each
terminal manifest bind request/source/pose/receipt/cleanup and output hashes.
No mesh/component was removed or sampled out of the input denominator.

| Scene | Known / total interior samples | Known shared-edge pairs | Maximum seam RGB L∞ |
|---|---:|---:|---:|
| Temporal raw | 276,518 / 285,354 | 60,961 | 0.562263 |
| Temporal proxy | 269,228 / 277,938 | 59,154 | 0.562263 |
| Parallax raw | 465,486 / 493,488 | 96,477 | 0.884563 |
| Parallax proxy | 457,427 / 485,472 | 94,439 | 0.884563 |

The independent NumPy/Pillow oracle reconstructs every point/face normal from
saved binary geometry, decodes original depth planes, projects calibrated RGB
and compares **2,313,378 samples / 34,036,362 view attempts**. Every candidate
mask, rejection ledger and selected owner matches; linear RGB matches exactly;
maximum score difference is `6.073e-14`. It independently checks shared-edge
comparisons and every diagnostic contact-sheet pixel, including unknown/padding.
PNG encodings preserve the original PPMs; both proxy sheets were inspected.
`oracle.py`, `oracle.log` and `oracle.json` retain complete results, not a subset.
`real_profiles.py` / `real-profile-commands.json` additionally verify the full
80,912-triangle parallax proxy through both Clang profiles and native sanitizers;
all diagnostic files are byte-identical to GCC output, with no sanitizer findings.

The 94.22–96.90% known **sample** fractions describe only the existing partial
mesh. They do not measure room/surface coverage or validate unsampled texels.
Large seam jumps remain; candidate selection alone is not a usable baked texture,
exposure correction or appearance-quality pass. Original unknown-space limits
remain unchanged.

`rejections.json` records bad cache/source pins and linked cache inputs rejecting
with exit 2 and no output directory. Existing-output rejection preserves every
published byte. SIGINT while the owned FFmpeg child was observed alive returns
130 in 0.0315 s, with that child reaped and no output directory. This is one local
CLI cancellation observation, not production job crash/resume qualification.
`preservation.json` checks 24,457 protected files and original fusion assets;
cleanup/bundle preservation records additionally verify prior no-op/final products,
24 frozen source files and all four full bundles. No upload/refit/reconstruction,
quality threshold change or environment modification occurred.

Next is seam-aware captured-color texture production, explicit texel validity,
bounded material/UV persistence, unlit interchange and the actual native textured
view. Metric scale, collision/walking, production jobs/resume and A0–A6/AP status
remain unchanged.
