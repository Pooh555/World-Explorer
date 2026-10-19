#include "world_explorer/application/mesh_texture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"

#include <bit>
#include <chrono>
#include <fstream>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

SceneChunk chunk_fixture(bool quad = false) {
    SceneChunk chunk;
    chunk.m_id = { 1 };
    chunk.m_pose_revision = { 1 };
    chunk.m_geometry_valid = true;
    chunk.m_source_frames = {{ 3 }, { 4 }};
    chunk.m_bounds = {{ -1, -1, 1 }, { 1, 1, 3 }};
    chunk.m_mesh = SceneMesh {
        {{{ -.5F, -.5F, 2 }, { 0, 0, -1 }}, {{ .5F, -.5F, 2 }, { 0, 0, -1 }}, {{ -.5F, .5F, 2 }, { 0, 0, -1 }}},
        {{ 0, 1, 2 }},
        {}};
    if (quad) {
        chunk.m_mesh->m_vertices.push_back({{ .5F, .5F, 2 }, { 0, 0, -1 }});
        chunk.m_mesh->m_triangles.push_back({ 1, 3, 2 });
    }
    return chunk;
}

ColorProjectionView view_fixture(std::array<unsigned char, 3> color,
                                 bool unknown = false,
                                 bool gradient = false,
                                 FrameId frame = { 3 }) {
    DepthImageAsset depth;
    auto& metadata = depth.m_metadata;
    metadata.m_dependency = {{ 1 }, { 1 }, { 3 }, { 2 }, std::string(64uz, 'a'), std::string(64uz, 'b')};
    metadata.m_dependency.m_frame = frame;
    metadata.m_width = metadata.m_height = 8;
    metadata.m_camera = { 4, 4, 3.5, 3.5 };
    metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
    metadata.m_recipe_sha256 = std::string(64uz, 'd');
    depth.m_axial_depth.assign(64uz, unknown ? 0.0F : 2.0F);
    depth.m_valid.assign(64uz, unknown ? 0 : 1);
    depth.m_confidence.assign(64uz, unknown ? 0.0F : 1.0F);
    depth.m_support_count.assign(64uz, unknown ? 0 : 2);
    std::vector<std::byte> rgb;
    for (std::size_t i { 0uz }; i < 64uz; ++i)
        for (auto channel : color)
            rgb.push_back(static_cast<std::byte>(gradient ? (i * 3uz + channel) % 256uz : channel));
    auto result = ColorProjectionView::create(std::move(depth), std::move(rgb), {}, .01);
    if (!result) throw std::runtime_error(result.error().m_message);
    return std::move(*result);
}

template <class Chunk> decltype(auto) baked_of(Chunk& chunk) {
    if (!chunk.m_mesh || !chunk.m_mesh->m_baked)
        throw std::runtime_error("Missing texture fixture");
    return *chunk.m_mesh->m_baked;
}

SceneChunk
baked_chunk(bool quad = false, bool unknown = false, bool gradient = false, ChunkId id = { 1 }) {
    auto chunk = chunk_fixture(quad);
    chunk.m_id = id;
    auto baker = MeshTextureBaker::create(chunk);
    if (!baker) throw std::runtime_error(baker.error().m_message);
    const auto K_RED = view_fixture({ 255, 0, 0 }, unknown, gradient),
               K_BLUE = view_fixture({ 0, 0, 255 }, unknown, gradient, { 4 });
    REQUIRE((*baker)->add_view(K_RED, { 3 }));
    REQUIRE((*baker)->add_view(K_BLUE, { 4 }));
    auto result = (*baker)->finish();
    if (!result) throw std::runtime_error(result.error().m_message);
    if (!chunk.m_mesh) throw std::runtime_error("Missing mesh fixture");
    chunk.m_mesh->m_baked = std::move(*result);
    return chunk;
}

SceneSnapshot scene_fixture(SceneChunk chunk) {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 7 };
    scene->m_epoch = { 1 };
    scene->m_revision = { 1 };
    scene->m_provenance = {{ "source_kind", "synthetic_diagnostic" }};
    const auto K_ID = chunk.m_id;
    scene->m_chunks[K_ID] = std::make_shared<SceneChunk>(std::move(chunk));
    return scene;
}

struct Directory {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_mesh_texture_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Directory() { REQUIRE(std::filesystem::create_directory(m_root)); }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }
};

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    REQUIRE(file);
}

std::string digest(const std::string& bytes) {
    return sha256(std::as_bytes(std::span(bytes)));
}

}  // namespace

TEST_CASE("texture baking blends in linear light and preserves geometry and unknown texels",
          "[mesh_texture]") {
    const auto K_UNKNOWN = GENERATE(false, true);
    auto chunk = baked_chunk(false, K_UNKNOWN);
    REQUIRE(validate_scene(*scene_fixture(chunk)));
    const auto& baked = baked_of(chunk);
    CHECK(baked.m_width == 8);
    CHECK(baked.m_height == 8);
    CHECK(scene_untextured_chunk_hash(chunk) == scene_chunk_content_hash(chunk_fixture()));
    CHECK(scene_chunk_content_hash(chunk) != scene_chunk_content_hash(chunk_fixture()));
    for (std::size_t y { 0uz }; y < 8uz; ++y)
        for (std::size_t x { 0uz }; x < 8uz; ++x) {
            const auto K_I = y * 8uz + x;
            if (x + y < 8uz && !K_UNKNOWN) {
                CHECK(baked.m_rgba[K_I] == std::array<float, 4> { .5F, 0, .5F, 1 });
                CHECK(baked.m_owners[K_I] == FrameId { 3 });
                CHECK(baked.m_candidates[K_I] == 3);
            } else {
                CHECK(baked.m_rgba[K_I] == std::array<float, 4> {});
                CHECK(baked.m_owners[K_I] == FrameId {});
                CHECK(baked.m_candidates[K_I] == 0);
            }
        }
}

TEST_CASE("texture border lattice shares exact observations on adjacent faces", "[mesh_texture]") {
    const auto K_CHUNK = baked_chunk(true, false, true);
    const auto& baked = baked_of(K_CHUNK);
    for (std::size_t i { 0uz }; i < 8uz; ++i) {
        const auto K_A = i * 16uz + 7uz - i, K_B = i * 16uz + 8uz;
        CHECK(baked.m_rgba[K_A] == baked.m_rgba[K_B]);
        CHECK(baked.m_candidates[K_A] == baked.m_candidates[K_B]);
    }
}

TEST_CASE("texture bake refuses incomplete duplicate cancelled and excessive input",
          "[mesh_texture]") {
    auto baker = MeshTextureBaker::create(chunk_fixture());
    REQUIRE(baker);
    CHECK((*baker)->finish().has_value() == false);
    const auto K_VIEW = view_fixture({ 255, 0, 0 });
    CHECK((*baker)->add_view(K_VIEW, { 4 }).has_value() == false);
    REQUIRE((*baker)->add_view(K_VIEW, { 3 }));
    CHECK((*baker)->add_view(K_VIEW, { 3 }).has_value() == false);
    const auto K_SECOND = view_fixture({ 0, 0, 255 }, false, false, { 4 });
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = (*baker)->add_view(K_SECOND, { 4 }, cancelled.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK((*baker)->add_view(K_SECOND, { 4 }).has_value() == false);
    CHECK((*baker)->finish().has_value() == false);
    auto excessive = chunk_fixture();
    if (!excessive.m_mesh) throw std::runtime_error("Missing mesh");
    excessive.m_mesh->m_triangles.resize(100001uz, { 0, 1, 2 });
    const auto K_EXCESSIVE = MeshTextureBaker::create(std::move(excessive));
    CHECK(K_EXCESSIVE.has_value() == false);
}

TEST_CASE("texture admission rejects malformed planes masks UV and provenance", "[mesh_texture]") {
    auto chunk = baked_chunk();
    auto& baked = baked_of(chunk);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    if (K_MUTATION == 0) baked.m_rgba[0][0] = std::numeric_limits<float>::quiet_NaN();
    if (K_MUTATION == 1) baked.m_rgba[0][3] = .5F;
    if (K_MUTATION == 2) baked.m_rgba[0][3] = 0;
    if (K_MUTATION == 3) baked.m_rgba[63] = { 1, 0, 0, 1 };
    if (K_MUTATION == 4) baked.m_owners[0] = { 9 };
    if (K_MUTATION == 5) baked.m_candidates[0] = 4;
    if (K_MUTATION == 6) baked.m_candidates[0] = 0;
    if (K_MUTATION == 7) baked.m_width = 0;
    if (K_MUTATION == 8) baked.m_rgba.pop_back();
    if (K_MUTATION == 9) baked.m_corner_uv[0][0] = std::numeric_limits<float>::quiet_NaN();
    if (K_MUTATION == 10) baked.m_source_chunk_sha256 = std::string(64uz, 'a');
    if (K_MUTATION == 11) baked.m_manifest_sha256 = std::string(64uz, 'a');
    if (K_MUTATION == 12) baked.m_frames[1] = { 3 };
    if (K_MUTATION == 13) baked.m_owners[0] = {};
    const auto K_REJECTED = validate_scene(*scene_fixture(std::move(chunk)));
    CHECK(K_REJECTED.has_value() == false);
}

TEST_CASE("texture budgets include metadata UV and aggregate atlases", "[mesh_texture]") {
    const auto K_SCENE = scene_fixture(baked_chunk());
    const auto K_BYTES = validate_scene(*K_SCENE);
    REQUIRE(K_BYTES);
    SceneLimits limits;
    limits.m_max_texture_texels = 64uz;
    limits.m_max_payload_bytes = *K_BYTES;
    REQUIRE(validate_scene(*K_SCENE, limits));
    --limits.m_max_payload_bytes;
    CHECK(validate_scene(*K_SCENE, limits).has_value() == false);
    auto scene = std::make_shared<SceneState>(*K_SCENE);
    auto second = baked_chunk(false, false, false, { 2 });
    scene->m_chunks[{ 2 }] = std::make_shared<SceneChunk>(std::move(second));
    limits = SceneLimits {};
    limits.m_max_texture_texels = 127uz;
    CHECK(validate_scene(*scene, limits).has_value() == false);
    limits.m_max_texture_texels = 128uz;
    CHECK(validate_scene(*scene, limits).has_value());
}

TEST_CASE("baked bundles round trip exactly while raw mesh bundles retain schema two",
          "[mesh_texture][bundle]") {
    Directory directory;
    const auto K_SCENE = scene_fixture(baked_chunk(true));
    REQUIRE(write_bundle(directory.m_root / "baked", K_SCENE));
    const auto K_LOADED = load_bundle(directory.m_root / "baked");
    REQUIRE(K_LOADED);
    CHECK(scene_content_hash(**K_LOADED) == scene_content_hash(*K_SCENE));
    auto manifest = nlohmann::json::parse(read_bytes(directory.m_root / "baked" / "manifest.json"));
    CHECK(manifest["schema_version"] == 3);
    REQUIRE(write_bundle(directory.m_root / "raw", scene_fixture(chunk_fixture())));
    CHECK(nlohmann::json::parse(
              read_bytes(directory.m_root / "raw" / "manifest.json"))["schema_version"] == 2);
    manifest["schema_version"] = 2;
    write_bytes(directory.m_root / "baked" / "manifest.json", manifest.dump());
    CHECK(load_bundle(directory.m_root / "baked").has_value() == false);
}

TEST_CASE("rehashed baked payloads reject malformed counts and colors without mutating readers",
          "[mesh_texture][bundle]") {
    Directory directory;
    const auto K_SCENE = scene_fixture(baked_chunk());
    REQUIRE(write_bundle(directory.m_root, K_SCENE));
    const auto K_RETAINED = load_bundle(directory.m_root);
    REQUIRE(K_RETAINED);
    auto manifest = nlohmann::json::parse(read_bytes(directory.m_root / "manifest.json"));
    auto index = read_bytes(directory.m_root / manifest.at("index").get<std::string>());
    const auto K_NAME = index.substr(100uz);
    auto bytes = read_bytes(directory.m_root / K_NAME);
    const auto K_MATERIAL = bytes.find("WEXMAT01");
    REQUIRE(K_MATERIAL != bytes.npos);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    if (K_MUTATION == 0) bytes[K_MATERIAL + 8uz] = 0;
    if (K_MUTATION == 1)
        for (std::size_t i { 0uz }; i < 4uz; ++i)
            bytes[K_MATERIAL + 16uz + i] = static_cast<char>(255);
    if (K_MUTATION == 2) bytes.pop_back();
    if (K_MUTATION == 3) bytes.push_back('\0');
    if (K_MUTATION == 4) bytes[K_MATERIAL] = 'X';
    if (K_MUTATION >= 5) {
        auto position = K_MATERIAL + 20uz + 16uz;
        for (std::size_t field { 0uz }; field < 3uz; ++field) {
            std::size_t length {};
            for (std::size_t i { 0uz }; i < 4uz; ++i)
                length |= static_cast<std::size_t>(static_cast<unsigned char>(bytes[position + i]))
                          << (i * 8uz);
            position += 4uz + length;
        }
        const auto K_UV = position, K_RGBA = K_UV + 24uz;
        const auto K_TARGET = K_MUTATION == 5   ? K_UV
                              : K_MUTATION == 6 ? K_RGBA
                              : K_MUTATION == 7 ? K_RGBA + 12uz
                                                : K_RGBA + 64uz * 24uz;
        const std::uint32_t K_VALUE {K_MUTATION <= 6   ? 0x7fc00000U
                                     : K_MUTATION == 7 ? 0x3f000000U
                                                       : 4U};
        for (std::size_t i { 0uz }; i < 4uz; ++i)
            bytes[K_TARGET + i] = static_cast<char>((K_VALUE >> (i * 8uz)) & 255U);
    }
    const auto K_HASH = digest(bytes), K_CHUNK = "chunk_" + K_HASH + ".bin";
    write_bytes(directory.m_root / K_CHUNK, bytes);
    for (std::size_t i { 0uz }; i < 8uz; ++i)
        index[24uz + i] = static_cast<char>((bytes.size() >> (i * 8uz)) & 255U);
    index.replace(32uz, 64uz, K_HASH);
    index.replace(100uz, K_NAME.size(), K_CHUNK);
    const auto K_INDEX_HASH = digest(index), K_INDEX_NAME = "index_" + K_INDEX_HASH + ".bin";
    write_bytes(directory.m_root / K_INDEX_NAME, index);
    manifest["index"] = K_INDEX_NAME;
    manifest["index_sha256"] = K_INDEX_HASH;
    manifest["index_bytes"] = index.size();
    write_bytes(directory.m_root / "manifest.json", manifest.dump());
    CHECK(load_bundle(directory.m_root).has_value() == false);
    CHECK(scene_content_hash(**K_RETAINED) == scene_content_hash(*K_SCENE));
    CHECK(read_bytes(directory.m_root / "manifest.json") == manifest.dump());
}

TEST_CASE("rehashed appearance metadata cannot relabel observations or change coverage",
          "[mesh_texture]") {
    auto chunk = baked_chunk();
    auto& baked = baked_of(chunk);
    auto document = nlohmann::json::parse(baked.m_manifest_json);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4);
    if (K_MUTATION == 0) document["lighting"] = "relightable_albedo";
    if (K_MUTATION == 1) document["known_texels"] = 35;
    if (K_MUTATION == 2) document["tile_size"] = 8.0;
    if (K_MUTATION == 3) document["source_chunk_sha256"] = std::string(64uz, 'a');
    baked.m_manifest_json = document.dump();
    if (K_MUTATION == 4) baked.m_manifest_json.insert(1uz, "\"kind\":\"CAPTURED_UNLIT_ATLAS_V1\",");
    baked.m_manifest_sha256 = digest(baked.m_manifest_json);
    CHECK(validate_scene(*scene_fixture(chunk)).has_value() == false);
    Directory directory;
    const auto K_VALID = scene_fixture(baked_chunk());
    REQUIRE(write_bundle(directory.m_root, K_VALID));
    const auto K_MANIFEST = read_bytes(directory.m_root / "manifest.json");
    CHECK(write_bundle(directory.m_root, scene_fixture(chunk)).has_value() == false);
    CHECK(read_bytes(directory.m_root / "manifest.json") == K_MANIFEST);
    const auto K_RETAINED = load_bundle(directory.m_root);
    REQUIRE(K_RETAINED);
    CHECK(scene_content_hash(**K_RETAINED) == scene_content_hash(*K_VALID));
}

TEST_CASE("bake command rejects cancelled malformed and existing destinations before output",
          "[mesh_texture]") {
    Directory directory;
    const auto K_REQUEST = directory.m_root / "request.json";
    write_bytes(K_REQUEST, "{}");
    const auto K_SHA = digest("{}");
    const auto K_OUTPUT = directory.m_root / "output";
    CHECK(bake_mesh_texture(K_REQUEST, K_SHA, K_OUTPUT).has_value() == false);
    CHECK(std::filesystem::exists(K_OUTPUT) == false);
    CHECK(bake_mesh_texture(K_REQUEST, std::string(64uz, 'a'), K_OUTPUT).has_value() == false);
    CHECK(bake_mesh_texture(K_REQUEST, K_SHA, directory.m_root).has_value() == false);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_CANCELLED =
        bake_mesh_texture(K_REQUEST, K_SHA, K_OUTPUT, cancellation.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(read_bytes(K_REQUEST) == "{}");
    CHECK(std::filesystem::exists(K_OUTPUT) == false);
}
