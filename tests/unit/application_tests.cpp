#include "world_explorer/application/commands.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <thread>
using namespace world_explorer;

TEST_CASE("native child launch preserves argument boundaries and exit status") {
    std::string output;
    const auto K_RESULT = run_process({ WE_TEST_PYTHON,
                                       "-c",
                                       "import sys; print(sys.argv[1]); sys.exit(7)",
                                       "space $HOME `literal` ; not a shell" },
                                      {},
                                      [&](const std::string& text) {
                                          output += text;
                                      });
    REQUIRE(K_RESULT);
    REQUIRE(*K_RESULT == 7);
    REQUIRE(output.find("space $HOME `literal` ; not a shell") != std::string::npos);
}

TEST_CASE("native child cancellation terminates an active worker") {
    std::stop_source source;
    std::jthread cancel([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        source.request_stop();
    });
    const auto K_START = std::chrono::steady_clock::now();
    const auto K_RESULT = run_process({ WE_TEST_PYTHON, "-c", "import time; time.sleep(20)" },
                                      source.get_token(),
                                      [](const std::string&) {
                                      });
    REQUIRE_FALSE(K_RESULT);
    REQUIRE(K_RESULT.error().m_code == ErrorCode::CANCELLED);
    REQUIRE(std::chrono::steady_clock::now() - K_START < std::chrono::seconds(4));
}

TEST_CASE("native input backpressure and a closed child input cannot block cancellation") {
    const auto K_MODE = GENERATE("blocked", "closed", "oversized");
    std::stop_source cancellation;
    bool sent {};
    const auto K_BEFORE = std::chrono::steady_clock::now();
    std::jthread timer([&](const std::stop_token& stopped) {
        for (int i { 0 }; i < 30 && !stopped.stop_requested(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!stopped.stop_requested()) {
            cancellation.request_stop();
        }
    });
    const auto K_RESULT = execute_process(
        { WE_TEST_PYTHON,
         "-u",
         "-c",
         std::string_view(K_MODE) == "closed"
             ? "import os,time; os.close(0); print('input closed'); time.sleep(20)"
             : "import time; print('input unread'); time.sleep(20)" },
        cancellation.get_token(),
        {},
        {},
        { [&](bool) -> world_explorer::Result<std::vector<std::byte>> {
             if (sent) {
                 return std::vector<std::byte> {};
             }
             sent = true;
             return std::vector<std::byte>(K_PROCESS_INPUT_LIMIT +
                                               (std::string_view(K_MODE) == "oversized" ? 1 : 0),
                                           std::byte { 17 });
         },
         {} });
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == (std::string_view(K_MODE) == "oversized"
                                          ? ErrorCode::RESOURCE_EXHAUSTED
                                          : ErrorCode::CANCELLED));
    CHECK(std::chrono::steady_clock::now() - K_BEFORE < std::chrono::seconds(4));
}
