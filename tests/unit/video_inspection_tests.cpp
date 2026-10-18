#include "inspection_fixture.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

TEST_CASE("storage inspection reports existing volumes without creating future outputs",
          "[inspection][storage]") {
    test::inspection_fixture fixture;
    const auto K_OUTPUT = fixture.root / "future" / "nested" / "scene.wexplore";
    const auto K_REPORT = inspect_output_storage({K_OUTPUT}, 1);
    REQUIRE(K_REPORT);
    REQUIRE(K_REPORT->size() == 1);
    CHECK(K_REPORT->front().m_output == std::filesystem::weakly_canonical(K_OUTPUT));
    CHECK(K_REPORT->front().m_existing_directory == std::filesystem::canonical(fixture.root));
    CHECK(K_REPORT->front().m_available_bytes >= 1);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "future"));
    CHECK_FALSE(inspect_output_storage({K_OUTPUT}, 0));
    CHECK_FALSE(inspect_output_storage({K_OUTPUT}, UINT64_MAX));
    CHECK_FALSE(inspect_output_storage({}, 1));
    CHECK_FALSE(inspect_output_storage({K_OUTPUT, K_OUTPUT, K_OUTPUT, K_OUTPUT, K_OUTPUT}, 1));
    CHECK_FALSE(inspect_output_storage({fixture.request.m_video / "nested"}, 1));
    const auto K_CONSTRAINED_VOLUME = std::filesystem::is_directory("/dev/shm")
                                          ? std::filesystem::path { "/dev/shm" }
                                          : fixture.root;
    const auto K_AVAILABLE = std::filesystem::space(K_CONSTRAINED_VOLUME).available;
    if (K_AVAILABLE < 32ULL * 1024 * 1024 * 1024) {
        const auto K_INSUFFICIENT = inspect_output_storage({K_CONSTRAINED_VOLUME}, K_AVAILABLE + 1);
        REQUIRE_FALSE(K_INSUFFICIENT);
        CHECK(K_INSUFFICIENT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_CANCELLED = inspect_output_storage({K_OUTPUT}, 1, cancellation.get_token());
    REQUIRE_FALSE(K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("automatic inspection gates capabilities and binds returned immutable metadata",
          "[inspection]") {
    test::inspection_fixture fixture;
    std::string log;
    const auto K_FIRST =
        inspect_video(fixture.request, {}, [&](const std::string& text) { log += text; });
    INFO(log);
    INFO((K_FIRST ? "inspected" : K_FIRST.error().m_message));
    REQUIRE(K_FIRST);
    CHECK(K_FIRST->m_report.m_width == 2);
    CHECK(K_FIRST->m_report.m_codec_name == "synthetic_diagnostic");
    CHECK(K_FIRST->m_report.m_storage.size() == fixture.request.m_outputs.size());
    CHECK(K_FIRST->m_report.m_source_sha256 == sha256_file(fixture.request.m_video, 4096).value());
    CHECK(log.find("Worker accepted START") != std::string::npos);
    const auto K_DIRECTORY = fixture.request.m_configuration.m_job_directory;
    const auto K_OWNER_HASH = sha256_file(K_DIRECTORY / ".we-inspection-owner.json", 65536).value();
    const auto K_COMPLETION_HASH = K_FIRST->m_inspection.m_completion_sha256;
    REQUIRE(inspect_video(fixture.request, {}));
    CHECK(sha256_file(K_DIRECTORY / ".we-inspection-owner.json", 65536).value() == K_OWNER_HASH);
    CHECK(sha256_file(K_FIRST->m_inspection.m_stage / "completion.json", 65536).value() ==
          K_COMPLETION_HASH);
    for (const auto& output : fixture.request.m_outputs)
        CHECK_FALSE(std::filesystem::exists(output));
    SECTION("different worker identity") {
        std::ofstream(fixture.request.m_configuration.m_worker.m_script, std::ios::app)
            << "\n# changed\n";
    }
    SECTION("different capture identity") {
        std::ofstream(fixture.request.m_video) << "different video";
    }
    SECTION("corrupt owner") {
        std::ofstream(K_DIRECTORY / ".we-inspection-owner.json") << "broken";
    }
    SECTION("unexpected job content") {
        std::ofstream(K_DIRECTORY / "foreign") << "preserve";
    }
    SECTION("worker lock needs explicit recovery") {
        std::ofstream(K_DIRECTORY / "job.lock") << "unknown pid";
    }
    SECTION("native writer already active") {
        auto lock = FileLock::acquire(K_DIRECTORY / ".we-inspection.lock");
        REQUIRE(lock);
        const auto K_BLOCKED = inspect_video(fixture.request, {});
        REQUIRE_FALSE(K_BLOCKED);
        CHECK(K_BLOCKED.error().m_code == ErrorCode::WOULD_BLOCK);
        return;
    }
    const auto K_COUNTER = sha256_file(K_DIRECTORY / "capabilities.json", 4096).value();
    CHECK_FALSE(inspect_video(fixture.request, {}));
    CHECK(sha256_file(K_DIRECTORY / "capabilities.json", 4096).value() == K_COUNTER);
}

TEST_CASE("inspection capability rejection prevents START and unsafe completions cannot pass",
          "[inspection]") {
    const auto K_MODE = GENERATE("no_video",
                                 "small_source",
                                 "small_output",
                                 "failure",
                                 "missing",
                                 "escape",
                                 "bad_metadata",
                                 "wrong_stage",
                                 "change_source",
                                 "change_script");
    test::inspection_fixture fixture {{}, {}, K_MODE};
    std::string log;
    const auto K_RESULT =
        inspect_video(fixture.request, {}, [&](const std::string& text) { log += text; });
    INFO(log);
    REQUIRE_FALSE(K_RESULT);
    const auto K_MARKER = fixture.request.m_configuration.m_job_directory / "capabilities.json";
    if (std::string_view(K_MODE) == "no_video" || std::string_view(K_MODE) == "small_source" ||
        std::string_view(K_MODE) == "small_output") {
        CHECK(K_RESULT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
        CHECK_FALSE(std::filesystem::exists(K_MARKER));
        CHECK(log.find("Worker accepted START") == std::string::npos);
    } else CHECK(std::filesystem::exists(K_MARKER));
    for (const auto& output : fixture.request.m_outputs)
        CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("inspection rejects unowned output overlap and cancellation without launching a worker",
          "[inspection]") {
    test::inspection_fixture fixture;
    SECTION("occupied directory") {
        std::filesystem::create_directory(fixture.request.m_configuration.m_job_directory);
        std::ofstream(fixture.request.m_configuration.m_job_directory / "foreign") << "preserve";
    }
    SECTION("source overlap") {
        fixture.request.m_configuration.m_job_directory = fixture.request.m_video.parent_path();
    }
    SECTION("worker overlap") {
        fixture.request.m_configuration.m_job_directory =
            fixture.request.m_configuration.m_worker.m_script;
    }
    SECTION("output overlap") {
        fixture.request.m_outputs[0] = fixture.request.m_configuration.m_job_directory / "map";
    }
    SECTION("raw worker prohibited") {
        fixture.request.m_configuration.m_worker.m_framed_protocol = false;
    }
    SECTION("cancel before ownership") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto K_CANCELLED = inspect_video(fixture.request, cancellation.get_token());
        REQUIRE_FALSE(K_CANCELLED);
        CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
        CHECK_FALSE(std::filesystem::exists(fixture.request.m_configuration.m_job_directory));
        return;
    }
    CHECK_FALSE(inspect_video(fixture.request, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.request.m_configuration.m_job_directory /
                                        ".we-inspection.lock"));
}

TEST_CASE("inspection cooperative cancellation releases ownership for same-configuration restart",
          "[inspection]") {
    test::inspection_fixture fixture {{}, {}, "cancel_once"};
    std::stop_source cancellation;
    std::string log;
    const auto K_CANCELLED =
        inspect_video(fixture.request, cancellation.get_token(), [&](const std::string& text) {
            log += text;
            if (text.find("Worker accepted START") != std::string::npos)
                cancellation.request_stop();
        });
    INFO(log);
    REQUIRE_FALSE(K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(log.find("Worker accepted CANCEL") != std::string::npos);
    REQUIRE(inspect_video(fixture.request, {}));
    for (const auto& output : fixture.request.m_outputs)
        CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("file writer locks reject aliases and release without deleting the lock inode",
          "[inspection][locking]") {
    test::inspection_fixture fixture;
    const auto K_PATH = fixture.root / "writer.lock";
    auto first = FileLock::acquire(K_PATH);
    REQUIRE(first);
    const auto K_BLOCKED = FileLock::acquire(K_PATH);
    REQUIRE_FALSE(K_BLOCKED);
    CHECK(K_BLOCKED.error().m_code == ErrorCode::WOULD_BLOCK);
    first->reset();
    CHECK(std::filesystem::is_regular_file(K_PATH));
    REQUIRE(FileLock::acquire(K_PATH));
    SECTION("hard linked file") {
        const auto K_ALIAS = fixture.root / "hard-link.lock";
        std::filesystem::create_hard_link(K_PATH, K_ALIAS);
        CHECK_FALSE(FileLock::acquire(K_ALIAS));
    }
#ifndef _WIN32
    SECTION("symbolic link") {
        const auto K_ALIAS = fixture.root / "symbolic-link.lock";
        std::filesystem::create_symlink(K_PATH, K_ALIAS);
        CHECK_FALSE(FileLock::acquire(K_ALIAS));
    }
#endif
    SECTION("directory") {
        CHECK_FALSE(FileLock::acquire(fixture.root));
    }
}
