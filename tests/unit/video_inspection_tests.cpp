#include "inspection_fixture.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/core/hash.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

TEST_CASE("storage inspection reports existing volumes without creating future outputs",
          "[inspection][storage]") {
    test::inspection_fixture fixture;
    const auto output = fixture.root / "future" / "nested" / "scene.wexplore";
    const auto report = inspect_output_storage({output}, 1);
    REQUIRE(report);
    REQUIRE(report->size() == 1);
    CHECK(report->front().output == std::filesystem::weakly_canonical(output));
    CHECK(report->front().existing_directory == std::filesystem::canonical(fixture.root));
    CHECK(report->front().available_bytes >= 1);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "future"));
    CHECK_FALSE(inspect_output_storage({output}, 0));
    CHECK_FALSE(inspect_output_storage({output}, UINT64_MAX));
    CHECK_FALSE(inspect_output_storage({}, 1));
    CHECK_FALSE(inspect_output_storage({output, output, output, output, output}, 1));
    CHECK_FALSE(inspect_output_storage({fixture.request.video / "nested"}, 1));
    const auto constrained_volume = std::filesystem::is_directory("/dev/shm")
                                        ? std::filesystem::path{"/dev/shm"}
                                        : fixture.root;
    const auto available = std::filesystem::space(constrained_volume).available;
    if (available < 32ULL * 1024 * 1024 * 1024) {
        const auto insufficient = inspect_output_storage({constrained_volume}, available + 1);
        REQUIRE_FALSE(insufficient);
        CHECK(insufficient.error().code == error_code::RESOURCE_EXHAUSTED);
    }
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelled = inspect_output_storage({output}, 1, cancellation.get_token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == error_code::CANCELLED);
}

TEST_CASE("automatic inspection gates capabilities and binds returned immutable metadata",
          "[inspection]") {
    test::inspection_fixture fixture;
    std::string log;
    const auto first =
        inspect_video(fixture.request, {}, [&](const std::string& text) { log += text; });
    INFO(log);
    INFO((first ? "inspected" : first.error().message));
    REQUIRE(first);
    CHECK(first->report.width == 2);
    CHECK(first->report.codec_name == "synthetic_diagnostic");
    CHECK(first->report.storage.size() == fixture.request.outputs.size());
    CHECK(first->report.source_sha256 == sha256_file(fixture.request.video, 4096).value());
    CHECK(log.find("Worker accepted START") != std::string::npos);
    const auto directory = fixture.request.configuration.job_directory;
    const auto owner_hash = sha256_file(directory / ".we-inspection-owner.json", 65536).value();
    const auto completion_hash = first->inspection.completion_sha256;
    REQUIRE(inspect_video(fixture.request, {}));
    CHECK(sha256_file(directory / ".we-inspection-owner.json", 65536).value() == owner_hash);
    CHECK(sha256_file(first->inspection.stage / "completion.json", 65536).value() ==
          completion_hash);
    for (const auto& output : fixture.request.outputs)
        CHECK_FALSE(std::filesystem::exists(output));
    SECTION("different worker identity") {
        std::ofstream(fixture.request.configuration.worker.script, std::ios::app)
            << "\n# changed\n";
    }
    SECTION("different capture identity") {
        std::ofstream(fixture.request.video) << "different video";
    }
    SECTION("corrupt owner") { std::ofstream(directory / ".we-inspection-owner.json") << "broken"; }
    SECTION("unexpected job content") { std::ofstream(directory / "foreign") << "preserve"; }
    SECTION("worker lock needs explicit recovery") {
        std::ofstream(directory / "job.lock") << "unknown pid";
    }
    SECTION("native writer already active") {
        auto lock = file_lock::acquire(directory / ".we-inspection.lock");
        REQUIRE(lock);
        const auto blocked = inspect_video(fixture.request, {});
        REQUIRE_FALSE(blocked);
        CHECK(blocked.error().code == error_code::WOULD_BLOCK);
        return;
    }
    const auto counter = sha256_file(directory / "capabilities.json", 4096).value();
    CHECK_FALSE(inspect_video(fixture.request, {}));
    CHECK(sha256_file(directory / "capabilities.json", 4096).value() == counter);
}

TEST_CASE("inspection capability rejection prevents START and unsafe completions cannot pass",
          "[inspection]") {
    const auto mode =
        GENERATE("no_video", "small_source", "small_output", "failure", "missing", "escape",
                 "bad_metadata", "wrong_stage", "change_source", "change_script");
    test::inspection_fixture fixture{{}, {}, mode};
    std::string log;
    const auto result =
        inspect_video(fixture.request, {}, [&](const std::string& text) { log += text; });
    INFO(log);
    REQUIRE_FALSE(result);
    const auto marker = fixture.request.configuration.job_directory / "capabilities.json";
    if (std::string_view(mode) == "no_video" || std::string_view(mode) == "small_source" ||
        std::string_view(mode) == "small_output") {
        CHECK(result.error().code == error_code::UNSUPPORTED_OPERATION);
        CHECK_FALSE(std::filesystem::exists(marker));
        CHECK(log.find("Worker accepted START") == std::string::npos);
    } else
        CHECK(std::filesystem::exists(marker));
    for (const auto& output : fixture.request.outputs)
        CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("inspection rejects unowned output overlap and cancellation without launching a worker",
          "[inspection]") {
    test::inspection_fixture fixture;
    SECTION("occupied directory") {
        std::filesystem::create_directory(fixture.request.configuration.job_directory);
        std::ofstream(fixture.request.configuration.job_directory / "foreign") << "preserve";
    }
    SECTION("source overlap") {
        fixture.request.configuration.job_directory = fixture.request.video.parent_path();
    }
    SECTION("worker overlap") {
        fixture.request.configuration.job_directory = fixture.request.configuration.worker.script;
    }
    SECTION("output overlap") {
        fixture.request.outputs[0] = fixture.request.configuration.job_directory / "map";
    }
    SECTION("raw worker prohibited") {
        fixture.request.configuration.worker.framed_protocol = false;
    }
    SECTION("cancel before ownership") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto cancelled = inspect_video(fixture.request, cancellation.get_token());
        REQUIRE_FALSE(cancelled);
        CHECK(cancelled.error().code == error_code::CANCELLED);
        CHECK_FALSE(std::filesystem::exists(fixture.request.configuration.job_directory));
        return;
    }
    CHECK_FALSE(inspect_video(fixture.request, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.request.configuration.job_directory /
                                        ".we-inspection.lock"));
}

TEST_CASE("inspection cooperative cancellation releases ownership for same-configuration restart",
          "[inspection]") {
    test::inspection_fixture fixture{{}, {}, "cancel_once"};
    std::stop_source cancellation;
    std::string log;
    const auto cancelled =
        inspect_video(fixture.request, cancellation.get_token(), [&](const std::string& text) {
            log += text;
            if (text.find("Worker accepted START") != std::string::npos)
                cancellation.request_stop();
        });
    INFO(log);
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == error_code::CANCELLED);
    CHECK(log.find("Worker accepted CANCEL") != std::string::npos);
    REQUIRE(inspect_video(fixture.request, {}));
    for (const auto& output : fixture.request.outputs)
        CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("file writer locks reject aliases and release without deleting the lock inode",
          "[inspection][locking]") {
    test::inspection_fixture fixture;
    const auto path = fixture.root / "writer.lock";
    auto first = file_lock::acquire(path);
    REQUIRE(first);
    const auto blocked = file_lock::acquire(path);
    REQUIRE_FALSE(blocked);
    CHECK(blocked.error().code == error_code::WOULD_BLOCK);
    first->reset();
    CHECK(std::filesystem::is_regular_file(path));
    REQUIRE(file_lock::acquire(path));
    SECTION("hard linked file") {
        const auto alias = fixture.root / "hard-link.lock";
        std::filesystem::create_hard_link(path, alias);
        CHECK_FALSE(file_lock::acquire(alias));
    }
#ifndef _WIN32
    SECTION("symbolic link") {
        const auto alias = fixture.root / "symbolic-link.lock";
        std::filesystem::create_symlink(path, alias);
        CHECK_FALSE(file_lock::acquire(alias));
    }
#endif
    SECTION("directory") { CHECK_FALSE(file_lock::acquire(fixture.root)); }
}
