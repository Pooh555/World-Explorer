#include "world_explorer/application/commands.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
using namespace world_explorer;
TEST_CASE("native child launch preserves argument boundaries and exit status") {
    std::string output;
    const auto result =
        run_process({WE_TEST_PYTHON, "-c", "import sys; print(sys.argv[1]); sys.exit(7)",
                     "space $HOME `literal` ; not a shell"},
                    {}, [&](std::string text) { output += text; });
    REQUIRE(result);
    REQUIRE(*result == 7);
    REQUIRE(output.find("space $HOME `literal` ; not a shell") != std::string::npos);
}
TEST_CASE("native child cancellation terminates an active worker") {
    std::stop_source source;
    std::jthread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        source.request_stop();
    });
    const auto start = std::chrono::steady_clock::now();
    const auto result = run_process({WE_TEST_PYTHON, "-c", "import time; time.sleep(20)"},
                                    source.get_token(), [](std::string) {});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == error_code::CANCELLED);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(4));
}
