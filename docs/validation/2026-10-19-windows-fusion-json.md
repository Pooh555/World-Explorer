# Windows fusion JSON comparison repair — 2026-10-19 (Asia/Hong_Kong)

The reported MSVC **C2666** ambiguity is removed from the fusion reader's source.
Local GCC C++26/C++23 builds and fusion checks pass; **native Windows confirmation
remains pending**. M3 continues; this repair does not begin M4 qualification or
change gates, dependency versions, language standards or warning policy.

Repository root is the default cwd. Parent `147f053`; repaired reader SHA-256:
`f7070396b6c99e070aa3ed68440bfe332e905571199807498727aace99c6cd05`.
Ignored evidence root `R=artifacts/validation/m3-mesh-view-20261019` retains the
supplied log as `windows-failure.txt`, SHA-256
`7f19b1e929b06f6e4e6fdc1c53ef5c31e4684572168e004502ddf3f312625e56`.

The supplied Windows build uses MSVC **14.51.36231**, `/std:c++latest`, `/W4 /WX`
and nlohmann JSON **3.12.0**. It fails comparing JSON values directly with
`std::string_view` constants in `check_policy`: JSON equality and the rewritten
standard string-view equality candidate require similar conversions. The new
`text_is` helper first requires a JSON string, then compares its explicitly typed
`std::string` reference with the expected view. No JSON equality candidate enters
that comparison. All scope/unit/free-space constants use the helper; malformed
or unsupported values still reject.

Actual local checks (exit **0**):

| Command | Result / log |
|---|---|
| `cmake --build --preset dev -j 4` | GCC **16.2.1**, C++26, native viewer/CLI/core build; `windows-fix-dev-build.log`, subsequent `build-final.log` |
| `cmake --build --preset compat-cxx23 -j 4` | Explicit GCC C++23 CPU profile; `compat-build-initial.log`, `compat-build-final.log` |
| `build/dev/we_core_tests '[fusion]'` | **9 cases / 155 assertions**, `windows-fix-fusion-final.log` |
| `build/compat-cxx23/we_core_tests '[fusion]'` | **9 / 155**, `windows-fix-cxx23-fusion.log` |

The tests retain the original malformed/rehashed semantic and dependency
rejections. Linux results identify a narrower profile than the failed Windows
workflow; no MSVC rerun or Windows success is inferred. Native scene/renderer
work proceeds using the preserved real assets; no solve/refit is needed.
