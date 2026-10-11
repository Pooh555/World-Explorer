# ADR 002: select compiler and standard library together in native CPU CI

2026-10-11. Accepted for development CPU CI; hosted rerun and release qualification pending.

The supplied CI screenshots show three configure failures: an unversioned Linux
Clang cannot expose `std::expected`, Apple's default library lacks `std::jthread`
and `std::stop_token`, and Windows cannot discover OpenSSL. C++23 mode does not
guarantee these library APIs. Keep the required feature probe and warning policy.

Select GCC 13 on Ubuntu and provision Clang 19 with GCC 13's libstdc++ for the
Clang job. Clang through 18 advertises `__cpp_concepts=201907L`, below the gate
used by libstdc++'s expected header; Clang 19 advertises `202002L`. Select the
GCC installation explicitly rather than taking the runner's newest library.
Do not redefine feature macros to bypass the header gate.

Use Homebrew LLVM 20 on macOS, explicitly selecting its libc++ headers, library
directory and runtime search path. libc++ 20 makes jthread/stop-token support
available without experimental-library flags. This is a native macOS development
CPU profile; Apple Clang and the shipping SDK/runtime tuple in technical plan
§7.3 still need separate qualification. Neither this change nor a CPU CI pass
certifies the MoltenVK viewer or M4.

Select the native x64 MSVC environment on Windows with an immutable action pin.
The runner already installs the full OpenSSL SDK at `%ProgramFiles%\OpenSSL`;
validate its headers and supply `OPENSSL_ROOT_DIR` to CMake. Avoid a new package
manager or replacing the separately pinned release dependency graph for this
discovery failure. All jobs remain explicit C++23, viewer disabled; the default
development target remains C++26.

Primary sources: [Clang 18 feature macros](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/clang/lib/Frontend/InitPreprocessor.cpp),
[Clang 19 feature macros](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/clang/lib/Frontend/InitPreprocessor.cpp),
[libstdc++ expected header gate](https://gcc.gnu.org/pipermail/libstdc%2B%2B-cvs/2023q4/040660.html),
[Ubuntu Clang 19 package](https://packages.ubuntu.com/noble/clang-19),
[libc++ 20 release notes](https://libcxx.llvm.org/ReleaseNotes/20.html),
[Homebrew LLVM 20 formula](https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/l/llvm%4020.rb),
[Windows runner OpenSSL installer](https://github.com/actions/runner-images/blob/main/images/windows/scripts/build/Install-OpenSSL.ps1),
[CMake OpenSSL hints](https://cmake.org/cmake/help/latest/module/FindOpenSSL.html).
