#include "../scene_store/gltf_internal.hpp"
#include "native_export_internal.hpp"

#include <bit>

namespace world_explorer::native_export_detail {
namespace {

void word(std::vector<std::byte>& out, std::uint32_t value) {
    for (std::size_t index { 0uz }; index < 4uz; ++index) {
        out.push_back(static_cast<std::byte>((value >> (index * 8uz)) & 255U));
    }
}

double scalar(std::vector<std::byte>& out, double value) {
    const float K_FLOAT { static_cast<float>(value) };
    require(
        std::isfinite(K_FLOAT), "Native export float overflow", ErrorCode::UNSUPPORTED_OPERATION);
    word(out, std::bit_cast<std::uint32_t>(K_FLOAT));
    return K_FLOAT;
}

Vec3d vector(std::vector<std::byte>& out, Vec3d value) {
    return { scalar(out, value.m_x), scalar(out, value.m_y), scalar(out, value.m_z) };
}

double length(Vec3d value) {
    return std::sqrt(dot(value, value));
}

Vec3d unit(Vec3d value) {
    return value * (1 / length(value));
}

double srgb(double value) {
    return value <= .0031308 ? 12.92 * value : 1.055 * std::pow(value, 1 / 2.4) - .055;
}

double linear(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}

std::byte quantize(double value) {
    require(std::isfinite(value) && value >= 0 && value <= 1,
            "Native export requires normalized core texture values",
            ErrorCode::UNSUPPORTED_OPERATION);
    return static_cast<std::byte>(static_cast<unsigned>(std::floor(value * 255 + .5)));
}

double decoded(std::byte value) {
    return std::to_integer<unsigned>(value) / 255.0;
}

Json textures(const AdmittedMaterialProduct& material,
              Json& doc,
              Files& files,
              const std::string& prefix,
              Errors& errors,
              const std::stop_token& stop) {
    const auto& selection = material.selection();
    require(selection.m_lighting != AssetLightingMode::CAPTURED_APPEARANCE,
            "Captured radiance requires its separate unlit exporter",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto K_VALUE = [&](MaterialChannelRole role, std::size_t pixel, std::size_t channel) {
        const auto& data = material.channels().at(role);
        return static_cast<double>(data.m_values.at(pixel * data.m_components + channel));
    };
    const auto K_SAMPLER = doc["samplers"].size();
    const auto K_FILTER = selection.m_filter == MaterialFilter::LINEAR ? 9729 : 9728;
    const auto K_WRAP = selection.m_wrap == MaterialWrap::REPEAT ? 10497 : 33071;
    doc["samplers"].push_back(
        { { "magFilter", K_FILTER }, { "minFilter", K_FILTER }, { "wrapS", K_WRAP }, { "wrapT", K_WRAP } });
    const auto K_BEGIN = doc["textures"].size();
    const std::array K_NAMES { "base", "orm", "normal", "emission" };
    auto mappings = Json::array();
    for (std::size_t image { 0uz }; image < 4uz; ++image) {
        gltf_detail::Image pixels { selection.m_width, selection.m_height, {} };
        const auto K_COUNT = static_cast<std::size_t>(selection.m_width) * selection.m_height;
        pixels.m_rgba.reserve(K_COUNT * 4uz);
        for (std::size_t pixel { 0uz }; pixel < K_COUNT; ++pixel) {
            check_stop(stop);
            std::array<std::byte, 4> encoded {
                std::byte {}, std::byte {}, std::byte {}, std::byte { 255 } };
            Vec3d original_normal;
            for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                if (image == 0uz || image == 3uz) {
                    const auto K_ORIGINAL = K_VALUE(image == 0uz ? MaterialChannelRole::BASE_COLOR
                                                                 : MaterialChannelRole::EMISSION,
                                                    pixel,
                                                    channel);
                    require(K_ORIGINAL <= 1,
                            "HDR emission is outside core native export profile",
                            ErrorCode::UNSUPPORTED_OPERATION);
                    encoded[channel] = quantize(srgb(K_ORIGINAL));
                    errors.m_color = std::max(
                        errors.m_color, std::abs(linear(decoded(encoded[channel])) - K_ORIGINAL));
                } else if (image == 1uz) {
                    const std::array K_ROLES { MaterialChannelRole::OCCLUSION,
                                              MaterialChannelRole::ROUGHNESS,
                                              MaterialChannelRole::METALLIC };
                    const auto K_ORIGINAL = K_VALUE(K_ROLES[channel], pixel, 0uz);
                    encoded[channel] = quantize(K_ORIGINAL);
                    errors.m_numeric = std::max(errors.m_numeric,
                                                std::abs(decoded(encoded[channel]) - K_ORIGINAL));
                } else {
                    const auto K_ORIGINAL = K_VALUE(MaterialChannelRole::NORMAL, pixel, channel);
                    encoded[channel] = quantize(K_ORIGINAL * .5 + .5);
                }
            }
            if (image == 2uz) {
                original_normal = { K_VALUE(MaterialChannelRole::NORMAL, pixel, 0uz),
                                   K_VALUE(MaterialChannelRole::NORMAL, pixel, 1uz),
                                   K_VALUE(MaterialChannelRole::NORMAL, pixel, 2uz) };
                require(original_normal.m_z > 0 && decoded(encoded[2uz]) > .5,
                        "Native normal map requires positive tangent Z",
                        ErrorCode::UNSUPPORTED_OPERATION);
                const Vec3d K_DECODED { decoded(encoded[0uz]) * 2 - 1,
                                       decoded(encoded[1uz]) * 2 - 1,
                                       decoded(encoded[2uz]) * 2 - 1 };
                errors.m_normal_map =
                    std::max(errors.m_normal_map, length(unit(K_DECODED) - original_normal));
            }
            pixels.m_rgba.insert(pixels.m_rgba.end(), encoded.begin(), encoded.end());
        }
        const auto K_NAME = "material-" + std::to_string(selection.m_variant.m_value) + "-" +
                            K_NAMES[image] + ".png";
        auto png = gltf_detail::encode_png(pixels, stop);
        mappings.push_back({ { "path", prefix + K_NAME },
                            { "sha256", sha256(png) },
                            { "role", K_NAMES[image] },
                            { "width", selection.m_width },
                            { "height", selection.m_height } });
        files.add(prefix + K_NAME, std::move(png));
        const auto K_IMAGE = doc["images"].size();
        doc["images"].push_back({ { "uri", K_NAME }, { "mimeType", "image/png" } });
        doc["textures"].push_back({ { "source", K_IMAGE }, { "sampler", K_SAMPLER } });
    }
    doc["materials"].push_back({ { "pbrMetallicRoughness",
                                 { { "baseColorTexture", { { "index", K_BEGIN } } },
                                  { "metallicRoughnessTexture", { { "index", K_BEGIN + 1uz } } } } },
                                { "normalTexture", { { "index", K_BEGIN + 2uz } } },
                                { "occlusionTexture", { { "index", K_BEGIN + 1uz } } },
                                { "emissiveTexture", { { "index", K_BEGIN + 3uz } } },
                                { "emissiveFactor", { 1, 1, 1 } },
                                { "alphaMode", "OPAQUE" },
                                { "doubleSided", true } });
    auto channels = Json::array();
    for (const auto& [K_ROLE, K_PIN] : selection.m_channels) {
        channels.push_back({ { "role", static_cast<unsigned>(K_ROLE) },
                            { "product", reference(K_PIN.m_product) },
                            { "provenance", reference(K_PIN.m_provenance) } });
    }
    return { { "variant_id", std::to_string(selection.m_variant.m_value) },
            { "product", reference(selection.m_product) },
            { "lighting",
             selection.m_lighting == AssetLightingMode::ESTIMATED_PBR ? "ESTIMATED_PBR"
                                                                      : "GENERATED_PBR" },
            { "channels", channels },
            { "images", mappings } };
}

}  // namespace

Json document() {
    Json result {
        { "asset", { { "version", "2.0" }, { "generator", "World Explorer native textured profile v1" } } },
        { "scene", 0 } };
    for (auto key : { "scenes",
                     "nodes",
                     "meshes",
                     "materials",
                     "buffers",
                     "bufferViews",
                     "accessors",
                     "images",
                     "textures",
                     "samplers" }) {
        result[key] = Json::array();
    }
    return result;
}

void transform_profile(const SimilarityTransform& value) {
    const auto& q = value.m_rotation;
    require(valid(value) &&
                std::abs(q.m_w * q.m_w + q.m_x * q.m_x + q.m_y * q.m_y + q.m_z * q.m_z - 1) <=
                    1e-12 &&
                std::max({ std::abs(value.m_translation.m_x),
                          std::abs(value.m_translation.m_y),
                          std::abs(value.m_translation.m_z) }) <= 1e6 &&
                value.m_scale >= 1e-4 && value.m_scale <= 1e4,
            "Native export transform profile exceeded",
            ErrorCode::UNSUPPORTED_OPERATION);
}

CompiledAsset compile_asset(const AdmittedAssetDefinition& admitted,
                            const NativeAssetSelection& source,
                            const AdmittedAssetInstances& instances,
                            Files& files,
                            const std::stop_token& stop,
                            std::optional<std::size_t> lod_level) {
    CompiledAsset result { document(), Json::object(), {} };
    auto& doc = result.m_document;
    auto& errors = result.m_errors;
    const auto& definition = admitted.definition();
    const auto& geometry = lod_level.value_or(0uz) == 0uz
                               ? selected(source.m_geometry)
                               : source.m_lods.at(lod_level.value_or(0uz) - 1uz).candidate();
    const auto& mesh = geometry.mesh();
    const auto K_BUFFER = lod_level
                              ? "geometry-lod" + std::to_string(lod_level.value_or(0uz)) + ".bin"
                              : "geometry.bin";
    const auto K_DOCUMENT =
        lod_level ? "lod-" + std::to_string(lod_level.value_or(0uz)) + ".gltf" : "asset.gltf";
    const auto& pivot = definition.m_pivot.m_asset_from_geometry;
    transform_profile(pivot);
    require(mesh.m_uv.size() == mesh.m_positions.size() &&
                mesh.m_tangents.size() == mesh.m_positions.size(),
            "Native export requires full UV and tangent arrays",
            ErrorCode::UNSUPPORTED_OPERATION);
    const auto K_PREFIX = "assets/" + std::to_string(definition.m_asset.m_value) + "/";
    std::vector<std::byte> binary;
    std::vector<Vec3d> positions, normals;
    std::vector<std::array<double, 2>> uv;
    Bounds3d bounds {};
    for (std::size_t vertex { 0uz }; vertex < mesh.m_positions.size(); ++vertex) {
        check_stop(stop);
        const auto K_ORIGINAL = transform_point(pivot, mesh.m_positions[vertex]);
        require(std::max({ std::abs(K_ORIGINAL.m_x),
                          std::abs(K_ORIGINAL.m_y),
                          std::abs(K_ORIGINAL.m_z) }) <= 1e6,
                "Native export coordinate budget exceeded",
                ErrorCode::UNSUPPORTED_OPERATION);
        const auto K_POSITION = vector(binary, K_ORIGINAL);
        const auto K_NORMAL = rotate(pivot.m_rotation, mesh.m_normals[vertex]);
        const auto K_EXPORTED_NORMAL = vector(binary, unit(K_NORMAL));
        const auto& tangent = mesh.m_tangents[vertex];
        const auto K_TANGENT = rotate(pivot.m_rotation, { tangent[0uz], tangent[1uz], tangent[2uz] });
        const auto K_EXPORTED_TANGENT = vector(binary, unit(K_TANGENT));
        scalar(binary, tangent[3uz]);
        errors.m_normal = std::max(errors.m_normal, length(K_EXPORTED_NORMAL - K_NORMAL));
        errors.m_tangent = std::max(errors.m_tangent, length(K_EXPORTED_TANGENT - K_TANGENT));
        require(std::abs(dot(K_EXPORTED_NORMAL, K_EXPORTED_TANGENT)) <= 1e-4,
                "Native export tangent loses orthogonality",
                ErrorCode::UNSUPPORTED_OPERATION);
        std::array converted { scalar(binary, mesh.m_uv[vertex][0uz]),
                              scalar(binary, mesh.m_uv[vertex][1uz]) };
        for (std::size_t axis { 0uz }; axis < 2uz; ++axis) {
            errors.m_uv =
                std::max(errors.m_uv, std::abs(converted[axis] - mesh.m_uv[vertex][axis]));
        }
        positions.push_back(K_POSITION);
        normals.push_back(K_EXPORTED_NORMAL);
        uv.push_back(converted);
        for (const auto& instance : instances.instances().m_instances) {
            if (instance.m_definition == admitted.reference()) {
                const auto K_DELTA = transform_point(instance.m_world_from_asset, K_POSITION) -
                                     transform_point(instance.m_world_from_asset, K_ORIGINAL);
                require(finite(K_DELTA), "Native export world error overflow");
                errors.m_position = std::max(errors.m_position, length(K_DELTA));
            }
        }
        if (vertex == 0uz) {
            bounds = { K_POSITION, K_POSITION };
        } else {
            bounds.m_minimum = { std::min(bounds.m_minimum.m_x, K_POSITION.m_x),
                                std::min(bounds.m_minimum.m_y, K_POSITION.m_y),
                                std::min(bounds.m_minimum.m_z, K_POSITION.m_z) };
            bounds.m_maximum = { std::max(bounds.m_maximum.m_x, K_POSITION.m_x),
                                std::max(bounds.m_maximum.m_y, K_POSITION.m_y),
                                std::max(bounds.m_maximum.m_z, K_POSITION.m_z) };
        }
    }
    doc["bufferViews"].push_back({ { "buffer", 0 },
                                  { "byteOffset", 0 },
                                  { "byteLength", binary.size() },
                                  { "byteStride", 48 },
                                  { "target", 34962 } });
    const std::array K_TYPES { "VEC3", "VEC3", "VEC4", "VEC2" };
    const std::array K_OFFSETS { 0, 12, 24, 40 };
    for (std::size_t attribute { 0uz }; attribute < 4uz; ++attribute) {
        Json accessor { { "bufferView", 0 },
                       { "byteOffset", K_OFFSETS[attribute] },
                       { "componentType", 5126 },
                       { "count", positions.size() },
                       { "type", K_TYPES[attribute] } };
        if (attribute == 0uz) {
            accessor["min"] = xyz(bounds.m_minimum);
            accessor["max"] = xyz(bounds.m_maximum);
        }
        doc["accessors"].push_back(accessor);
    }
    auto primitives = Json::array(), groups = Json::array(), materials = Json::array();
    for (const auto& binding : definition.m_materials) {
        const auto K_MATERIAL = std::ranges::find_if(source.m_materials, [&](const auto& item) {
            return item.selection().m_product == binding.m_product;
        });
        require(K_MATERIAL != source.m_materials.end(), "Native export material missing");
        const auto K_MATERIAL_INDEX = doc["materials"].size();
        materials.push_back(textures(*K_MATERIAL, doc, files, K_PREFIX, errors, stop));
        const auto K_OFFSET = binary.size();
        auto faces = Json::array();
        for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
            if (mesh.m_groups[face] != binding.m_variant) {
                continue;
            }
            check_stop(stop);
            const auto& indices = mesh.m_triangles[face];
            const auto K_CROSS = cross(positions[indices[1uz]] - positions[indices[0uz]],
                                       positions[indices[2uz]] - positions[indices[0uz]]);
            const auto& a = uv[indices[0uz]];
            const auto& b = uv[indices[1uz]];
            const auto& c = uv[indices[2uz]];
            const auto K_AREA =
                (b[0uz] - a[0uz]) * (c[1uz] - a[1uz]) - (b[1uz] - a[1uz]) * (c[0uz] - a[0uz]);
            require(finite(K_CROSS) && std::isfinite(dot(K_CROSS, K_CROSS)) &&
                        dot(K_CROSS, K_CROSS) > 0 && std::isfinite(K_AREA) && K_AREA != 0,
                    "Native export float conversion collapses triangle/UV",
                    ErrorCode::UNSUPPORTED_OPERATION);
            for (auto index : indices) {
                require(dot(K_CROSS, normals[index]) > 0,
                        "Native export winding disagrees with normals");
                word(binary, index);
            }
            faces.push_back(face);
        }
        const auto K_VIEW = doc["bufferViews"].size(), K_ACCESSOR = doc["accessors"].size();
        doc["bufferViews"].push_back({ { "buffer", 0 },
                                      { "byteOffset", K_OFFSET },
                                      { "byteLength", binary.size() - K_OFFSET },
                                      { "target", 34963 } });
        doc["accessors"].push_back({ { "bufferView", K_VIEW },
                                    { "componentType", 5125 },
                                    { "count", faces.size() * 3uz },
                                    { "type", "SCALAR" } });
        primitives.push_back(
            { { "attributes", { { "POSITION", 0 }, { "NORMAL", 1 }, { "TANGENT", 2 }, { "TEXCOORD_0", 3 } } },
             { "indices", K_ACCESSOR },
             { "material", K_MATERIAL_INDEX },
             { "mode", 4 } });
        groups.push_back(
            { { "variant_id", std::to_string(binding.m_variant.m_value) }, { "source_faces", faces } });
    }
    doc["buffers"].push_back({ { "uri", K_BUFFER }, { "byteLength", binary.size() } });
    doc["meshes"].push_back({ { "primitives", primitives } });
    doc["nodes"].push_back({ { "mesh", 0 } });
    doc["scenes"].push_back({ { "nodes", { 0 } } });
    doc["scenes"][0uz]["name"] = selected(source.m_geometry).synthetic_source()
                                     ? "World Explorer synthetic diagnostic"
                                     : "World Explorer unqualified asset draft";
    doc["extras"] = { { "profile", K_PROFILE },
                     { "synthetic_diagnostic", selected(source.m_geometry).synthetic_source() },
                     { "qualification", "UNQUALIFIED_EXPORT" } };
    if (lod_level) {
        doc["extras"]["profile"] = K_LOD_PROFILE;
        doc["extras"]["lod_level"] = lod_level.value_or(0uz);
    }
    result.m_mapping = { { "definition", reference(admitted.reference()) },
                        { "geometry", reference(geometry.selection().m_product) },
                        { "geometry_path", K_PREFIX + K_BUFFER },
                        { "geometry_sha256", sha256(binary) },
                        { "asset_path", K_PREFIX + K_DOCUMENT },
                        { "groups", groups },
                        { "materials", materials } };
    files.add(K_PREFIX + K_BUFFER, std::move(binary));
    files.json(K_PREFIX + K_DOCUMENT, doc);
    return result;
}

void append_resources(Json& assembly, Json asset, const std::string& prefix) {
    const auto K_BUFFER = assembly["buffers"].size(), K_VIEW = assembly["bufferViews"].size();
    const auto K_ACCESSOR = assembly["accessors"].size(), K_MATERIAL = assembly["materials"].size();
    const auto K_IMAGE = assembly["images"].size(), K_TEXTURE = assembly["textures"].size();
    const auto K_SAMPLER = assembly["samplers"].size();
    for (auto& row : asset["buffers"]) {
        row["uri"] = prefix + row["uri"].get<std::string>();
    }
    for (auto& row : asset["bufferViews"]) {
        row["buffer"] = row["buffer"].get<std::size_t>() + K_BUFFER;
    }
    for (auto& row : asset["accessors"]) {
        row["bufferView"] = row["bufferView"].get<std::size_t>() + K_VIEW;
    }
    for (auto& row : asset["images"]) {
        row["uri"] = prefix + row["uri"].get<std::string>();
    }
    for (auto& row : asset["textures"]) {
        row["source"] = row["source"].get<std::size_t>() + K_IMAGE;
        row["sampler"] = row["sampler"].get<std::size_t>() + K_SAMPLER;
    }
    for (auto& row : asset["materials"]) {
        for (auto key : { "normalTexture", "occlusionTexture", "emissiveTexture" }) {
            row[key]["index"] = row[key]["index"].get<std::size_t>() + K_TEXTURE;
        }
        for (auto key : { "baseColorTexture", "metallicRoughnessTexture" }) {
            row["pbrMetallicRoughness"][key]["index"] =
                row["pbrMetallicRoughness"][key]["index"].get<std::size_t>() + K_TEXTURE;
        }
    }
    for (auto& mesh : asset["meshes"]) {
        for (auto& row : mesh["primitives"]) {
            for (auto& accessor : row["attributes"]) {
                accessor = accessor.get<std::size_t>() + K_ACCESSOR;
            }
            row["indices"] = row["indices"].get<std::size_t>() + K_ACCESSOR;
            row["material"] = row["material"].get<std::size_t>() + K_MATERIAL;
        }
    }
    for (auto key : { "buffers",
                     "bufferViews",
                     "accessors",
                     "images",
                     "textures",
                     "samplers",
                     "materials",
                     "meshes" }) {
        for (auto& row : asset[key]) {
            assembly[key].push_back(std::move(row));
        }
    }
}

}  // namespace world_explorer::native_export_detail
