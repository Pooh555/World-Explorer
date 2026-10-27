#include "../fixtures/asset_component_fixture.hpp"
#include "../fixtures/probe_environment.hpp"
#include "world_explorer/asset_catalog/external_models.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <chrono>

using namespace world_explorer;
using namespace world_explorer::asset_component_test;

namespace {

namespace fs = std::filesystem;

struct ModelFixture {
    EvidenceFixture m_evidence;
    RecipeFixture m_storage;
    std::vector<AssetExternalModelSelection> m_models;
    std::vector<AssetReviewSelection> m_reviews;
    AssetExternalModelLimits m_limits { 1024, 4096 };

    void file(std::size_t model, const std::string& path, const std::string& bytes) {
        REQUIRE(model <= m_models.size());
        if (model == m_models.size()) {
            m_models.push_back({ {}, {} });
            m_evidence.m_manifests.push_back({ { "schema_version", 2 },
                                              { "kind", "EXTERNAL_MODEL_FILES" },
                                              { "role", "MODEL" },
                                              { "archive", nullptr },
                                              { "installed_bytes", 0 },
                                              { "files", Json::array() } });
        }
        const auto K_FILE = m_storage.m_root / ("file" + std::to_string(model) + "-" +
                                                std::to_string(m_models[model].m_files.size()));
        write(K_FILE, bytes);
        m_models[model].m_files.push_back({ path, hash(bytes), bytes.size(), K_FILE });
        auto& manifest = m_evidence.m_manifests[model + 2uz];
        manifest["files"].push_back(
            { { "path", path }, { "sha256", hash(bytes) }, { "bytes", bytes.size() } });
        manifest["installed_bytes"] =
            manifest["installed_bytes"].get<std::uint64_t>() + bytes.size();
    }

    ModelFixture() {
        file(0uz, "weights/model.bin", "Synthetic MODEL; not executable weights.");
    }

    auto freeze() {
        auto receipts = m_evidence.freeze();
        m_reviews.clear();
        for (std::size_t index { 0uz }; index < m_models.size(); ++index) {
            m_models[index].m_component_sha256 = hash(m_evidence.m_manifests[index + 2uz].dump());
            m_reviews.push_back(m_evidence.m_selected[index + 2uz]);
        }
        return receipts;
    }

    auto
    inspect(const std::pair<AdmittedAssetReceipt, AdmittedAssetReceipt>& receipts,
            const std::stop_token& stop = {},
            const std::function<void(const AssetExternalModelProgress&)>& progress = {}) const {
        return inspect_asset_external_models(receipts.first,
                                             { 1000 },
                                             receipts.second,
                                             m_evidence.m_recipe.m_context,
                                             m_evidence.m_recipe.m_host,
                                             m_reviews,
                                             m_models,
                                             m_limits,
                                             stop,
                                             progress);
    }
};

bool blocked(const AssetExternalModelObservation& result, AssetRecipeBlocker blocker) {
    return std::ranges::find(result.m_recipe.m_blockers, blocker) !=
           result.m_recipe.m_blockers.end();
}

void large_model(std::uint64_t bytes, const std::string& expected_sha) {
    ModelFixture fixture;
    auto& file = fixture.m_models[0uz].m_files[0uz];
    fs::path retained;
    if (auto output = test_support::probe_environment("WE_EXTERNAL_MODEL_PROBE_ROOT")) {
        retained = fs::path(*output) / std::to_string(bytes);
        REQUIRE(fs::create_directory(retained));
        retained = fs::canonical(retained);
        file.m_file = retained / "synthetic-model.bin";
    }
    const std::string K_PREFIX { "World Explorer synthetic external model\n" };
    const std::string K_SUFFIX { "\nEND synthetic external model\n" };
    std::ofstream output(file.m_file, std::ios::binary);
    output.write(K_PREFIX.data(), static_cast<std::streamsize>(K_PREFIX.size()));
    output.seekp(static_cast<std::streamoff>(bytes - K_SUFFIX.size()));
    output.write(K_SUFFIX.data(), static_cast<std::streamsize>(K_SUFFIX.size()));
    output.close();
    REQUIRE(output);
    file.m_bytes = bytes;
    file.m_sha256 = expected_sha;
    auto& manifest = fixture.m_evidence.m_manifests[2uz];
    manifest["installed_bytes"] = bytes;
    manifest["files"][0uz]["bytes"] = bytes;
    manifest["files"][0uz]["sha256"] = expected_sha;
    fixture.m_limits = { bytes, bytes };
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_evidence.m_recipe.m_root);
    const auto K_EVIDENCE = tree(fixture.m_evidence.m_evidence.m_root);
    std::vector<AssetExternalModelProgress> progress;
    const auto K_RESULT = fixture.inspect(K_RECEIPTS, {}, [&](const auto& value) {
        progress.push_back(value);
    });
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_observed_bytes == bytes);
    CHECK(K_RESULT->m_hashed_bytes == 2uz * bytes);
    REQUIRE(progress.size() == 2uz);
    CHECK(progress[0uz].m_pass == 1uz);
    CHECK(progress[1uz].m_pass == 2uz);
    CHECK(progress[0uz].m_hashed_bytes == bytes);
    CHECK(progress[1uz].m_hashed_bytes == 2uz * bytes);
    CHECK(K_RESULT->m_models[0uz].m_review_selected);
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::CONTRACTS_ONLY_HOST));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::HOST_CAPACITY_EXCEEDED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::REVIEW_NOT_SELECTED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::RUNTIME_CLOSURE_UNVERIFIED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::WORKER_LIMITS_UNVERIFIED));
    CHECK(tree(fixture.m_evidence.m_recipe.m_root) == K_BEFORE);
    CHECK(tree(fixture.m_evidence.m_evidence.m_root) == K_EVIDENCE);
    if (!retained.empty()) {
        fs::copy(
            fixture.m_evidence.m_recipe.m_root, retained / "recipe", fs::copy_options::recursive);
        fs::copy(fixture.m_evidence.m_evidence.m_root,
                 retained / "evidence",
                 fs::copy_options::recursive);
        const Json K_REPORT { { "synthetic", true },
                             { "schema_version", K_RESULT->m_schema_version },
                             { "recipe_receipt_sha256", K_RESULT->m_recipe.m_receipt_sha256 },
                             { "recipe_sha256", K_RESULT->m_recipe.m_recipe_sha256 },
                             { "evidence_receipt_sha256", K_RESULT->m_evidence_receipt_sha256 },
                             { "manifest", manifest },
                             { "component_sha256", K_RESULT->m_models[0uz].m_component_sha256 },
                             { "terms_sha256", K_RESULT->m_models[0uz].m_terms_sha256 },
                             { "review_sha256", K_RESULT->m_models[0uz].m_review_sha256 },
                             { "review_selected", K_RESULT->m_models[0uz].m_review_selected },
                             { "file", file.m_file.generic_string() },
                             { "observed_bytes", K_RESULT->m_observed_bytes },
                             { "hashed_bytes", K_RESULT->m_hashed_bytes },
                             { "passes", progress.size() },
                             { "global_blockers", K_RESULT->m_recipe.m_blockers } };
        write(retained / "observation.json", K_REPORT.dump(2));
    }
}

}  // namespace

TEST_CASE("external MODEL observations bind multiple inventories without clearing recipe blockers",
          "[asset-external-models]") {
    ModelFixture fixture;
    fixture.file(0uz, "weights/config.bin", "Synthetic configuration");
    fixture.file(1uz, "second.bin", "Another synthetic MODEL");
    fixture.m_evidence.m_change_recipe = [](auto& value) {
        value["components"][0uz]["installed_bytes"] = nullptr;
    };
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_storage.m_root);
    std::ranges::reverse(fixture.m_models);
    std::ranges::reverse(fixture.m_models[1uz].m_files);
    std::ranges::reverse(fixture.m_reviews);
    std::vector<AssetExternalModelProgress> progress;
    const auto K_RESULT = fixture.inspect(K_RECEIPTS, {}, [&](const auto& value) {
        progress.push_back(value);
    });
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_schema_version == 2);
    CHECK(K_RESULT->m_recipe.m_context.m_world_epoch ==
          fixture.m_evidence.m_recipe.m_context.m_world_epoch);
    CHECK(K_RESULT->m_recipe.m_context.m_pose_revision ==
          fixture.m_evidence.m_recipe.m_context.m_pose_revision);
    CHECK(K_RESULT->m_recipe.m_receipt_sha256 == K_RECEIPTS.first.receipt_sha256());
    CHECK(K_RESULT->m_evidence_receipt_sha256 == K_RECEIPTS.second.receipt_sha256());
    REQUIRE(K_RESULT->m_models.size() == 2uz);
    CHECK(K_RESULT->m_models[0uz].m_component_sha256 == fixture.m_models[1uz].m_component_sha256);
    CHECK(K_RESULT->m_models[0uz].m_files[0uz].m_relative_path == "weights/model.bin");
    CHECK(K_RESULT->m_models[0uz].m_review_selected);
    CHECK(K_RESULT->m_models[1uz].m_review_selected);
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::UNKNOWN_INSTALLATION_SIZE));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::CONTRACTS_ONLY_HOST));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::DISTRIBUTION_NOT_CLEARED));
    CHECK(blocked(*K_RESULT, AssetRecipeBlocker::REVIEW_NOT_SELECTED));
    CHECK(K_RESULT->m_hashed_bytes == 2uz * K_RESULT->m_observed_bytes);
    REQUIRE(progress.size() == 6uz);
    for (std::size_t index { 0uz }; index < progress.size(); ++index) {
        CHECK(progress[index].m_pass == index / 3uz + 1uz);
        CHECK(progress[index].m_file_index == index % 3uz);
    }
    CHECK(tree(fixture.m_storage.m_root) == K_BEFORE);
    CHECK(!fixture.m_evidence.inspect(K_RECEIPTS));
    fixture.m_reviews.clear();
    const auto K_UNSELECTED = fixture.inspect(K_RECEIPTS);
    REQUIRE(K_UNSELECTED);
    CHECK(!K_UNSELECTED->m_models[0uz].m_review_selected);
    CHECK(blocked(*K_UNSELECTED, AssetRecipeBlocker::REVIEW_NOT_SELECTED));
}

TEST_CASE("external MODEL observations reject changed independent selections and context",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    ModelFixture fixture;
    const auto K_RECEIPTS = fixture.freeze();
    auto& model = fixture.m_models[0uz];
    auto& file = model.m_files[0uz];
    if (K_CASE == 0) {
        file.m_sha256 = std::string(64uz, '0');
    }
    if (K_CASE == 1) {
        ++file.m_bytes;
    }
    if (K_CASE == 2) {
        file.m_relative_path = "weights/MODEL.bin";
    }
    if (K_CASE == 3) {
        model.m_component_sha256 = std::string(64uz, '0');
    }
    if (K_CASE == 4) {
        fixture.m_models.push_back(model);
    }
    if (K_CASE == 5) {
        model.m_files.push_back(file);
    }
    if (K_CASE == 6) {
        fixture.m_reviews[0uz].m_review_sha256 = std::string(64uz, '0');
    }
    if (K_CASE == 7) {
        fixture.m_reviews[0uz].m_reviewer = "not.the.selected.reviewer";
    }
    if (K_CASE == 8) {
        fixture.m_reviews.push_back(fixture.m_reviews[0uz]);
    }
    if (K_CASE == 9) {
        fixture.m_reviews = { fixture.m_evidence.m_selected[0uz] };
    }
    if (K_CASE == 10) {
        ++fixture.m_evidence.m_recipe.m_context.m_world_epoch.m_value;
    }
    if (K_CASE == 11) {
        fixture.m_models.clear();
    }
    const auto K_BEFORE = tree(fixture.m_storage.m_root);
    CHECK(!fixture.inspect(K_RECEIPTS));
    CHECK(tree(fixture.m_storage.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL schemas budgets costs and portable paths fail closed",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    ModelFixture fixture;
    auto& manifest = fixture.m_evidence.m_manifests[2uz];
    if (K_CASE == 0) {
        manifest["schema_version"] = 1;
    }
    if (K_CASE == 1) {
        manifest["kind"] = "UNSUPPORTED";
    }
    if (K_CASE == 2) {
        manifest["unknown"] = true;
    }
    if (K_CASE == 3) {
        manifest["files"][0uz]["bytes"] = -1;
    }
    if (K_CASE == 4) {
        ++manifest["installed_bytes"].get_ref<Json::number_unsigned_t&>();
    }
    if (K_CASE == 5) {
        manifest["files"][0uz]["path"] = "../model";
    }
    if (K_CASE == 6) {
        manifest["files"][0uz]["path"] = "CON.bin";
    }
    if (K_CASE == 7) {
        manifest["files"][0uz]["path"] = "weights//model";
    }
    if (K_CASE == 8) {
        fixture.m_limits.m_file_bytes = 1;
    }
    if (K_CASE == 9) {
        fixture.m_limits.m_total_bytes = 0;
    }
    if (K_CASE == 10) {
        fixture.m_limits.m_file_bytes = 0;
    }
    if (K_CASE == 11) {
        fixture.m_limits = { 5uz * 1024uz * 1024uz * 1024uz, 6uz * 1024uz * 1024uz * 1024uz };
    }
    if (K_CASE == 12) {
        manifest["archive"] = { { "sha256", std::string(64uz, 'a') }, { "bytes", 1 } };
    }
    if (K_CASE == 13) {
        fixture.m_evidence.m_change_recipe = [](auto& value) {
            value["components"][2uz]["installed_bytes"] = nullptr;
        };
    }
    if (K_CASE == 14) {
        fixture.m_evidence.m_omitted = hash(fixture.m_evidence.m_terms);
    }
    if (K_CASE == 15) {
        fixture.m_evidence.m_terms.resize(64uz * 1024uz + 1uz, 'x');
    }
    if (K_CASE == 16) {
        manifest["files"][0uz]["path"] = 123;
    }
    if (K_CASE == 17) {
        manifest["files"][0uz]["bytes"] = true;
    }
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPTS);
    REQUIRE(!K_RESULT);
    if (K_CASE >= 16) {
        CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
    }
}

TEST_CASE("external MODEL file count bounds include every independently selected model",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    ModelFixture fixture;
    for (std::size_t model { 0uz }; model < 4uz; ++model) {
        const auto K_COUNT = model == 0uz ? 63uz : 64uz;
        for (std::size_t index { 0uz }; index < K_COUNT; ++index) {
            fixture.file(model,
                         "m" + std::to_string(model) + "/row" + std::to_string(index),
                         "Synthetic row");
        }
    }
    if (K_CASE == 1) {
        fixture.file(0uz, "excess", "Synthetic excess per model");
    }
    if (K_CASE == 2) {
        fixture.file(4uz, "excess", "Synthetic excess aggregate");
    }
    const auto K_RECEIPTS = fixture.freeze();
    fixture.m_limits = { 1024, 16384 };
    const auto K_RESULT = fixture.inspect(K_RECEIPTS);
    INFO((K_RESULT ? "Success" : K_RESULT.error().m_message));
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        REQUIRE(K_RESULT->m_models.size() == 4uz);
        for (const auto& model : K_RESULT->m_models) {
            CHECK(model.m_files.size() == 64uz);
        }
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code ==
              (K_CASE == 1 ? ErrorCode::INVALID_ARGUMENT : ErrorCode::RESOURCE_EXHAUSTED));
    }
}

TEST_CASE("external MODEL observation checks aggregate budgets and logical parent collisions",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    ModelFixture fixture;
    fixture.file(0uz, K_CASE == 1 ? "weights/model.bin/child" : "other", "more synthetic bytes");
    if (K_CASE == 0) {
        fixture.m_limits = { 40, 40 };
    }
    if (K_CASE == 2) {
        fixture.m_evidence.m_manifests[2uz]["files"][1uz]["path"] = "WEIGHTS/model.bin";
    }
    const auto K_RECEIPTS = fixture.freeze();
    CHECK(!fixture.inspect(K_RECEIPTS));
}

TEST_CASE("external MODEL observation rejects wrong native bytes and noncanonical paths",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    ModelFixture fixture;
    const auto K_RECEIPTS = fixture.freeze();
    auto& file = fixture.m_models[0uz].m_files[0uz];
    if (K_CASE == 0) {
        write(file.m_file, "wrong size");
    }
    if (K_CASE == 1) {
        write(file.m_file, std::string(static_cast<std::size_t>(file.m_bytes), 'x'));
    }
    if (K_CASE == 2) {
        fs::remove(file.m_file);
    }
    if (K_CASE == 3) {
        file.m_file = file.m_file.filename();
    }
    if (K_CASE == 4) {
        file.m_file = file.m_file.parent_path() / "blobs" / ".." / file.m_file.filename();
    }
    if (K_CASE == 5) {
        fs::remove(file.m_file);
        fs::create_directory(file.m_file);
    }
    if (K_CASE == 6) {
        auto text = file.m_file.native();
        text.push_back(fs::path::value_type {});
        text += fs::path("suffix").native();
        file.m_file = text;
    }
    const auto K_BEFORE = tree(fixture.m_storage.m_root);
    CHECK(!fixture.inspect(K_RECEIPTS));
    CHECK(tree(fixture.m_storage.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL observation rejects cross component aliases and native links",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    ModelFixture fixture;
    fixture.file(1uz, "other.bin", "Synthetic MODEL; not executable weights.");
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_FILE = fixture.m_models[0uz].m_files[0uz].m_file;
    auto& other = fixture.m_models[1uz].m_files[0uz].m_file;
    if (K_CASE == 0) {
        other = K_FILE;
    }
    if (K_CASE == 1) {
        fs::remove(other);
        fs::create_hard_link(K_FILE, other);
    }
    if (K_CASE >= 2) {
#ifdef _WIN32
        SKIP("Creating synthetic symlinks requires separately granted Windows privileges");
#else
        if (K_CASE == 2) {
            fs::remove(other);
            fs::create_symlink(K_FILE, other);
        }
        if (K_CASE == 3) {
            const auto K_ALIAS = fixture.m_storage.m_root / "alias";
            fs::create_directory_symlink(K_FILE.parent_path(), K_ALIAS);
            other = K_ALIAS / K_FILE.filename();
        }
#endif
    }
    CHECK(!fixture.inspect(K_RECEIPTS));
    CHECK(sha256_file(K_FILE, 1024).value() == fixture.m_models[0uz].m_files[0uz].m_sha256);
}

TEST_CASE("external MODEL observation detects cancellation callback faults and phase mutations",
          "[asset-external-models]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    ModelFixture fixture;
    fixture.file(0uz, "other.bin", "Other synthetic bytes");
    const auto K_RECEIPTS = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_storage.m_root);
    std::stop_source source;
    if (K_CASE == 0) {
        source.request_stop();
    }
    const auto K_RESULT = fixture.inspect(K_RECEIPTS, source.get_token(), [&](const auto& value) {
        if (K_CASE == 1) {
            source.request_stop();
        }
        if (K_CASE == 2) {
            throw std::runtime_error("Synthetic progress failure");
        }
        if ((K_CASE == 3 && value.m_pass == 1uz && value.m_file_index == 1uz) ||
            (K_CASE == 4 && value.m_pass == 2uz && value.m_file_index == 1uz)) {
            const auto K_FILE = fixture.m_models[0uz].m_files[0uz].m_file;
            fs::last_write_time(K_FILE, fs::last_write_time(K_FILE) + std::chrono::seconds(2));
        }
    });
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == (K_CASE <= 1   ? ErrorCode::CANCELLED
                                      : K_CASE == 2 ? ErrorCode::BACKEND_FAILURE
                                                    : ErrorCode::RESYNC_REQUIRED));
    CHECK(tree(fixture.m_storage.m_root) == K_BEFORE);
}

TEST_CASE("external MODEL streams a synthetic 156008466 byte checkpoint size",
          "[asset-external-models][asset-external-model-large]") {
    large_model(156008466, "da17ed8233dda77a0f80e391ac1288a7ec1532a88cc37cea146668183e553ac7");
}

TEST_CASE("external MODEL streams a synthetic doubled checkpoint size",
          "[asset-external-models][asset-external-model-large]") {
    large_model(312016932, "4507efd42e061bba8728d55a9593f34471e03a54b4b2cea937d0affde61f2d66");
}
