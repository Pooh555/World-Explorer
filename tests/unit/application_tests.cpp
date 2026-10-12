#include "world_explorer/application/commands.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>
using namespace world_explorer;
TEST_CASE("native child launch preserves argument boundaries and exit status") {
    std::string output;
    const auto result =
        run_process({WE_TEST_PYTHON, "-c", "import sys; print(sys.argv[1]); sys.exit(7)",
                     "space $HOME `literal` ; not a shell"},
                    {}, [&](const std::string& text) { output += text; });
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
                                    source.get_token(), [](const std::string&) {});
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == error_code::CANCELLED);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(4));
}

TEST_CASE("native input backpressure and a closed child input cannot block cancellation") {
    const auto mode = GENERATE("blocked", "closed", "oversized");
    std::stop_source cancellation;
    bool sent{};
    const auto before = std::chrono::steady_clock::now();
    std::jthread timer([&](const std::stop_token& stopped) {
        for (int i = 0; i < 30 && !stopped.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!stopped.stop_requested())
            cancellation.request_stop();
    });
    const auto result = execute_process(
        {WE_TEST_PYTHON, "-u", "-c",
         std::string_view(mode) == "closed"
             ? "import os,time; os.close(0); print('input closed'); time.sleep(20)"
             : "import time; print('input unread'); time.sleep(20)"},
        cancellation.get_token(), {}, {},
        {[&](bool) -> world_explorer::result<std::vector<std::byte>> {
             if (sent)
                 return std::vector<std::byte>{};
             sent = true;
             return std::vector<std::byte>(PROCESS_INPUT_LIMIT +
                                               (std::string_view(mode) == "oversized" ? 1 : 0),
                                           std::byte{17});
         },
         {}});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == (std::string_view(mode) == "oversized"
                                      ? error_code::RESOURCE_EXHAUSTED
                                      : error_code::CANCELLED));
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(4));
}
