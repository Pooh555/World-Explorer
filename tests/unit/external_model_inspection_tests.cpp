#include "../fixtures/asset_component_fixture.hpp"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/external_model_inspection.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <chrono>

using namespace world_explorer;
using namespace world_explorer::asset_component_test;

namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& value) {
    const auto K_TEXT = value.generic_u8string();
    return { K_TEXT.begin(), K_TEXT.end() };
}

struct RequestFixture {
    EvidenceFixture m_evidence;
    RecipeFixture m_output;
    Json m_request;
    std::string m_payload { "Synthetic MODEL; not executable weights." };

    explicit RequestFixture(bool unknown_cost = false, bool restricted = false) {
        write(m_output.m_root / "model.bin", m_payload);
        write(m_output.m_root / "config.bin", "Synthetic MODEL configuration");
        const auto K_CONFIG = hash("Synthetic MODEL configuration");
        m_evidence.m_manifests.push_back(
            { { "schema_version", 2 },
             { "kind", "EXTERNAL_MODEL_FILES" },
             { "role", "MODEL" },
             { "archive", nullptr },
             { "installed_bytes",
              m_payload.size() + std::string("Synthetic MODEL configuration").size() },
             { "files",
              Json::array({ { { "path", "weights/model.bin" },
                            { "sha256", hash(m_payload) },
                            { "bytes", m_payload.size() } },
                           { { "path", "config.bin" },
                            { "sha256", K_CONFIG },
                            { "bytes", std::string("Synthetic MODEL configuration").size() } } }) } });
        if (unknown_cost) {
            m_evidence.m_change_recipe = [](auto& recipe) {
                recipe["components"][0uz]["installed_bytes"] = nullptr;
            };
        }
        if (restricted) {
            m_evidence.m_local_use = "RESTRICTED";
        }
        const auto K_RECEIPTS = m_evidence.freeze();
        fs::copy(
            m_evidence.m_recipe.m_root, m_output.m_root / "recipe", fs::copy_options::recursive);
        fs::copy(m_evidence.m_evidence.m_root,
                 m_output.m_root / "evidence",
                 fs::copy_options::recursive);
        const auto& selected = m_evidence.m_selected[2uz];
        m_request = {
            { "schema_version", 1 },
            { "kind", "EXTERNAL_MODEL_OBSERVATION" },
            { "source_kind", "SYNTHETIC" },
            { "recipe_receipt",
             { { "directory", "recipe" }, { "sha256", K_RECEIPTS.first.receipt_sha256() } } },
            { "evidence_receipt",
             { { "directory", "evidence" }, { "sha256", K_RECEIPTS.second.receipt_sha256() } } },
            { "product_id", 1000 },
            { "context", { { "world_epoch", 3 }, { "pose_revision", 5 } } },
            { "host",
             { { "contracts_only", true },
              { "host_ram_bytes", 100 },
              { "device_vram_bytes", 0 },
              { "free_disk_bytes", 1000000 },
              { "cpu_threads", 2 },
              { "queued_jobs", 1 } } },
            { "limits", { { "file_bytes", 1024 }, { "total_bytes", 4096 } } },
            { "models",
             Json::array(
                 { { { "component_sha256", selected.m_component_sha256 },
                   { "files",
                    Json::array({ { { "path", "weights/model.bin" },
                                  { "sha256", hash(m_payload) },
                                  { "bytes", m_payload.size() },
                                  { "file", "model.bin" } },
                                 { { "path", "config.bin" },
                                  { "sha256", K_CONFIG },
                                  { "bytes", std::string("Synthetic MODEL configuration").size() },
                                  { "file", "config.bin" } } }) } } }) },
            { "reviews",
             Json::array({ { { "role", "MODEL" },
                           { "component_sha256", selected.m_component_sha256 },
                           { "review_sha256", selected.m_review_sha256 },
                           { "reviewer", selected.m_reviewer } } }) } };
    }

    fs::path request() const {
        return m_output.m_root / "request.json";
    }

    std::string save() const {
        const auto K_TEXT = m_request.dump();
        write(request(), K_TEXT);
        return hash(K_TEXT);
    }

    auto inspect() const {
        return inspect_asset_external_models_request(request(), save());
    }
};

void blocker(const Json& report, const std::string& name) {
    const auto& rows = report.at("blockers");
    CHECK(std::ranges::find_if(rows, [&](const auto& row) {
              return row.template get<std::string>() == name;
          }) != rows.end());
}

}  // namespace

TEST_CASE("external MODEL request reopens independent snapshots and resolves files from its parent",
          "[external-model-inspection]") {
    RequestFixture fixture;
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    std::vector<AssetExternalModelProgress> progress;
    const auto K_RESULT =
        inspect_asset_external_models_request(fixture.request(), K_SHA, {}, [&](const auto& value) {
            progress.push_back(value);
        });
    INFO((K_RESULT ? "Success" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    const auto K_REPORT = Json::parse(*K_RESULT);
    CHECK(K_REPORT.at("schema_version") == 1);
    CHECK(K_REPORT.at("kind").get<std::string>() == "EXTERNAL_MODEL_OBSERVATION");
    CHECK(K_REPORT.at("request_sha256").get<std::string>() == K_SHA);
    CHECK(K_REPORT.at("recipe_receipt_sha256") == fixture.m_request["recipe_receipt"]["sha256"]);
    CHECK(K_REPORT.at("evidence_receipt_sha256") ==
          fixture.m_request["evidence_receipt"]["sha256"]);
    CHECK(K_REPORT.at("context") == fixture.m_request["context"]);
    CHECK(K_REPORT.at("host") == fixture.m_request["host"]);
    CHECK(K_REPORT.at("limits") == fixture.m_request["limits"]);
    CHECK(K_REPORT.at("source_kind").get<std::string>() == "SYNTHETIC");
    CHECK(K_REPORT.at("qualification").get<std::string>() == "UNQUALIFIED");
    CHECK(K_REPORT.at("observation_only").get<bool>());
    CHECK(!K_REPORT.at("immutable_payloads").get<bool>());
    CHECK(!K_REPORT.at("execution_authorized").get<bool>());
    const auto& model = K_REPORT.at("models")[0uz];
    CHECK(model.at("component_sha256") == fixture.m_request["models"][0uz]["component_sha256"]);
    CHECK(model.at("review_sha256") == fixture.m_request["reviews"][0uz]["review_sha256"]);
    CHECK(model.at("review_selected").get<bool>());
    CHECK(model.at("files")[0uz]["file"].get<std::string>() ==
          utf8(fixture.m_output.m_root / "model.bin"));
    CHECK(model.at("files")[0uz]["sha256"].get<std::string>() == hash(fixture.m_payload));
    CHECK(K_REPORT.at("hashed_bytes").get<std::uint64_t>() ==
          2uz * K_REPORT.at("observed_bytes").get<std::uint64_t>());
    REQUIRE(progress.size() == 4uz);
    CHECK(progress[0uz].m_pass == 1uz);
    CHECK(progress[3uz].m_pass == 2uz);
    CHECK(progress[3uz].m_file_index == 1uz);
    blocker(K_REPORT, "CONTRACTS_ONLY_HOST");
    blocker(K_REPORT, "COMPONENT_BYTES_UNVERIFIED");
    blocker(K_REPORT, "INSTALLED_FILES_UNVERIFIED");
    blocker(K_REPORT, "REVIEW_NOT_SELECTED");
    blocker(K_REPORT, "DISTRIBUTION_NOT_CLEARED");
    blocker(K_REPORT, "RUNTIME_CLOSURE_UNVERIFIED");
    blocker(K_REPORT, "WORKER_LIMITS_UNVERIFIED");
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL request retains unknown costs restricted rights and unselected reviews",
          "[external-model-inspection]") {
    RequestFixture fixture(true, true);
    fixture.m_request["reviews"] = Json::array();
    fixture.m_request["source_kind"] = "UNQUALIFIED";
    fixture.m_request["host"]["contracts_only"] = false;
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    const auto K_REPORT = Json::parse(*K_RESULT);
    CHECK(K_REPORT.at("installation_bytes").is_null());
    CHECK(K_REPORT.at("reserved_disk_bytes").is_null());
    CHECK(!K_REPORT.at("models")[0uz].at("review_selected").get<bool>());
    CHECK(!K_REPORT.at("execution_authorized").get<bool>());
    blocker(K_REPORT, "UNKNOWN_INSTALLATION_SIZE");
    blocker(K_REPORT, "RESTRICTED_LOCAL_RIGHTS");
    blocker(K_REPORT, "DISTRIBUTION_NOT_CLEARED");
    blocker(K_REPORT, "REVIEW_NOT_SELECTED");
}

TEST_CASE("external MODEL request handles explicit UTF8 native names and absolute selections",
          "[external-model-inspection]") {
    RequestFixture fixture;
    const auto K_NATIVE = fixture.m_output.m_root / fs::path(u8"weights \u03bb.bin");
    fs::rename(fixture.m_output.m_root / "model.bin", K_NATIVE);
    fixture.m_request["models"][0uz]["files"][0uz]["file"] = utf8(K_NATIVE.filename());
    fixture.m_request["recipe_receipt"]["directory"] = utf8(fixture.m_output.m_root / "recipe");
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "Success" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(Json::parse(*K_RESULT)["models"][0uz]["files"][0uz]["file"].get<std::string>() ==
          utf8(K_NATIVE));
}

TEST_CASE(
    "external MODEL request refuses changed independent pins files context and review selections",
    "[external-model-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    RequestFixture fixture;
    auto& file = fixture.m_request["models"][0uz]["files"][0uz];
    if (K_CASE == 0) {
        fixture.m_request["recipe_receipt"]["sha256"] = hash("wrong");
    }
    if (K_CASE == 1) {
        fixture.m_request["evidence_receipt"]["sha256"] = hash("wrong");
    }
    if (K_CASE == 2) {
        fixture.m_request["product_id"] = 999;
    }
    if (K_CASE == 3) {
        fixture.m_request["context"]["pose_revision"] = 99;
    }
    if (K_CASE == 4) {
        fixture.m_request["models"][0uz]["component_sha256"] = hash("wrong");
    }
    if (K_CASE == 5) {
        file["sha256"] = hash("wrong");
    }
    if (K_CASE == 6) {
        file["bytes"] = fixture.m_payload.size() + 1uz;
    }
    if (K_CASE == 7) {
        file["path"] = "weights/MODEL.bin";
    }
    if (K_CASE == 8) {
        fixture.m_request["reviews"][0uz]["review_sha256"] = hash("wrong");
    }
    if (K_CASE == 9) {
        fixture.m_request["reviews"][0uz]["reviewer"] = "wrong.reviewer";
    }
    if (K_CASE == 10) {
        write(fixture.m_output.m_root / "model.bin", std::string(fixture.m_payload.size(), 'x'));
    }
    if (K_CASE == 11) {
        file["file"] = "config.bin";
    }
    if (K_CASE == 12) {
        fixture.m_request["models"].push_back(fixture.m_request["models"][0uz]);
    }
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    CHECK(!inspect_asset_external_models_request(fixture.request(), K_SHA));
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL request validates schema types enums paths and empty selections",
          "[external-model-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    RequestFixture fixture;
    auto& file = fixture.m_request["models"][0uz]["files"][0uz];
    if (K_CASE == 0) {
        fixture.m_request["schema_version"] = 2;
    }
    if (K_CASE == 1) {
        fixture.m_request["kind"] = "OTHER";
    }
    if (K_CASE == 2) {
        fixture.m_request["source_kind"] = "VERIFIED";
    }
    if (K_CASE == 3) {
        fixture.m_request["host"]["contracts_only"] = 1;
    }
    if (K_CASE == 4) {
        fixture.m_request["host"]["host_ram_bytes"] = -1;
    }
    if (K_CASE == 5) {
        file["bytes"] = true;
    }
    if (K_CASE == 6) {
        file["sha256"] = "not a digest";
    }
    if (K_CASE == 7) {
        file["file"] = "recipe/../model.bin";
    }
    if (K_CASE == 8) {
        file["file"] = std::string("model.bin\0suffix", 16uz);
    }
    if (K_CASE == 9) {
        file["file"] = std::string(4097uz, 'x');
    }
    if (K_CASE == 10) {
        fixture.m_request["models"] = Json::array();
    }
    if (K_CASE == 11) {
        fixture.m_request["models"][0uz]["files"] = Json::array();
    }
    if (K_CASE == 12) {
        fixture.m_request["reviews"][0uz]["role"] = "CODE";
    }
    if (K_CASE == 13) {
        fixture.m_request["product_id"] = 0;
    }
    if (K_CASE == 14) {
        fixture.m_request["unknown"] = false;
    }
    if (K_CASE == 15) {
        fixture.m_request["context"]["pose_revision"] = 0;
    }
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    CHECK(!inspect_asset_external_models_request(fixture.request(), K_SHA));
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL request count and byte ceilings reject before opening selected receipts",
          "[external-model-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    RequestFixture fixture;
    fixture.m_request["recipe_receipt"]["directory"] = "missing";
    if (K_CASE == 0) {
        fixture.m_request["models"] = std::vector<Json>(17uz, fixture.m_request["models"][0uz]);
    }
    if (K_CASE == 1) {
        fixture.m_request["reviews"] = std::vector<Json>(17uz, fixture.m_request["reviews"][0uz]);
    }
    if (K_CASE == 2) {
        fixture.m_request["models"][0uz]["files"] =
            std::vector<Json>(65uz, fixture.m_request["models"][0uz]["files"][0uz]);
    }
    if (K_CASE == 3) {
        auto model = fixture.m_request["models"][0uz];
        model["files"] = std::vector<Json>(64uz, model["files"][0uz]);
        fixture.m_request["models"] = std::vector<Json>(5uz, model);
        fixture.m_request["limits"]["total_bytes"] = 65536;
    }
    if (K_CASE == 4) {
        fixture.m_request["limits"]["file_bytes"] = 0;
    }
    if (K_CASE == 5) {
        fixture.m_request["limits"] = { { "file_bytes", 5uz * 1024uz * 1024uz * 1024uz },
                                       { "total_bytes", 6uz * 1024uz * 1024uz * 1024uz } };
    }
    if (K_CASE == 6) {
        fixture.m_request["limits"]["total_bytes"] = 1;
    }
    if (K_CASE == 7) {
        fixture.m_request["limits"] = { { "file_bytes", fixture.m_payload.size() },
                                       { "total_bytes", fixture.m_payload.size() } };
    }
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == (K_CASE >= 4 && K_CASE <= 6 ? ErrorCode::INVALID_ARGUMENT
                                                                 : ErrorCode::RESOURCE_EXHAUSTED));
}

TEST_CASE("external MODEL request checks exact raw bytes nesting duplicate keys and hash",
          "[external-model-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    RequestFixture fixture;
    auto raw = fixture.m_request.dump();
    if (K_CASE == 0) {
        raw.insert(1uz, "\"schema_version\":1,");
    }
    if (K_CASE == 1) {
        raw.push_back('\0');
    }
    if (K_CASE == 2) {
        raw = std::string(18uz, '[') + "0" + std::string(18uz, ']');
    }
    if (K_CASE == 3 || K_CASE == 4) {
        raw.resize(2uz * 1024uz * 1024uz + (K_CASE == 4 ? 1uz : 0uz), ' ');
    }
    write(fixture.request(), raw);
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    const auto K_RESULT = inspect_asset_external_models_request(
        fixture.request(), K_CASE == 5 ? hash("wrong") : hash(raw));
    if (K_CASE == 3) {
        REQUIRE(K_RESULT);
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == (K_CASE == 2 || K_CASE == 4 ? ErrorCode::RESOURCE_EXHAUSTED
                                          : K_CASE == 5              ? ErrorCode::RESYNC_REQUIRED
                                                                     : ErrorCode::INVALID_DATA));
    }
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL request preserves snapshots on cancellation and checked file mutation",
          "[external-model-inspection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    RequestFixture fixture;
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    std::stop_source source;
    if (K_CASE == 0) {
        source.request_stop();
    }
    const auto K_RESULT = inspect_asset_external_models_request(
        fixture.request(), K_SHA, source.get_token(), [&](const auto& value) {
            if (K_CASE == 1) {
                source.request_stop();
            }
            if (K_CASE == 2) {
                throw std::runtime_error("Synthetic callback failure");
            }
            if (K_CASE == 3 && value.m_file_index == 1uz) {
                const auto K_FILE = fixture.m_output.m_root / "model.bin";
                fs::last_write_time(K_FILE, fs::last_write_time(K_FILE) + std::chrono::seconds(2));
            }
        });
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == (K_CASE <= 1   ? ErrorCode::CANCELLED
                                      : K_CASE == 2 ? ErrorCode::BACKEND_FAILURE
                                                    : ErrorCode::RESYNC_REQUIRED));
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL request rejects linked requests receipt roots and model parents",
          "[external-model-inspection]") {
#ifdef _WIN32
    SKIP("Synthetic symlink creation requires independently granted Windows privileges");
#else
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    RequestFixture fixture;
    const auto K_SHA = fixture.save();
    auto request = fixture.request();
    if (K_CASE == 0) {
        fs::create_symlink(request, fixture.m_output.m_root / "alias.json");
        request = fixture.m_output.m_root / "alias.json";
    }
    if (K_CASE == 1) {
        fs::rename(fixture.m_output.m_root / "recipe", fixture.m_output.m_root / "original");
        fs::create_directory_symlink(fixture.m_output.m_root / "original",
                                     fixture.m_output.m_root / "recipe");
    }
    if (K_CASE == 2 || K_CASE == 3) {
        fs::create_directory_symlink(fixture.m_output.m_root, fixture.m_output.m_root / "alias");
        if (K_CASE == 2) {
            request = fixture.m_output.m_root / "alias/request.json";
        } else {
            fixture.m_request["models"][0uz]["files"][0uz]["file"] = "alias/model.bin";
            fixture.save();
        }
    }
    const auto K_RESULT =
        inspect_asset_external_models_request(request, K_CASE == 3 ? fixture.save() : K_SHA);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(sha256_file(fixture.m_output.m_root / "model.bin", 1024).value() ==
          hash(fixture.m_payload));
#endif
}

TEST_CASE(
    "external MODEL CLI prints shared JSON and refuses bad hashes arguments and changed bytes",
    "[external-model-inspection]") {
    RequestFixture fixture;
    const auto K_SHA = fixture.save();
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    const auto K_SHARED = inspect_asset_external_models_request(fixture.request(), K_SHA);
    REQUIRE(K_SHARED);
    std::string output;
    const auto K_LOG = [&](const std::string& value) {
        output += value;
    };
    const auto K_RUN = [&](const std::vector<std::string>& args) {
        output.clear();
        const auto K_RESULT = run_process(args, {}, K_LOG);
        REQUIRE(K_RESULT);
        return *K_RESULT;
    };
    CHECK(K_RUN({ WE_TEST_PROCESS, "inspect-external-models", fixture.request().string(), K_SHA }) ==
          0);
    CHECK(Json::parse(output) == Json::parse(*K_SHARED));
    CHECK(K_RUN({ WE_TEST_PROCESS,
                 "inspect-external-models",
                 fixture.request().string(),
                 hash("wrong") }) == 2);
    CHECK(output.find("checksum differs") != output.npos);
    CHECK(K_RUN({ WE_TEST_PROCESS, "inspect-external-models" }) == 2);
    CHECK(output.find("Usage:") != output.npos);
    CHECK(K_RUN({ WE_TEST_PROCESS,
                 "inspect-external-models",
                 fixture.request().string(),
                 K_SHA,
                 "extra" }) == 2);
    CHECK(K_RUN({ WE_TEST_PROCESS, "--help" }) == 0);
    CHECK(output.find("inspect-external-models") != output.npos);
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
    write(fixture.m_output.m_root / "model.bin", "Changed synthetic data");
    const auto K_CHANGED = tree(fixture.m_output.m_root);
    CHECK(K_RUN({ WE_TEST_PROCESS, "inspect-external-models", fixture.request().string(), K_SHA }) ==
          2);
    CHECK(output.find("differs") != output.npos);
    CHECK(tree(fixture.m_output.m_root) == K_CHANGED);
}
