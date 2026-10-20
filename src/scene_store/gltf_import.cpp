#include "gltf_internal.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>

namespace world_explorer {

Result<SceneSnapshot> decode_baked_glb(std::span<const std::byte> bytes,
                                       const std::stop_token& stop) {
    using namespace gltf_detail;
    try {
        cancelled(stop);
        require(bytes.size() <= K_MAX_BYTES, "GLB exceeds encoded budget");
        Reader file { bytes };
        require(file.integer() == 0x46546c67U && file.integer() == 2 &&
                    file.integer() == bytes.size(),
                "Invalid GLB header");
        const auto K_JSON_LENGTH = static_cast<std::size_t>(file.integer());
        require(K_JSON_LENGTH % 4uz == 0 && K_JSON_LENGTH <= 16uz * 1024uz * 1024uz &&
                    file.integer() == 0x4e4f534aU,
                "Invalid GLB JSON chunk");
        const auto K_TEXT = file.take(K_JSON_LENGTH);
        const auto K_DOCUMENT =
            parse({ reinterpret_cast<const char*>(K_TEXT.data()), K_TEXT.size() });
        const auto K_BINARY_LENGTH = static_cast<std::size_t>(file.integer());
        require(K_BINARY_LENGTH % 4uz == 0 && file.integer() == 0x004e4942U,
                "Invalid GLB BIN chunk");
        const auto K_BINARY = file.take(K_BINARY_LENGTH);
        require(file.m_position == bytes.size() && K_DOCUMENT.is_object() &&
                    K_DOCUMENT.size() == 15uz,
                "Unsupported GLB root");
        const auto& nodes = K_DOCUMENT.at("nodes");
        require(nodes.is_array() && !nodes.empty() && nodes.size() <= 4096uz,
                "GLB node count exceeds budget");
        const auto K_N = nodes.size();
        require(count(K_DOCUMENT.at("scene"), 0uz) == 0uz, "Unsupported GLB scene");
        const auto K_EQUAL = [](const Json& actual, const Json& expected) {
            require(actual.dump() == expected.dump(),
                    "Unsupported or inconsistent GLB profile field");
        };
        K_EQUAL(K_DOCUMENT.at("buffers"), Json::array({ { { "byteLength", K_BINARY.size() } } }));
        for (const auto* key : { "meshes", "images", "textures", "materials" }) {
            require(K_DOCUMENT.at(key).is_array() && K_DOCUMENT.at(key).size() == K_N,
                    "GLB resource counts differ");
        }
        require(K_DOCUMENT.at("accessors").is_array() &&
                    K_DOCUMENT.at("accessors").size() == K_N * 2uz &&
                    K_DOCUMENT.at("bufferViews").is_array() &&
                    K_DOCUMENT.at("bufferViews").size() == K_N * 5uz,
                "GLB view/accessor counts differ");
        std::vector<std::span<const std::byte>> views;
        std::size_t offset {};
        for (std::size_t i { 0uz }; i < K_N * 5uz; ++i) {
            const auto& view = K_DOCUMENT.at("bufferViews")[i];
            const auto K_LENGTH = count(view.at("byteLength"), K_BINARY.size());
            require(offset <= K_BINARY.size() && K_LENGTH <= K_BINARY.size() - offset,
                    "GLB view outside buffer");
            Json expected { { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", K_LENGTH } };
            if (i % 5uz == 0uz) {
                expected["byteStride"] = 20;
                expected["target"] = 34962;
            }
            K_EQUAL(view, expected);
            views.push_back(K_BINARY.subspan(offset, K_LENGTH));
            offset += K_LENGTH;
            while (offset % 4uz) {
                require(offset < K_BINARY.size() && K_BINARY[offset] == std::byte {},
                        "Invalid GLB padding");
                ++offset;
            }
        }
        require(offset == K_BINARY.size(), "Unreferenced GLB bytes");
        auto scene = std::make_shared<SceneState>();
        const auto K_IDENTITIES =
            K_DOCUMENT.at("extras").at("scene_identity").get<std::array<std::string, 3>>();
        const auto K_ID = [](const std::string& text) {
            std::size_t used {};
            const auto K_VALUE = std::stoull(text, &used);
            require(used == text.size() && std::to_string(K_VALUE) == text,
                    "Invalid GLB scene identity");
            return static_cast<std::uint64_t>(K_VALUE);
        };
        scene->m_id = { K_ID(K_IDENTITIES[0]) };
        scene->m_epoch = { K_ID(K_IDENTITIES[1]) };
        scene->m_revision = { K_ID(K_IDENTITIES[2]) };
        require(K_DOCUMENT.at("extras").at("synthetic").is_boolean(),
                "Invalid GLB synthetic label");
        scene->m_provenance["source_kind"] = K_DOCUMENT.at("extras").at("synthetic").get<bool>()
                                                 ? "synthetic_diagnostic"
                                                 : "captured_baked_glb";
        const auto K_PROFILE = K_DOCUMENT.at("extras").at("profile").get<std::string>();
        const bool K_SEQUENCE_PROFILE { K_PROFILE == "WE_CAPTURED_GLB_V2" };
        require(K_SEQUENCE_PROFILE || K_PROFILE == "WE_CAPTURED_GLB_V1", "Unsupported GLB profile");
        const auto K_BASE = document_base(*scene, K_SEQUENCE_PROFILE);
        for (const auto* key : { "asset", "samplers", "extensionsUsed", "extensionsRequired" }) {
            K_EQUAL(K_DOCUMENT.at(key), K_BASE.at(key));
        }
        auto expected_extras = K_BASE.at("extras");
        const auto K_SOURCE_HASH =
            K_DOCUMENT.at("extras").at("source_scene_sha256").get<std::string>();
        require(K_SOURCE_HASH.size() == 64uz &&
                    K_SOURCE_HASH.find_first_not_of("0123456789abcdef") == K_SOURCE_HASH.npos,
                "Invalid GLB source pin");
        expected_extras["source_scene_sha256"] = K_SOURCE_HASH;
        K_EQUAL(K_DOCUMENT.at("extras"), expected_extras);
        scene->m_provenance["source_scene_sha256"] = K_SOURCE_HASH;
        scene->m_provenance["appearance"] =
            "baked_appearance; captured unlit; conservative bilinear validity; sRGB8 quantization";
        Json selected_nodes = Json::array();
        std::size_t total_vertices {}, total_triangles {}, total_texels {};
        bool has_sequence {};
        for (std::size_t n { 0uz }; n < K_N; ++n) {
            cancelled(stop);
            const auto& node = nodes[n];
            require(node.is_object() && node.size() == 5uz, "Unsupported GLB node fields");
            const auto& header = node.at("extras");
            auto chunk = read_header(header, K_SEQUENCE_PROFILE);
            require(chunk.m_mesh && chunk.m_mesh->m_baked, "Missing GLB mesh metadata");
            auto& mesh = *chunk.m_mesh;
            auto& baked = *mesh.m_baked;
            const bool K_SEQUENCE { baked.m_profile == MeshTextureProfile::DEPTH_SEQUENCE };
            has_sequence = has_sequence || K_SEQUENCE;
            const auto K_V = count(header.at("vertices"), 4000000uz - total_vertices);
            const auto K_T =
                count(header.at("triangles"), std::min(100000uz, 8000000uz - total_triangles));
            const auto K_P = static_cast<std::size_t>(baked.m_width) * baked.m_height;
            require(K_V && K_T && K_P <= 8000000uz - total_texels,
                    "GLB decoded aggregate budget exceeded");
            total_vertices += K_V;
            total_triangles += K_T;
            total_texels += K_P;
            const auto K_VIEW = n * 5uz, K_ACCESSOR = n * 2uz;
            require(views[K_VIEW].size() == K_T * 60uz &&
                        views[K_VIEW + 1uz].size() == K_V * 24uz &&
                        views[K_VIEW + 2uz].size() == K_T * 12uz &&
                        views[K_VIEW + 4uz].size() == K_P * (K_SEQUENCE ? 24uz : 12uz),
                    "GLB geometry/ownership counts disagree with bytes");
            Reader vertices { views[K_VIEW + 1uz] }, triangles { views[K_VIEW + 2uz] },
                ownership { views[K_VIEW + 4uz] };
            mesh.m_vertices.resize(K_V);
            mesh.m_triangles.resize(K_T);
            for (auto& vertex : mesh.m_vertices) {
                for (auto& value : vertex.m_position) {
                    value = vertices.real();
                }
                for (auto& value : vertex.m_normal) {
                    value = vertices.real();
                }
            }
            for (auto& triangle : mesh.m_triangles) {
                for (auto& index : triangle) {
                    const auto K_INDEX = triangles.integer();
                    require(K_INDEX < K_V, "GLB source index outside vertices");
                    index = static_cast<std::uint32_t>(K_INDEX);
                }
            }
            const auto K_EXTENT = image_extent(baked);
            const auto K_IMAGE = decode_png(views[K_VIEW + 3uz], stop);
            require(K_IMAGE.m_width == K_EXTENT[0] && K_IMAGE.m_height == K_EXTENT[1],
                    "GLB PNG extent differs");
            baked.m_rgba.resize(K_P);
            baked.m_owners.resize(K_P);
            if (K_SEQUENCE) {
                baked.m_sequence_candidates.resize(K_P);
            } else {
                baked.m_candidates.resize(K_P);
            }
            for (std::size_t y { 0uz }; y < K_IMAGE.m_height; ++y) {
                cancelled(stop);
                for (std::size_t x { 0uz }; x < K_IMAGE.m_width; ++x) {
                    const auto K_PIXEL = (y * K_IMAGE.m_width + x) * 4uz;
                    if (x >= baked.m_width || y >= baked.m_height) {
                        for (std::size_t c { 0uz }; c < 4uz; ++c) {
                            require(K_IMAGE.m_rgba[K_PIXEL + c] == std::byte {},
                                    "GLB PNG padding is nonzero");
                        }
                        continue;
                    }
                    auto& color = baked.m_rgba[y * baked.m_width + x];
                    const auto K_ALPHA = K_IMAGE.m_rgba[K_PIXEL + 3uz];
                    require(K_ALPHA == std::byte {} || K_ALPHA == std::byte { 255 },
                            "GLB alpha must be binary");
                    for (std::size_t c { 0uz }; c < 3uz; ++c) {
                        const auto K_SRGB =
                            std::to_integer<unsigned>(K_IMAGE.m_rgba[K_PIXEL + c]) / 255.0;
                        color[c] = static_cast<float>(K_SRGB <= .04045
                                                          ? K_SRGB / 12.92
                                                          : std::pow((K_SRGB + .055) / 1.055, 2.4));
                    }
                    color[3] = K_ALPHA == std::byte { 255 } ? 1.F : 0.F;
                }
            }
            for (std::size_t i { 0uz }; i < K_P; ++i) {
                baked.m_owners[i] = { ownership.integer(8uz) };
                if (K_SEQUENCE) {
                    for (auto& word : baked.m_sequence_candidates[i]) {
                        word = static_cast<std::uint32_t>(ownership.integer());
                    }
                } else {
                    baked.m_candidates[i] = static_cast<std::uint32_t>(ownership.integer());
                }
            }
            Reader rendered { views[K_VIEW] };
            const auto K_COLUMNS = baked.m_width / 8U;
            require(K_COLUMNS && K_T <= static_cast<std::size_t>(baked.m_height / 8U) * K_COLUMNS,
                    "Invalid GLB tile layout");
            std::array<float, 3> minimum;
            minimum.fill(std::numeric_limits<float>::max());
            std::array<float, 3> maximum;
            maximum.fill(std::numeric_limits<float>::lowest());
            for (std::size_t face { 0uz }; face < K_T; ++face) {
                cancelled(stop);
                const auto K_X = static_cast<float>(face % K_COLUMNS * 8uz) + .5F;
                const auto K_ROW = face / K_COLUMNS;
                const auto K_Y = static_cast<float>(K_ROW * 8uz) + .5F;
                const std::array<std::array<float, 2>, 3> K_UV {
                    std::array { K_X, K_Y }, { K_X + 7, K_Y }, { K_X, K_Y + 7 } };
                for (std::size_t c { 0uz }; c < 3uz; ++c) {
                    const auto& vertex = mesh.m_vertices[mesh.m_triangles[face][c]];
                    for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
                        require(rendered.real() == vertex.m_position[axis],
                                "GLB visible positions differ from source geometry");
                        minimum[axis] = std::min(minimum[axis], vertex.m_position[axis]);
                        maximum[axis] = std::max(maximum[axis], vertex.m_position[axis]);
                    }
                    require(rendered.real() * static_cast<float>(K_IMAGE.m_width) == K_UV[c][0] &&
                                rendered.real() * static_cast<float>(K_IMAGE.m_height) ==
                                    K_UV[c][1],
                            "GLB UVs differ from canonical geometry");
                    baked.m_corner_uv.push_back(K_UV[c]);
                }
            }
            K_EQUAL(K_DOCUMENT.at("accessors")[K_ACCESSOR],
                    Json { { "bufferView", K_VIEW },
                          { "byteOffset", 0 },
                          { "componentType", 5126 },
                          { "count", K_T * 3uz },
                          { "type", "VEC3" },
                          { "min", minimum },
                          { "max", maximum } });
            K_EQUAL(K_DOCUMENT.at("accessors")[K_ACCESSOR + 1uz],
                    Json { { "bufferView", K_VIEW },
                          { "byteOffset", 12 },
                          { "componentType", 5126 },
                          { "count", K_T * 3uz },
                          { "type", "VEC2" } });
            K_EQUAL(K_DOCUMENT.at("images")[n],
                    Json { { "bufferView", K_VIEW + 3uz }, { "mimeType", "image/png" } });
            K_EQUAL(K_DOCUMENT.at("textures")[n], Json { { "sampler", 0 }, { "source", n } });
            K_EQUAL(K_DOCUMENT.at("materials")[n],
                    Json { { "name", "baked_appearance" },
                          { "extensions", { { "KHR_materials_unlit", Json::object() } } },
                          { "alphaMode", "MASK" },
                          { "alphaCutoff", 1.0 },
                          { "doubleSided", true },
                          { "pbrMetallicRoughness",
                           { { "baseColorTexture", { { "index", n } } },
                            { "metallicFactor", 0 },
                            { "roughnessFactor", 1 } } } });
            K_EQUAL(
                K_DOCUMENT.at("meshes")[n],
                Json { { "primitives",
                       Json::array({ { { "attributes",
                                      { { "POSITION", K_ACCESSOR }, { "TEXCOORD_0", K_ACCESSOR + 1uz } } },
                                     { "material", n },
                                     { "mode", 4 } } }) } });
            const auto& transform = chunk.m_world_from_chunk;
            K_EQUAL(node,
                    Json { { "mesh", n },
                          { "translation",
                           { transform.m_translation.m_x,
                            transform.m_translation.m_y,
                            transform.m_translation.m_z } },
                          { "rotation",
                           { transform.m_rotation.m_x,
                            transform.m_rotation.m_y,
                            transform.m_rotation.m_z,
                            transform.m_rotation.m_w } },
                          { "scale", { transform.m_scale, transform.m_scale, transform.m_scale } },
                          { "extras", header } });
            require(sha256(std::as_bytes(std::span(baked.m_manifest_json))) ==
                        baked.m_manifest_sha256,
                    "GLB authoring metadata hash differs");
            auto appearance = parse(baked.m_manifest_json);
            require(count(appearance.at("schema_version"), K_SEQUENCE ? 3uz : 1uz) ==
                            (K_SEQUENCE ? 3uz : 1uz) &&
                        appearance.at("kind").get<std::string>() ==
                            (K_SEQUENCE ? "CAPTURED_UNLIT_ATLAS_SEQUENCE_V1"
                                        : "CAPTURED_UNLIT_ATLAS_V1") &&
                        !appearance.contains("parent_chunk_sha256") &&
                        appearance.at("blend").get<std::string>() ==
                            "score_squared_convex_linear_rgb" &&
                        appearance.at("filter").get<std::string>() ==
                            "triangular_linear_valid_only",
                    "Unsupported GLB original appearance");
            baked.m_portable_parent_sha256 = header.at("parent_chunk_sha256").get<std::string>();
            appearance["schema_version"] = K_SEQUENCE ? 4 : 2;
            appearance["kind"] =
                K_SEQUENCE ? "CAPTURED_UNLIT_ATLAS_SEQUENCE_PNG_V1" : "CAPTURED_UNLIT_ATLAS_PNG_V1";
            appearance["filter"] = "bilinear_valid_only";
            appearance["blend"] = "score_squared_convex_linear_rgb_then_srgb8";
            appearance["parent_chunk_sha256"] = baked.m_portable_parent_sha256;
            baked.m_manifest_json = appearance.dump(2);
            baked.m_manifest_sha256 = sha256(std::as_bytes(std::span(baked.m_manifest_json)));
            const auto K_VALID = validate_chunk(chunk);
            if (!K_VALID) {
                return std::unexpected(K_VALID.error());
            }
            const auto K_IDENTITY = chunk.m_id;
            require(
                scene->m_chunks.emplace(K_IDENTITY, std::make_shared<SceneChunk>(std::move(chunk)))
                    .second,
                "Duplicate GLB chunk ID");
            selected_nodes.push_back(n);
        }
        require(has_sequence == K_SEQUENCE_PROFILE, "GLB profile disagrees with appearances");
        K_EQUAL(K_DOCUMENT.at("scenes"), Json::array({ { { "nodes", selected_nodes } } }));
        if (std::ranges::all_of(scene->m_chunks, [](const auto& entry) {
                return entry.second->m_mesh && entry.second->m_mesh->m_observed &&
                       !entry.second->m_mesh->m_observed->m_cleanup_json.empty();
            })) {
            scene->m_provenance["geometry_processing"] = "retained source cleanup certificates";
        }
        const auto K_VALID = validate_scene(*scene);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        cancelled(stop);
        return scene;
    } catch (const Cancelled& error) {
        return fail(ErrorCode::CANCELLED, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "GLB import allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
