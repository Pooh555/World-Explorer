#include "world_explorer/application/geometry_job.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <map>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

std::string save(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot save test document");
    }
    return sha256(std::as_bytes(std::span(text)));
}

struct Fixture {
    std::filesystem::path m_root {
        std::filesystem::temp_directory_path() /
        ("we-mesh-job-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) };
    std::filesystem::path m_request, m_job;
    Json m_configuration;
    std::string m_pin;

    Fixture() {
        std::filesystem::create_directory(m_root);
        m_root = std::filesystem::canonical(m_root);
        m_request = m_root / "request.json";
        m_job = m_root / "job";
        SceneChunk chunk;
        chunk.m_id = { 1 };
        chunk.m_pose_revision = { 1 };
        chunk.m_geometry_valid = true;
        chunk.m_source_frames = { { 9007199254740993ULL } };
        chunk.m_bounds = { { -.5, -.5, 2 }, { .5, .5, 2 } };
        chunk.m_mesh = SceneMesh { { { { -.5F, -.5F, 2 }, { 0, 0, -1 } },
                                   { { .5F, -.5F, 2 }, { 0, 0, -1 } },
                                   { { -.5F, .5F, 2 }, { 0, 0, -1 } } },
                                  { { 0, 1, 2 } },
                                  {},
                                  {} };
        auto source = std::make_shared<SceneState>();
        source->m_id = { 1 };
        source->m_revision = { 1 };
        source->m_provenance = { { "source_kind", "synthetic_diagnostic" } };
        source->m_chunks[chunk.m_id] = std::make_shared<SceneChunk>(chunk);
        REQUIRE(write_bundle(m_root / "raw", source));
        auto baker = MeshTextureBaker::create(chunk);
        REQUIRE(baker);
        DepthImageAsset depth;
        depth.m_metadata.m_dependency = { { 0 },
                                         { 1 },
                                         chunk.m_source_frames[0],
                                         { 1 },
                                         std::string(64uz, 'a'),
                                         std::string(64uz, 'b') };
        depth.m_metadata.m_width = depth.m_metadata.m_height = 8;
        depth.m_metadata.m_camera = { 4, 4, 3.5, 3.5 };
        depth.m_metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
        depth.m_metadata.m_recipe_sha256 = std::string(64uz, 'd');
        depth.m_axial_depth.assign(64uz, 2.F);
        depth.m_valid.assign(64uz, 1);
        depth.m_support_count.assign(64uz, 2);
        depth.m_confidence.assign(64uz, 1.F);
        const auto K_VIEW = ColorProjectionView::create(
            std::move(depth), std::vector<std::byte>(192uz, std::byte { 127 }), {}, .01);
        REQUIRE(K_VIEW);
        REQUIRE((*baker)->add_view(*K_VIEW, chunk.m_source_frames[0]));
        auto material = (*baker)->finish();
        REQUIRE(material);
        if (!chunk.m_mesh) {
            throw std::runtime_error("Missing test mesh");
        }
        chunk.m_mesh->m_baked = std::move(*material);
        source->m_chunks[chunk.m_id] = std::make_shared<SceneChunk>(chunk);
        REQUIRE(write_bundle(m_root / "baked", source));
        m_configuration = {
            { "schema_version", 1U },
            { "kind", "MESH_POSTPROCESS_PIPELINE_V1" },
            { "source_bundle", "raw" },
            { "source_manifest_sha256", pin("raw/manifest.json") },
            { "cleanup_request", nullptr },
            { "bake_request", nullptr },
            { "reuse_baked_bundle", { { "path", "baked" }, { "sha256", pin("baked/manifest.json") } } },
            { "export_glb", true } };
        publish();
    }

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    std::string pin(const std::filesystem::path& relative) const {
        const auto K_PIN = sha256_file(m_root / relative, 16uz * 1024uz * 1024uz);
        if (!K_PIN) {
            throw std::runtime_error(K_PIN.error().m_message);
        }
        return *K_PIN;
    }

    void publish() {
        m_pin = save(m_request, m_configuration.dump(2));
    }

    Result<std::filesystem::path> run(const std::stop_token& stop = {},
                                      const LogCallback& log = {}) const {
        return run_geometry_job(m_request, m_pin, m_job, stop, log);
    }

    std::map<std::string, std::string> snapshot() const {
        std::map<std::string, std::string> result;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_root)) {
            if (entry.is_regular_file()) {
                const auto K_RELATIVE = entry.path().lexically_relative(m_root);
                result.emplace(K_RELATIVE.generic_string(), pin(K_RELATIVE));
            }
        }
        return result;
    }
};

}  // namespace

TEST_CASE("owned mesh stages preserve raw input reuse exact bakes and replay complete handoffs",
          "[mesh_job]") {
    Fixture fixture;
    const auto K_SOURCE = fixture.pin("raw/manifest.json");
    const auto K_RESULT = fixture.run();
    INFO((K_RESULT ? "ok" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    const auto K_SCENE = load_bundle(*K_RESULT);
    REQUIRE(K_SCENE);
    const auto K_BAKED = load_bundle(fixture.m_root / "baked");
    REQUIRE(K_BAKED);
    CHECK(scene_content_hash(**K_SCENE) == scene_content_hash(**K_BAKED));
    CHECK(std::filesystem::exists(fixture.m_job / "stages/export-0/output/scene.glb"));
    const auto K_BEFORE = fixture.snapshot();
    REQUIRE(fixture.run());
    CHECK(fixture.snapshot() == K_BEFORE);
    CHECK(fixture.pin("raw/manifest.json") == K_SOURCE);
}

TEST_CASE("owned mesh cancellation retains completed stages and reserves fresh attempts on resume",
          "[mesh_job]") {
    Fixture fixture;
    const auto K_BOUNDARY = GENERATE("Starting mesh stage: native\n",
                                     "Completed mesh stage: native\n",
                                     "Starting mesh stage: bake\n",
                                     "Completed mesh stage: bake\n",
                                     "Starting mesh stage: export\n",
                                     "Completed mesh stage: export\n");
    std::stop_source cancellation;
    const auto K_RESULT = fixture.run(cancellation.get_token(), [&](const std::string& text) {
        if (text == K_BOUNDARY) {
            cancellation.request_stop();
        }
    });
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
    CHECK_FALSE(std::filesystem::exists(fixture.m_job / "manifest.json"));
    const auto K_BEFORE = fixture.snapshot();
    REQUIRE(fixture.run());
    const auto K_AFTER = fixture.snapshot();
    for (const auto& [K_PATH, K_PIN] : K_BEFORE) {
        CHECK(K_AFTER.at(K_PATH) == K_PIN);
    }
    const auto K_COMPLETED = fixture.snapshot();
    REQUIRE(fixture.run());
    CHECK(fixture.snapshot() == K_COMPLETED);
}

TEST_CASE("owned mesh rejects changed selections and corrupted terminal stages without repair",
          "[mesh_job]") {
    Fixture fixture;
    REQUIRE(fixture.run());
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    if (K_CASE == 0) {
        fixture.m_configuration["export_glb"] = false;
        fixture.publish();
    }
    if (K_CASE == 1) {
        save(fixture.m_job / "stages/bake-0/output/manifest.json", "{}");
    }
    if (K_CASE == 2) {
        save(fixture.m_job / "bake.json", "{}");
    }
    if (K_CASE == 3) {
        save(fixture.m_job / "manifest.json", "{}");
    }
    if (K_CASE == 4) {
        std::filesystem::remove(fixture.m_job / "bake.json");
    }
    if (K_CASE == 5) {
        save(fixture.m_root / "baked/manifest.json", "{}");
    }
    const auto K_BEFORE = fixture.snapshot();
    CHECK_FALSE(fixture.run());
    CHECK(fixture.snapshot() == K_BEFORE);
}

TEST_CASE("owned mesh producer failures retain prior outputs and refuse unsafe partial attempts",
          "[mesh_job]") {
    Fixture fixture;
    const auto K_RESULT = fixture.run({}, [](const std::string& text) {
        if (text == "Starting mesh stage: bake\n") {
            throw std::runtime_error("injected stage failure");
        }
    });
    REQUIRE_FALSE(K_RESULT);
    CHECK(std::filesystem::exists(fixture.m_job / "native.json"));
    CHECK(std::filesystem::is_directory(fixture.m_job / "stages/bake-0"));
    CHECK_FALSE(std::filesystem::exists(fixture.m_job / "manifest.json"));
    const auto K_UNSAFE = GENERATE(false, true);
    if (K_UNSAFE) {
        std::filesystem::create_directory(fixture.m_job / "stages/bake-0/unknown");
        const auto K_BEFORE = fixture.snapshot();
        CHECK_FALSE(fixture.run());
        CHECK(fixture.snapshot() == K_BEFORE);
    } else {
        const auto K_BEFORE = fixture.snapshot();
        REQUIRE(fixture.run());
        const auto K_AFTER = fixture.snapshot();
        for (const auto& [K_PATH, K_PIN] : K_BEFORE) {
            CHECK(K_AFTER.at(K_PATH) == K_PIN);
        }
        CHECK(std::filesystem::exists(fixture.m_job / "stages/bake-1/output/manifest.json"));
    }
}

TEST_CASE("owned mesh rejects ambiguous plans inputs and overlap before ownership", "[mesh_job]") {
    Fixture fixture;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    if (K_CASE == 0) {
        fixture.m_configuration["source_manifest_sha256"] = std::string(64uz, '0');
    }
    if (K_CASE == 1) {
        fixture.m_configuration["export_glb"] = "yes";
    }
    if (K_CASE == 2) {
        fixture.m_configuration["schema_version"] = 1.0;
    }
    if (K_CASE == 3) {
        fixture.m_configuration["unknown"] = false;
    }
    if (K_CASE == 4) {
        fixture.m_configuration["reuse_baked_bundle"] = nullptr;
    }
    if (K_CASE == 5) {
        fixture.m_job = fixture.m_root / "raw/nested";
    }
    if (K_CASE == 6) {
        fixture.m_configuration["cleanup_request"] = { { "path", "request.json" },
                                                      { "sha256", std::string(64uz, 'a') } };
    }
    if (K_CASE == 7) {
        fixture.m_configuration["reuse_baked_bundle"]["extra"] = true;
    }
    fixture.publish();
    if (K_CASE == 8) {
        fixture.m_pin = save(fixture.m_request,
                             fixture.m_configuration.dump().insert(1uz, "\"kind\":\"ambiguous\","));
    }
    const auto K_BEFORE = fixture.snapshot();
    CHECK_FALSE(fixture.run());
    CHECK_FALSE(std::filesystem::exists(fixture.m_job));
    CHECK(fixture.snapshot() == K_BEFORE);
}

TEST_CASE("owned mesh can export an original baked source or retain raw geometry without baking",
          "[mesh_job]") {
    Fixture fixture;
    const auto K_BAKED = GENERATE(false, true);
    fixture.m_configuration["source_bundle"] = K_BAKED ? "baked" : "raw";
    fixture.m_configuration["source_manifest_sha256"] =
        fixture.pin(K_BAKED ? "baked/manifest.json" : "raw/manifest.json");
    fixture.m_configuration["reuse_baked_bundle"] = nullptr;
    fixture.m_configuration["export_glb"] = K_BAKED;
    fixture.publish();
    const auto K_RESULT = fixture.run();
    REQUIRE(K_RESULT);
    CHECK_FALSE(std::filesystem::exists(fixture.m_job / "bake.json"));
    CHECK(std::filesystem::exists(fixture.m_job / "export.json") == K_BAKED);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_BEFORE = fixture.snapshot();
    const auto K_CANCELLED = fixture.run(cancellation.get_token());
    REQUIRE_FALSE(K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(fixture.snapshot() == K_BEFORE);
}
