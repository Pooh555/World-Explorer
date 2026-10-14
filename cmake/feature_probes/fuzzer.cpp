#include <cstddef>
#include <cstdint>

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's required external C entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
