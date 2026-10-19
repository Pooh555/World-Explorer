#include "gltf_internal.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <charconv>
#include <set>

namespace world_explorer::gltf_detail {

std::size_t count(const Json& value, std::size_t maximum) {
    require(value.is_number_unsigned(), "GLB count must be an unsigned integer");
    const auto K_VALUE = value.get<std::uint64_t>();
    require(K_VALUE <= maximum, "GLB count exceeds budget");
    return static_cast<std::size_t>(K_VALUE);
}

Json parse(std::string_view text) {
    require(text.size() <= 16uz * 1024uz * 1024uz && text.find('\0') == text.npos,
            "Invalid GLB JSON length/NUL");
    std::vector<std::set<std::string>> keys;
    return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 24, "GLB JSON nesting exceeds budget");
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.resize(K_DEPTH + 1uz);
            keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key) {
            require(keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second,
                    "Duplicate GLB JSON key");
        }
        return true;
    });
}

std::array<std::uint32_t, 2> image_extent(const BakedMeshAppearance& baked) {
    return { std::bit_ceil(baked.m_width), std::bit_ceil(baked.m_height) };
}

Json chunk_header(const SceneChunk& chunk) {
    if (!chunk.m_mesh || !chunk.m_mesh->m_baked) {
        throw std::runtime_error("Missing captured appearance");
    }
    const auto& mesh = *chunk.m_mesh;
    const auto& baked = *mesh.m_baked;
    Json frames = Json::array();
    for (auto frame : chunk.m_source_frames) {
        frames.push_back(std::to_string(frame.m_value));
    }
    Json source = nullptr;
    if (mesh.m_observed) {
        const auto& observed = *mesh.m_observed;
        const auto& context = observed.m_context;
        source = { { "pins",
                   { context.m_manifest_sha256,
                    context.m_receipt_sha256,
                    context.m_recipe_sha256,
                    context.m_pose_sha256,
                    observed.m_asset_sha256,
                    observed.m_cleanup_sha256 } },
                  { "documents",
                   { observed.m_manifest_json, observed.m_receipt_json, observed.m_cleanup_json } },
                  { "epoch", std::to_string(context.m_epoch.m_value) },
                  { "pose_revision", std::to_string(context.m_pose_revision.m_value) } };
    }
    const auto& transform = chunk.m_world_from_chunk;
    return {
        { "id", std::to_string(chunk.m_id.m_value) },
        { "generation", std::to_string(chunk.m_generation) },
        { "pose_revision", std::to_string(chunk.m_pose_revision.m_value) },
        { "frames", frames },
        { "bounds",
         { chunk.m_bounds.m_minimum.m_x,
          chunk.m_bounds.m_minimum.m_y,
          chunk.m_bounds.m_minimum.m_z,
          chunk.m_bounds.m_maximum.m_x,
          chunk.m_bounds.m_maximum.m_y,
          chunk.m_bounds.m_maximum.m_z } },
        { "rotation",
         { transform.m_rotation.m_w,
          transform.m_rotation.m_x,
          transform.m_rotation.m_y,
          transform.m_rotation.m_z } },
        { "translation",
         { transform.m_translation.m_x, transform.m_translation.m_y, transform.m_translation.m_z } },
        { "scale", transform.m_scale },
        { "source", source },
        { "vertices", mesh.m_vertices.size() },
        { "triangles", mesh.m_triangles.size() },
        { "width", baked.m_width },
        { "height", baked.m_height },
        { "appearance_json", baked.m_manifest_json },
        { "appearance_sha256", baked.m_manifest_sha256 },
        { "source_chunk_sha256", baked.m_source_chunk_sha256 },
        { "parent_chunk_sha256", scene_chunk_content_hash(chunk) } };
}

SceneChunk read_header(const Json& document) {
    const auto K_ID = [](const Json& value) {
        const auto K_TEXT = value.get<std::string>();
        std::uint64_t result {};
        const auto K_PARSED = std::from_chars(K_TEXT.data(), K_TEXT.data() + K_TEXT.size(), result);
        require(!K_TEXT.empty() && K_PARSED.ec == std::errc {} &&
                    K_PARSED.ptr == K_TEXT.data() + K_TEXT.size() &&
                    std::to_string(result) == K_TEXT,
                "Invalid GLB identity");
        return result;
    };
    require(document.is_object() && document.size() == 17uz, "Unsupported GLB chunk metadata");
    const auto K_ARRAY_SIZE = [](const Json& value, std::size_t size) {
        require(value.is_array() && value.size() == size, "Invalid GLB metadata array length");
    };
    K_ARRAY_SIZE(document.at("bounds"), 6uz);
    K_ARRAY_SIZE(document.at("rotation"), 4uz);
    K_ARRAY_SIZE(document.at("translation"), 3uz);
    SceneChunk chunk;
    chunk.m_id = { K_ID(document.at("id")) };
    chunk.m_generation = K_ID(document.at("generation"));
    chunk.m_pose_revision = { K_ID(document.at("pose_revision")) };
    chunk.m_geometry_valid = true;
    require(document.at("frames").is_array() && document.at("frames").size() <= 18uz,
            "Invalid GLB source frames");
    for (const auto& frame : document.at("frames")) {
        chunk.m_source_frames.push_back({ K_ID(frame) });
    }
    const auto K_BOUNDS = document.at("bounds").get<std::array<double, 6>>();
    chunk.m_bounds = { { K_BOUNDS[0], K_BOUNDS[1], K_BOUNDS[2] },
                      { K_BOUNDS[3], K_BOUNDS[4], K_BOUNDS[5] } };
    const auto K_ROTATION = document.at("rotation").get<std::array<double, 4>>();
    const auto K_TRANSLATION = document.at("translation").get<std::array<double, 3>>();
    chunk.m_world_from_chunk = { { K_ROTATION[0], K_ROTATION[1], K_ROTATION[2], K_ROTATION[3] },
                                { K_TRANSLATION[0], K_TRANSLATION[1], K_TRANSLATION[2] },
                                document.at("scale").get<double>() };
    auto& mesh = chunk.m_mesh.emplace();
    if (!document.at("source").is_null()) {
        const auto& source = document.at("source");
        require(source.is_object() && source.size() == 4uz, "Invalid GLB observed context");
        K_ARRAY_SIZE(source.at("pins"), 6uz);
        K_ARRAY_SIZE(source.at("documents"), 3uz);
        const auto K_PINS = source.at("pins").get<std::array<std::string, 6>>();
        const auto K_TEXTS = source.at("documents").get<std::array<std::string, 3>>();
        mesh.m_observed = ObservedMeshSource { { K_PINS[0],
                                               K_PINS[1],
                                               K_PINS[2],
                                               K_PINS[3],
                                               { K_ID(source.at("epoch")) },
                                               { K_ID(source.at("pose_revision")) } },
                                              K_PINS[4],
                                              K_TEXTS[0],
                                              K_TEXTS[1],
                                              K_PINS[5],
                                              K_TEXTS[2] };
    }
    auto& baked = mesh.m_baked.emplace();
    baked.m_width = static_cast<std::uint32_t>(count(document.at("width"), 4096uz));
    baked.m_height = static_cast<std::uint32_t>(count(document.at("height"), 4096uz));
    require(baked.m_width && baked.m_height && baked.m_height <= 8000000uz / baked.m_width,
            "GLB texture exceeds budget");
    baked.m_frames = chunk.m_source_frames;
    baked.m_manifest_json = document.at("appearance_json").get<std::string>();
    baked.m_manifest_sha256 = document.at("appearance_sha256").get<std::string>();
    baked.m_source_chunk_sha256 = document.at("source_chunk_sha256").get<std::string>();
    return chunk;
}

Json document_base(const SceneState& scene) {
    const bool K_SYNTHETIC = scene.m_provenance.contains("source_kind") &&
                             scene.m_provenance.at("source_kind") == "synthetic_diagnostic";
    return { { "asset", { { "version", "2.0" }, { "generator", "World Explorer captured unlit V1" } } },
            { "scene", 0 },
            { "scenes", Json::array() },
            { "nodes", Json::array() },
            { "meshes", Json::array() },
            { "buffers", Json::array() },
            { "bufferViews", Json::array() },
            { "accessors", Json::array() },
            { "images", Json::array() },
            { "textures", Json::array() },
            { "materials", Json::array() },
            { "samplers",
             Json::array(
                 { { { "magFilter", 9729 }, { "minFilter", 9729 }, { "wrapS", 33071 }, { "wrapT", 33071 } } }) },
            { "extensionsUsed", { "KHR_materials_unlit" } },
            { "extensionsRequired", { "KHR_materials_unlit" } },
            { "extras",
             { { "profile", "WE_CAPTURED_GLB_V1" },
              { "source_scene_sha256", scene_content_hash(scene) },
              { "scene_identity",
               { std::to_string(scene.m_id.m_value),
                std::to_string(scene.m_epoch.m_value),
                std::to_string(scene.m_revision.m_value) } },
              { "synthetic", K_SYNTHETIC },
              { "unit_mapping", "1 arbitrary world unit to 1 nominal glTF metre; uncalibrated" },
              { "walking_enabled", false },
              { "source_images_added", false },
              { "location_fields_added", false },
              { "authoring_documents_included", true },
              { "authoring_metadata_review", "UNINSPECTED_RETAINED_DOCUMENTS" },
              { "filter", "bilinear_mask_cutoff_one_no_mips" } } } };
}

}  // namespace world_explorer::gltf_detail

namespace world_explorer {

Result<std::vector<std::byte>> encode_baked_glb(const SceneSnapshot& scene,
                                                const std::stop_token& stop) {
    using namespace gltf_detail;
    try {
        cancelled(stop);
        if (!scene) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Missing GLB scene");
        }
        const auto K_VALID = validate_scene(*scene);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        require(scene->m_scale == ScaleStatus::ARBITRARY && !scene->m_chunks.empty(),
                "GLB profile requires explicit arbitrary-scale captured meshes");
        auto document = document_base(*scene);
        Writer binary;
        Json nodes = Json::array();
        for (const auto& [K_ID, K_CHUNK] : scene->m_chunks) {
            static_cast<void>(K_ID);
            cancelled(stop);
            if (!K_CHUNK->m_geometry_valid || !K_CHUNK->m_splats.empty() || !K_CHUNK->m_mesh ||
                !K_CHUNK->m_mesh->m_baked) {
                throw std::runtime_error(
                    "GLB export cannot omit stale, splat or untextured chunks");
            }
            const auto& mesh = *K_CHUNK->m_mesh;
            const auto& baked = *mesh.m_baked;
            require(baked.m_portable_parent_sha256.empty(),
                    "GLB V1 exports original native bakes only");
            const auto K_EXTENT = image_extent(baked);
            Image image {
                K_EXTENT[0],
                K_EXTENT[1],
                std::vector<std::byte>(static_cast<std::size_t>(K_EXTENT[0]) * K_EXTENT[1] * 4uz) };
            for (std::size_t y { 0uz }; y < baked.m_height; ++y) {
                cancelled(stop);
                for (std::size_t x { 0uz }; x < baked.m_width; ++x) {
                    const auto& color = baked.m_rgba[y * baked.m_width + x];
                    const auto K_PIXEL = (y * image.m_width + x) * 4uz;
                    for (std::size_t c { 0uz }; c < 3uz; ++c) {
                        const double K_LINEAR { color[c] };
                        const auto K_SRGB = K_LINEAR <= .0031308
                                                ? K_LINEAR * 12.92
                                                : 1.055 * std::pow(K_LINEAR, 1 / 2.4) - .055;
                        image.m_rgba[K_PIXEL + c] = static_cast<std::byte>(static_cast<unsigned>(
                            std::clamp(std::floor(K_SRGB * 255 + .5), 0.0, 255.0)));
                    }
                    image.m_rgba[K_PIXEL + 3uz] = color[3] == 1 ? std::byte { 255 } : std::byte {};
                }
            }
            const auto K_INDEX = document["meshes"].size();
            const auto K_VIEW = document["bufferViews"].size();
            const auto K_ACCESSOR = document["accessors"].size();
            const auto K_ADD_VIEW = [&](std::size_t begin, bool render) {
                Json view { { "buffer", 0 },
                           { "byteOffset", begin },
                           { "byteLength", binary.m_data.size() - begin } };
                if (render) {
                    view["byteStride"] = 20;
                    view["target"] = 34962;
                }
                document["bufferViews"].push_back(view);
                binary.align();
            };
            auto begin = binary.m_data.size();
            std::array<float, 3> minimum { std::numeric_limits<float>::max(),
                                          std::numeric_limits<float>::max(),
                                          std::numeric_limits<float>::max() };
            auto maximum = minimum;
            maximum.fill(std::numeric_limits<float>::lowest());
            std::size_t corner {};
            for (const auto& triangle : mesh.m_triangles) {
                cancelled(stop);
                for (auto index : triangle) {
                    const auto& position = mesh.m_vertices[index].m_position;
                    for (std::size_t c { 0uz }; c < 3uz; ++c) {
                        binary.real(position[c]);
                        minimum[c] = std::min(minimum[c], position[c]);
                        maximum[c] = std::max(maximum[c], position[c]);
                    }
                    const auto& uv = baked.m_corner_uv[corner++];
                    binary.real(uv[0] / static_cast<float>(image.m_width));
                    binary.real(uv[1] / static_cast<float>(image.m_height));
                }
            }
            K_ADD_VIEW(begin, true);
            document["accessors"].push_back({ { "bufferView", K_VIEW },
                                             { "byteOffset", 0 },
                                             { "componentType", 5126 },
                                             { "count", corner },
                                             { "type", "VEC3" },
                                             { "min", minimum },
                                             { "max", maximum } });
            document["accessors"].push_back({ { "bufferView", K_VIEW },
                                             { "byteOffset", 12 },
                                             { "componentType", 5126 },
                                             { "count", corner },
                                             { "type", "VEC2" } });
            begin = binary.m_data.size();
            for (const auto& vertex : mesh.m_vertices) {
                for (float v : vertex.m_position) {
                    binary.real(v);
                }
                for (float v : vertex.m_normal) {
                    binary.real(v);
                }
            }
            K_ADD_VIEW(begin, false);
            begin = binary.m_data.size();
            for (const auto& triangle : mesh.m_triangles) {
                for (auto index : triangle) {
                    binary.integer(index);
                }
            }
            K_ADD_VIEW(begin, false);
            begin = binary.m_data.size();
            binary.append(encode_png(image, stop));
            K_ADD_VIEW(begin, false);
            begin = binary.m_data.size();
            for (std::size_t i { 0uz }; i < baked.m_rgba.size(); ++i) {
                binary.integer(baked.m_owners[i].m_value, 8uz);
                binary.integer(baked.m_candidates[i]);
            }
            K_ADD_VIEW(begin, false);
            document["images"].push_back({ { "bufferView", K_VIEW + 3uz }, { "mimeType", "image/png" } });
            document["textures"].push_back({ { "sampler", 0 }, { "source", K_INDEX } });
            document["materials"].push_back(
                { { "name", "baked_appearance" },
                 { "extensions", { { "KHR_materials_unlit", Json::object() } } },
                 { "alphaMode", "MASK" },
                 { "alphaCutoff", 1.0 },
                 { "doubleSided", true },
                 { "pbrMetallicRoughness",
                  { { "baseColorTexture", { { "index", K_INDEX } } },
                   { "metallicFactor", 0 },
                   { "roughnessFactor", 1 } } } });
            document["meshes"].push_back(
                { { "primitives",
                  Json::array({ { { "attributes",
                                 { { "POSITION", K_ACCESSOR }, { "TEXCOORD_0", K_ACCESSOR + 1uz } } },
                                { "material", K_INDEX },
                                { "mode", 4 } } }) } });
            const auto& transform = K_CHUNK->m_world_from_chunk;
            document["nodes"].push_back(
                { { "mesh", K_INDEX },
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
                 { "extras", chunk_header(*K_CHUNK) } });
            nodes.push_back(K_INDEX);
        }
        document["scenes"].push_back({ { "nodes", nodes } });
        document["buffers"].push_back({ { "byteLength", binary.m_data.size() } });
        auto text = document.dump();
        require(text.size() <= 16uz * 1024uz * 1024uz, "GLB JSON exceeds budget");
        while (text.size() % 4uz) {
            text.push_back(' ');
        }
        Writer output;
        output.integer(0x46546c67U);
        output.integer(2);
        output.integer(28uz + text.size() + binary.m_data.size());
        output.integer(text.size());
        output.integer(0x4e4f534aU);
        output.append(std::as_bytes(std::span(text)));
        output.integer(binary.m_data.size());
        output.integer(0x004e4942U);
        output.append(binary.m_data);
        cancelled(stop);
        return std::move(output.m_data);
    } catch (const Cancelled& error) {
        return fail(ErrorCode::CANCELLED, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "GLB export allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
