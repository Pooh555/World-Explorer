#include "world_explorer/application/gltf.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/gltf.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"

#include <chrono>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

SceneSnapshot fixture(bool unknown = false) {
    SceneChunk chunk;
    chunk.m_id = { 9 };
    chunk.m_pose_revision = { 1 };
    chunk.m_geometry_valid = true;
    chunk.m_source_frames = {{ 18446744073709551614ULL }};
    chunk.m_bounds = {{ -1, -1, 1 }, { 1, 1, 3 }};
    chunk.m_world_from_chunk = {{ 0, 1, 0, 0 }, { 1000, -2000, 3000 }, 2};
    chunk.m_mesh = SceneMesh {{{{ -.5F, -.5F, 2 }, { 0, 0, -1 }},
                               {{ .5F, -.5F, 2 }, { 0, 0, -1 }},
                               {{ -.5F, .5F, 2 }, { 0, 0, -1 }},
                               {{ .5F, .5F, 2 }, { 0, 0, -1 }},
                               {{ 0, 0, 2 }, { 0, 0, -1 }}},
                              {{ 0, 1, 2 }, { 1, 3, 2 }, { 0, 1, 2 }, { 1, 3, 2 }, { 0, 1, 2 }},
                              {},
                              {}};
    DepthImageAsset depth;
    auto& metadata = depth.m_metadata;
    metadata.m_dependency = {
        { 1 }, { 1 }, chunk.m_source_frames[0], { 2 }, std::string(64uz, 'a'), std::string(64uz, 'b')};
    metadata.m_width = metadata.m_height = 8;
    metadata.m_camera = { 4, 4, 3.5, 3.5 };
    metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
    metadata.m_recipe_sha256 = std::string(64uz, 'd');
    depth.m_axial_depth.assign(64uz, unknown ? 0.F : 2.F);
    depth.m_valid.assign(64uz, unknown ? 0 : 1);
    depth.m_support_count.assign(64uz, unknown ? 0 : 2);
    depth.m_confidence.assign(64uz, unknown ? 0.F : 1.F);
    std::vector<std::byte> rgb;
    for (std::size_t i { 0uz }; i < 64uz * 3uz; ++i) rgb.push_back(static_cast<std::byte>(i % 256uz));
    const auto K_VIEW = ColorProjectionView::create(
        std::move(depth), std::move(rgb), chunk.m_world_from_chunk, .01);
    REQUIRE(K_VIEW);
    auto baker = MeshTextureBaker::create(chunk);
    REQUIRE(baker);
    REQUIRE((*baker)->add_view(*K_VIEW, chunk.m_source_frames[0]));
    auto baked = (*baker)->finish();
    REQUIRE(baked);
    if (!chunk.m_mesh) throw std::runtime_error("Missing fixture mesh");
    chunk.m_mesh->m_baked = std::move(*baked);
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 17 };
    scene->m_epoch = { 1 };
    scene->m_revision = { 1 };
    scene->m_provenance = {{ "source_kind", "synthetic_diagnostic" }};
    scene->m_chunks[{ 9 }] = std::make_shared<SceneChunk>(std::move(chunk));
    return scene;
}

struct Directory {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_glb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Directory() { REQUIRE(std::filesystem::create_directory(m_root)); }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }
};

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value {};
    for (std::size_t i { 0uz }; i < 4uz; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8uz);
    return value;
}

void integer(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i { 0uz }; i < 4uz; ++i)
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8uz)) & 255U);
}

template <class Mutator>
std::vector<std::byte> mutate_document(const std::vector<std::byte>& bytes, Mutator mutate) {
    const auto K_LENGTH = word(bytes, 12uz);
    auto document =
        nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()) + 20,
                              reinterpret_cast<const char*>(bytes.data()) + 20uz + K_LENGTH);
    mutate(document);
    auto text = document.dump();
    while (text.size() % 4uz) text.push_back(' ');
    std::vector<std::byte> result(bytes.begin(), bytes.begin() + 12);
    result.resize(20uz);
    integer(result, 12uz, static_cast<std::uint32_t>(text.size()));
    integer(result, 16uz, 0x4e4f534aU);
    const auto K_TEXT = std::as_bytes(std::span(text));
    result.insert(result.end(), K_TEXT.begin(), K_TEXT.end());
    result.insert(result.end(), bytes.begin() + 20 + K_LENGTH, bytes.end());
    integer(result, 8uz, static_cast<std::uint32_t>(result.size()));
    return result;
}

}  // namespace

TEST_CASE("GLB keeps complete geometry source pins and exact half-pixel UVs with bounded RGB "
          "quantization",
          "[gltf]") {
    const auto K_SOURCE = fixture(GENERATE(false, true));
    const auto K_ORIGINAL_HASH = scene_content_hash(*K_SOURCE);
    const auto K_BYTES = encode_baked_glb(K_SOURCE);
    REQUIRE(K_BYTES);
    const auto K_LOADED = decode_baked_glb(*K_BYTES);
    REQUIRE(K_LOADED);
    REQUIRE(validate_scene(**K_LOADED));
    const auto& original = *K_SOURCE->m_chunks.at({ 9 });
    const auto& loaded = *(*K_LOADED)->m_chunks.at({ 9 });
    CHECK(scene_untextured_chunk_hash(original) == scene_untextured_chunk_hash(loaded));
    const auto K_BAKED = [](const SceneChunk& chunk) -> const BakedMeshAppearance& {
        if (!chunk.m_mesh || !chunk.m_mesh->m_baked)
            throw std::runtime_error("Missing baked test payload");
        return *chunk.m_mesh->m_baked;
    };
    const auto& before = K_BAKED(original);
    const auto& after = K_BAKED(loaded);
    CHECK(after.m_portable_parent_sha256 == scene_chunk_content_hash(original));
    CHECK(before.m_owners == after.m_owners);
    CHECK(before.m_candidates == after.m_candidates);
    CHECK(before.m_corner_uv == after.m_corner_uv);
    CHECK(before.m_rgba.size() == after.m_rgba.size());
    for (std::size_t i { 0uz }; i < before.m_rgba.size(); ++i) {
        CHECK(before.m_rgba[i][3] == after.m_rgba[i][3]);
        for (std::size_t c { 0uz }; c < 3uz; ++c)
            CHECK(std::abs(before.m_rgba[i][c] - after.m_rgba[i][c]) <= .0045F);
    }
    CHECK(scene_content_hash(*K_SOURCE) == K_ORIGINAL_HASH);
    Directory directory;
    REQUIRE(write_bundle(directory.m_root / "import", *K_LOADED));
    const auto K_REOPENED = load_bundle(directory.m_root / "import");
    REQUIRE(K_REOPENED);
    CHECK(scene_content_hash(**K_REOPENED) == scene_content_hash(**K_LOADED));
}

TEST_CASE("GLB rejects rehashed unsafe references layouts materials UVs and authoring changes",
          "[gltf]") {
    const auto K_BYTES = encode_baked_glb(fixture());
    REQUIRE(K_BYTES);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    auto changed = mutate_document(*K_BYTES, [&](auto& document) {
        if (K_CASE == 0) document["buffers"][0]["uri"] = "https://example.invalid/remote.bin";
        if (K_CASE == 1) document["images"][0]["uri"] = "../private.png";
        if (K_CASE == 2) document["accessors"][0]["count"] = 999999999;
        if (K_CASE == 3) document["bufferViews"][0]["byteOffset"] = 4;
        if (K_CASE == 4) document["materials"][0]["alphaCutoff"] = .5;
        if (K_CASE == 5) document["samplers"][0]["minFilter"] = 9987;
        if (K_CASE == 6) document["nodes"][0]["rotation"] = { 0, 0, 0, 1 };
        if (K_CASE == 7)
            document["nodes"][0]["extras"]["source_chunk_sha256"] = std::string(64uz, 'a');
        if (K_CASE == 8) document["nodes"][0]["extras"]["triangles"] = 0;
        if (K_CASE == 9) document["extras"]["source_images_added"] = true;
        if (K_CASE == 10) document["extensionsRequired"] = { "KHR_draco_mesh_compression" };
        if (K_CASE == 11) document["meshes"][0]["primitives"][0]["indices"] = 0;
        if (K_CASE == 12) document["nodes"][0]["extras"]["bounds"].push_back(0);
        if (K_CASE == 13) document["nodes"][0]["extras"]["rotation"].push_back(0);
        if (K_CASE == 14) document["nodes"][0]["extras"]["translation"].push_back(0);
    });
    CHECK(decode_baked_glb(changed).has_value() == false);
}

TEST_CASE("GLB rejects damaged embedded geometry PNG checksums ownership and truncation",
          "[gltf]") {
    const auto K_SOURCE = fixture();
    const auto K_BYTES = encode_baked_glb(K_SOURCE);
    REQUIRE(K_BYTES);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    auto bytes = *K_BYTES;
    const auto K_LENGTH = word(bytes, 12uz);
    const auto K_DOCUMENT =
        nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()) + 20,
                              reinterpret_cast<const char*>(bytes.data()) + 20uz + K_LENGTH);
    const auto K_START = 28uz + K_LENGTH;
    if (K_CASE == 0) bytes[K_START] ^= std::byte { 1 };
    if (K_CASE == 1) {
        const auto K_OFFSET = K_DOCUMENT["bufferViews"][3]["byteOffset"].get<std::size_t>();
        bytes[K_START + K_OFFSET + 42uz] ^= std::byte { 1 };
    }
    if (K_CASE == 2) {
        const auto K_OFFSET = K_DOCUMENT["bufferViews"][4]["byteOffset"].get<std::size_t>();
        bytes[K_START + K_OFFSET] ^= std::byte { 1 };
    }
    if (K_CASE == 3) bytes.pop_back();
    if (K_CASE == 4) bytes.push_back(std::byte {});
    if (K_CASE == 5) bytes[16] = std::byte {};
    CHECK(decode_baked_glb(bytes).has_value() == false);
    CHECK(scene_content_hash(*K_SOURCE) == scene_content_hash(*fixture()));
}

TEST_CASE("GLB cancellation and publication failures preserve inputs and previous outputs",
          "[gltf]") {
    const auto K_SOURCE = fixture();
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_ENCODED = encode_baked_glb(K_SOURCE, cancelled.get_token());
    REQUIRE(K_ENCODED.has_value() == false);
    CHECK(K_ENCODED.error().m_code == ErrorCode::CANCELLED);
    const auto K_BYTES = encode_baked_glb(K_SOURCE);
    REQUIRE(K_BYTES);
    const auto K_DECODED = decode_baked_glb(*K_BYTES, cancelled.get_token());
    REQUIRE(K_DECODED.has_value() == false);
    CHECK(K_DECODED.error().m_code == ErrorCode::CANCELLED);
    Directory directory;
    REQUIRE(write_bundle(directory.m_root / "source", K_SOURCE));
    REQUIRE(export_baked_glb_bundle({ directory.m_root / "source", directory.m_root / "export" }));
    CHECK(export_baked_glb_bundle({ directory.m_root / "source", directory.m_root / "export" })
              .has_value() == false);
    CHECK(export_baked_glb_bundle(
              { directory.m_root / "source", directory.m_root / "source" / "nested" })
              .has_value() == false);
    const auto K_SHA = sha256(*K_BYTES);
    REQUIRE(import_baked_glb(
        directory.m_root / "export" / "scene.glb", K_SHA, directory.m_root / "import"));
    CHECK(import_baked_glb(
              directory.m_root / "export" / "scene.glb", K_SHA, directory.m_root / "import")
              .has_value() == false);
    CHECK(import_baked_glb(directory.m_root / "export" / "scene.glb",
                           std::string(64uz, '0'),
                           directory.m_root / "bad")
              .has_value() == false);
    CHECK(std::filesystem::exists(directory.m_root / "bad") == false);
    const auto K_RETAINED = load_bundle(directory.m_root / "source");
    REQUIRE(K_RETAINED);
    CHECK(scene_content_hash(**K_RETAINED) == scene_content_hash(*K_SOURCE));
}

TEST_CASE("GLB refuses stale hybrid and partially untextured snapshots instead of dropping data",
          "[gltf]") {
    auto scene = std::make_shared<SceneState>(*fixture());
    auto chunk = std::make_shared<SceneChunk>(*scene->m_chunks.at({ 9 }));
    const auto K_CASE = GENERATE(0, 1, 2);
    if (K_CASE == 0) {
        chunk->m_geometry_valid = false;
        scene->m_chunks[{ 9 }] = chunk;
    }
    if (K_CASE == 1) {
        chunk->m_id = { 10 };
        if (!chunk->m_mesh) throw std::runtime_error("Missing test mesh");
        chunk->m_mesh->m_baked.reset();
        scene->m_chunks[{ 10 }] = chunk;
    }
    if (K_CASE == 2) {
        auto splat = std::make_shared<SceneChunk>();
        splat->m_id = { 10 };
        splat->m_bounds = {{ -1, -1, -1 }, { 1, 1, 1 }};
        splat->m_splats.push_back(Gaussian {});
        scene->m_chunks[{ 10 }] = splat;
    }
    REQUIRE(validate_scene(*scene));
    const auto K_BEFORE = scene_content_hash(*scene);
    CHECK(encode_baked_glb(scene).has_value() == false);
    CHECK(scene_content_hash(*scene) == K_BEFORE);
}

TEST_CASE("portable native admission binds filter parent and rounded sRGB8 storage", "[gltf]") {
    const auto K_BYTES = encode_baked_glb(fixture());
    REQUIRE(K_BYTES);
    const auto K_LOADED = decode_baked_glb(*K_BYTES);
    REQUIRE(K_LOADED);
    auto scene = std::make_shared<SceneState>(**K_LOADED);
    auto chunk = std::make_shared<SceneChunk>(*scene->m_chunks.at({ 9 }));
    if (!chunk->m_mesh || !chunk->m_mesh->m_baked)
        throw std::runtime_error("Missing portable test appearance");
    auto& appearance = *chunk->m_mesh->m_baked;
    const auto K_CASE = GENERATE(0, 1, 2);
    if (K_CASE == 0) appearance.m_rgba[0][0] = .5F;
    if (K_CASE == 1) appearance.m_portable_parent_sha256.clear();
    if (K_CASE == 2) appearance.m_portable_parent_sha256 = std::string(64uz, 'a');
    scene->m_chunks[{ 9 }] = chunk;
    CHECK(validate_scene(*scene).has_value() == false);
    CHECK(encode_baked_glb(*K_LOADED).has_value() == false);
    Directory directory;
    REQUIRE(write_bundle(directory.m_root / "retained", *K_LOADED));
    CHECK(write_bundle(directory.m_root / "retained", scene).has_value() == false);
    const auto K_RETAINED = load_bundle(directory.m_root / "retained");
    REQUIRE(K_RETAINED);
    CHECK(scene_content_hash(**K_RETAINED) == scene_content_hash(**K_LOADED));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = export_baked_glb_bundle(
        { directory.m_root / "retained", directory.m_root / "cancelled" }, cancelled.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(std::filesystem::exists(directory.m_root / "cancelled") == false);
}
