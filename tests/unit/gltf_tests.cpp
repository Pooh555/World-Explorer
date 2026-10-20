#include "world_explorer/application/gltf.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/gltf.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>

using namespace world_explorer;

namespace {

SceneSnapshot
fixture(bool unknown = false, std::size_t sequence_count = 0uz, ChunkId chunk_id = { 9 }) {
    SceneChunk chunk;
    chunk.m_id = chunk_id;
    chunk.m_pose_revision = { 1 };
    chunk.m_geometry_valid = true;
    chunk.m_source_frames = { { 18446744073709551614ULL } };
    if (sequence_count) {
        chunk.m_source_frames.clear();
        for (std::size_t i { 0uz }; i < sequence_count; ++i) {
            chunk.m_source_frames.push_back({ 18446744073709551614ULL - i });
        }
    }
    chunk.m_bounds = { { -1, -1, 1 }, { 1, 1, 3 } };
    chunk.m_world_from_chunk = { { 0, 1, 0, 0 }, { 1000, -2000, 3000 }, 2 };
    chunk.m_mesh = SceneMesh { { { { -.5F, -.5F, 2 }, { 0, 0, -1 } },
                               { { .5F, -.5F, 2 }, { 0, 0, -1 } },
                               { { -.5F, .5F, 2 }, { 0, 0, -1 } },
                               { { .5F, .5F, 2 }, { 0, 0, -1 } },
                               { { 0, 0, 2 }, { 0, 0, -1 } } },
                              { { 0, 1, 2 }, { 1, 3, 2 }, { 0, 1, 2 }, { 1, 3, 2 }, { 0, 1, 2 } },
                              {},
                              {} };
    DepthImageAsset depth;
    auto& metadata = depth.m_metadata;
    metadata.m_dependency = {
        { 1 }, { 1 }, chunk.m_source_frames[0], { 2 }, std::string(64uz, 'a'), std::string(64uz, 'b') };
    metadata.m_width = metadata.m_height = 8;
    metadata.m_camera = { 4, 4, 3.5, 3.5 };
    metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
    metadata.m_recipe_sha256 = std::string(64uz, 'd');
    depth.m_axial_depth.assign(64uz, unknown ? 0.F : 2.F);
    depth.m_valid.assign(64uz, unknown ? 0 : 1);
    depth.m_support_count.assign(64uz, unknown ? 0 : 2);
    depth.m_confidence.assign(64uz, unknown ? 0.F : 1.F);
    std::vector<std::byte> rgb;
    for (std::size_t i { 0uz }; i < 64uz * 3uz; ++i) {
        rgb.push_back(static_cast<std::byte>(i % 256uz));
    }
    auto baker = MeshTextureBaker::create(chunk,
                                          sequence_count ? MeshTextureProfile::DEPTH_SEQUENCE
                                                         : MeshTextureProfile::SINGLE_CACHE);
    REQUIRE(baker);
    for (auto frame : chunk.m_source_frames) {
        depth.m_metadata.m_dependency.m_frame = frame;
        const auto K_VIEW = ColorProjectionView::create(depth, rgb, chunk.m_world_from_chunk, .01);
        REQUIRE(K_VIEW);
        REQUIRE((*baker)->add_view(*K_VIEW, frame));
    }
    auto baked = (*baker)->finish();
    REQUIRE(baked);
    if (!chunk.m_mesh) {
        throw std::runtime_error("Missing fixture mesh");
    }
    chunk.m_mesh->m_baked = std::move(*baked);
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 17 };
    scene->m_epoch = { 1 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" } };
    scene->m_chunks[chunk_id] = std::make_shared<SceneChunk>(std::move(chunk));
    return scene;
}

const BakedMeshAppearance& baked_of(const SceneChunk& chunk) {
    if (!chunk.m_mesh || !chunk.m_mesh->m_baked) {
        throw std::runtime_error("Missing baked test payload");
    }
    return *chunk.m_mesh->m_baked;
}

struct Directory {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_glb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Directory() {
        REQUIRE(std::filesystem::create_directory(m_root));
    }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }
};

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value {};
    for (std::size_t i { 0uz }; i < 4uz; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8uz);
    }
    return value;
}

void integer(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i { 0uz }; i < 4uz; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8uz)) & 255U);
    }
}

template <class Mutator>
std::vector<std::byte> mutate_document(const std::vector<std::byte>& bytes, Mutator mutate) {
    const auto K_LENGTH = word(bytes, 12uz);
    auto document =
        nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()) + 20,
                              reinterpret_cast<const char*>(bytes.data()) + 20uz + K_LENGTH);
    mutate(document);
    auto text = document.dump();
    while (text.size() % 4uz) {
        text.push_back(' ');
    }
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

TEST_CASE("sequence GLB preserves all 120 candidate bits wide identities and native topology",
          "[gltf][sequence_glb]") {
    const auto K_COUNT = GENERATE(19uz, 63uz, 120uz);
    const auto K_SOURCE = fixture(GENERATE(false, true), K_COUNT);
    const auto K_HASH = scene_content_hash(*K_SOURCE);
    const auto K_BYTES = encode_baked_glb(K_SOURCE);
    REQUIRE(K_BYTES);
    const auto K_LOADED = decode_baked_glb(*K_BYTES);
    REQUIRE(K_LOADED);
    const auto& before = *K_SOURCE->m_chunks.at({ 9 });
    const auto& after = *(*K_LOADED)->m_chunks.at({ 9 });
    const auto& native = baked_of(before);
    const auto& portable = baked_of(after);
    CHECK(scene_untextured_chunk_hash(before) == scene_untextured_chunk_hash(after));
    CHECK(portable.m_profile == MeshTextureProfile::DEPTH_SEQUENCE);
    CHECK(portable.m_frames == native.m_frames);
    CHECK(portable.m_owners == native.m_owners);
    CHECK(portable.m_candidates.empty());
    CHECK(portable.m_sequence_candidates == native.m_sequence_candidates);
    CHECK(portable.m_corner_uv == native.m_corner_uv);
    CHECK(portable.m_portable_parent_sha256 == scene_chunk_content_hash(before));
    const auto K_META = nlohmann::json::parse(portable.m_manifest_json);
    CHECK(K_META.at("schema_version") == 4);
    CHECK(K_META.at("kind") == "CAPTURED_UNLIT_ATLAS_SEQUENCE_PNG_V1");
    for (std::size_t i { 0uz }; i < native.m_rgba.size(); ++i) {
        CHECK(portable.m_rgba[i][3] == native.m_rgba[i][3]);
        for (std::size_t c { 0uz }; c < 3uz; ++c) {
            CHECK(std::abs(portable.m_rgba[i][c] - native.m_rgba[i][c]) <= .0045F);
        }
        if (native.m_rgba[i][3] == 1) {
            CHECK(portable.m_owners[i] == before.m_source_frames.back());
            for (std::size_t bit { 0uz }; bit < 128uz; ++bit) {
                CHECK(((portable.m_sequence_candidates[i][bit / 32uz] >> (bit % 32uz)) & 1U) ==
                      (bit < K_COUNT ? 1U : 0U));
            }
        }
    }
    Directory directory;
    REQUIRE(write_bundle(directory.m_root / "portable", *K_LOADED));
    const auto K_REOPENED = load_bundle(directory.m_root / "portable");
    REQUIRE(K_REOPENED);
    CHECK(scene_content_hash(**K_REOPENED) == scene_content_hash(**K_LOADED));
    CHECK(encode_baked_glb(*K_LOADED).has_value() == false);
    CHECK(scene_content_hash(*K_SOURCE) == K_HASH);
}

TEST_CASE("sequence GLB explicitly describes mixed legacy and sequence ownership",
          "[gltf][sequence_glb]") {
    auto scene = std::make_shared<SceneState>(*fixture(false, 63uz));
    scene->m_chunks[{ 10 }] = fixture(false, 0uz, { 10 })->m_chunks.at({ 10 });
    const auto K_BYTES = encode_baked_glb(scene);
    REQUIRE(K_BYTES);
    const auto K_LOADED = decode_baked_glb(*K_BYTES);
    REQUIRE(K_LOADED);
    CHECK((*K_LOADED)->m_chunks.size() == 2uz);
    Directory directory;
    REQUIRE(write_bundle(directory.m_root / "mixed", *K_LOADED));
    const auto K_REOPENED = load_bundle(directory.m_root / "mixed");
    REQUIRE(K_REOPENED);
    CHECK(scene_content_hash(**K_REOPENED) == scene_content_hash(**K_LOADED));
    for (const auto& [K_ID, K_CHUNK] : (*K_REOPENED)->m_chunks) {
        const auto& baked = baked_of(*K_CHUNK);
        CHECK(baked.m_profile == (K_ID.m_value == 9 ? MeshTextureProfile::DEPTH_SEQUENCE
                                                    : MeshTextureProfile::SINGLE_CACHE));
        CHECK(nlohmann::json::parse(baked.m_manifest_json).at("schema_version") ==
              (K_ID.m_value == 9 ? 4 : 2));
    }
}

TEST_CASE("sequence GLB rejects ambiguous layouts budgets and rehashed authoring changes",
          "[gltf][sequence_glb]") {
    const auto K_BYTES = encode_baked_glb(fixture(false, GENERATE(19uz, 63uz, 120uz)));
    REQUIRE(K_BYTES);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    const auto K_CHANGED = mutate_document(*K_BYTES, [&](auto& document) {
        auto& header = document["nodes"][0]["extras"];
        if (K_CASE == 0) {
            document["extras"]["profile"] = "WE_CAPTURED_GLB_V1";
        }
        if (K_CASE == 1) {
            header["ownership_layout"] = "OWNER_U64_CANDIDATES_U32_LE";
        }
        if (K_CASE == 2) {
            header.erase("ownership_layout");
        }
        if (K_CASE == 3) {
            header["ownership_layout"] = "OWNER_U64_CANDIDATES_U32X5_LE";
        }
        if (K_CASE == 4) {
            header["frames"].push_back(header["frames"][0]);
        }
        if (K_CASE == 5) {
            header["frames"] = std::vector<std::string>(121uz, "1");
        }
        if (K_CASE == 6) {
            header["width"] = 4097;
        }
        if (K_CASE == 7) {
            header["height"] = 4096;
            header["width"] = 4096;
        }
        if (K_CASE == 8) {
            document["bufferViews"][4]["byteLength"] = 0;
        }
        if (K_CASE >= 9) {
            auto metadata =
                nlohmann::json::parse(header["appearance_json"].template get<std::string>());
            if (K_CASE == 9) {
                metadata["schema_version"] = 1;
            }
            if (K_CASE == 10) {
                metadata["kind"] = "CAPTURED_UNLIT_ATLAS_V1";
            }
            if (K_CASE == 11) {
                metadata["blend"] = "invented";
            }
            if (K_CASE == 12) {
                metadata["parent_chunk_sha256"] = std::string(64uz, 'a');
            }
            const auto K_TEXT = metadata.dump(2);
            header["appearance_json"] = K_TEXT;
            header["appearance_sha256"] = sha256(std::as_bytes(std::span(K_TEXT)));
        }
    });
    CHECK(decode_baked_glb(K_CHANGED).has_value() == false);
}

TEST_CASE("sequence GLB rejects reserved candidate bits missing owners and damaged wide planes",
          "[gltf][sequence_glb]") {
    const auto K_COUNT = GENERATE(19uz, 63uz, 120uz);
    const auto K_BYTES = encode_baked_glb(fixture(false, K_COUNT));
    REQUIRE(K_BYTES);
    auto changed = *K_BYTES;
    const auto K_LENGTH = word(changed, 12uz);
    const auto K_DOCUMENT =
        nlohmann::json::parse(reinterpret_cast<const char*>(changed.data()) + 20,
                              reinterpret_cast<const char*>(changed.data()) + 20uz + K_LENGTH);
    const auto K_START =
        28uz + K_LENGTH + K_DOCUMENT["bufferViews"][4]["byteOffset"].get<std::size_t>();
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    if (K_CASE == 0) {
        changed[K_START + 23uz] |= std::byte { 128 };
    }
    if (K_CASE == 1) {
        const auto K_OFFSET = K_START + 8uz + (K_COUNT - 1uz) / 32uz * 4uz;
        integer(changed, K_OFFSET, word(changed, K_OFFSET) & ~(1U << ((K_COUNT - 1uz) % 32uz)));
    }
    if (K_CASE == 2) {
        const auto K_OFFSET = K_START + 8uz + K_COUNT / 32uz * 4uz;
        integer(changed, K_OFFSET, word(changed, K_OFFSET) | (1U << (K_COUNT % 32uz)));
    }
    if (K_CASE == 3) {
        integer(changed, K_START, 1);
        integer(changed, K_START + 4uz, 0);
    }
    CHECK(decode_baked_glb(changed).has_value() == false);
}

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
    const auto& before = baked_of(original);
    const auto& after = baked_of(loaded);
    CHECK(after.m_portable_parent_sha256 == scene_chunk_content_hash(original));
    CHECK(before.m_owners == after.m_owners);
    CHECK(before.m_candidates == after.m_candidates);
    CHECK(before.m_corner_uv == after.m_corner_uv);
    CHECK(before.m_rgba.size() == after.m_rgba.size());
    for (std::size_t i { 0uz }; i < before.m_rgba.size(); ++i) {
        CHECK(before.m_rgba[i][3] == after.m_rgba[i][3]);
        for (std::size_t c { 0uz }; c < 3uz; ++c) {
            CHECK(std::abs(before.m_rgba[i][c] - after.m_rgba[i][c]) <= .0045F);
        }
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
        if (K_CASE == 0) {
            document["buffers"][0]["uri"] = "https://example.invalid/remote.bin";
        }
        if (K_CASE == 1) {
            document["images"][0]["uri"] = "../private.png";
        }
        if (K_CASE == 2) {
            document["accessors"][0]["count"] = 999999999;
        }
        if (K_CASE == 3) {
            document["bufferViews"][0]["byteOffset"] = 4;
        }
        if (K_CASE == 4) {
            document["materials"][0]["alphaCutoff"] = .5;
        }
        if (K_CASE == 5) {
            document["samplers"][0]["minFilter"] = 9987;
        }
        if (K_CASE == 6) {
            document["nodes"][0]["rotation"] = { 0, 0, 0, 1 };
        }
        if (K_CASE == 7) {
            document["nodes"][0]["extras"]["source_chunk_sha256"] = std::string(64uz, 'a');
        }
        if (K_CASE == 8) {
            document["nodes"][0]["extras"]["triangles"] = 0;
        }
        if (K_CASE == 9) {
            document["extras"]["source_images_added"] = true;
        }
        if (K_CASE == 10) {
            document["extensionsRequired"] = { "KHR_draco_mesh_compression" };
        }
        if (K_CASE == 11) {
            document["meshes"][0]["primitives"][0]["indices"] = 0;
        }
        if (K_CASE == 12) {
            document["nodes"][0]["extras"]["bounds"].push_back(0);
        }
        if (K_CASE == 13) {
            document["nodes"][0]["extras"]["rotation"].push_back(0);
        }
        if (K_CASE == 14) {
            document["nodes"][0]["extras"]["translation"].push_back(0);
        }
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
    if (K_CASE == 0) {
        bytes[K_START] ^= std::byte { 1 };
    }
    if (K_CASE == 1) {
        const auto K_OFFSET = K_DOCUMENT["bufferViews"][3]["byteOffset"].get<std::size_t>();
        bytes[K_START + K_OFFSET + 42uz] ^= std::byte { 1 };
    }
    if (K_CASE == 2) {
        const auto K_OFFSET = K_DOCUMENT["bufferViews"][4]["byteOffset"].get<std::size_t>();
        bytes[K_START + K_OFFSET] ^= std::byte { 1 };
    }
    if (K_CASE == 3) {
        bytes.pop_back();
    }
    if (K_CASE == 4) {
        bytes.push_back(std::byte {});
    }
    if (K_CASE == 5) {
        bytes[16] = std::byte {};
    }
    CHECK(decode_baked_glb(bytes).has_value() == false);
    CHECK(scene_content_hash(*K_SOURCE) == scene_content_hash(*fixture()));
}

TEST_CASE("GLB cancellation and publication failures preserve inputs and previous outputs",
          "[gltf]") {
    const auto K_SOURCE = fixture(false, GENERATE(0uz, 120uz));
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
        if (!chunk->m_mesh) {
            throw std::runtime_error("Missing test mesh");
        }
        chunk->m_mesh->m_baked.reset();
        scene->m_chunks[{ 10 }] = chunk;
    }
    if (K_CASE == 2) {
        auto splat = std::make_shared<SceneChunk>();
        splat->m_id = { 10 };
        splat->m_bounds = { { -1, -1, -1 }, { 1, 1, 1 } };
        splat->m_splats.push_back(Gaussian {});
        scene->m_chunks[{ 10 }] = splat;
    }
    REQUIRE(validate_scene(*scene));
    const auto K_BEFORE = scene_content_hash(*scene);
    CHECK(encode_baked_glb(scene).has_value() == false);
    CHECK(scene_content_hash(*scene) == K_BEFORE);
}

TEST_CASE("portable native admission binds filter parent and rounded sRGB8 storage", "[gltf]") {
    const auto K_BYTES = encode_baked_glb(fixture(false, GENERATE(0uz, 120uz)));
    REQUIRE(K_BYTES);
    const auto K_LOADED = decode_baked_glb(*K_BYTES);
    REQUIRE(K_LOADED);
    auto scene = std::make_shared<SceneState>(**K_LOADED);
    auto chunk = std::make_shared<SceneChunk>(*scene->m_chunks.at({ 9 }));
    if (!chunk->m_mesh || !chunk->m_mesh->m_baked) {
        throw std::runtime_error("Missing portable test appearance");
    }
    auto& appearance = *chunk->m_mesh->m_baked;
    const auto K_CASE = GENERATE(0, 1, 2);
    if (K_CASE == 0) {
        appearance.m_rgba[0][0] = .5F;
    }
    if (K_CASE == 1) {
        appearance.m_portable_parent_sha256.clear();
    }
    if (K_CASE == 2) {
        appearance.m_portable_parent_sha256 = std::string(64uz, 'a');
    }
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
