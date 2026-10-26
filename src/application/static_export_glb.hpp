#pragma once

#include "../core/json_records.hpp"
#include "world_explorer/application/static_export.hpp"
#include "world_explorer/core/hash.hpp"

#include <bit>
#include <map>
#include <span>

namespace world_explorer::static_export_detail {

using namespace json_record_detail;

struct EncodedStaticGlb {
    std::vector<std::byte> m_bytes;
    Json m_assets = Json::array(), m_nodes = Json::array();
    double m_position_error {}, m_normal_error {};
};

inline void word(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (std::size_t index { 0uz }; index < 4uz; ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (8uz * index)) & 255U));
    }
}

inline double scalar(std::vector<std::byte>& bytes, double value) {
    const float K_FLOAT { static_cast<float>(value) };
    require(std::isfinite(K_FLOAT),
            "Static GLB float32 conversion overflow",
            ErrorCode::UNSUPPORTED_OPERATION);
    word(bytes, std::bit_cast<std::uint32_t>(K_FLOAT));
    return K_FLOAT;
}

inline Vec3d vector(std::vector<std::byte>& bytes, Vec3d value) {
    return { scalar(bytes, value.m_x), scalar(bytes, value.m_y), scalar(bytes, value.m_z) };
}

inline Json xyz(Vec3d value) {
    return { value.m_x, value.m_y, value.m_z };
}

inline Json definition_ref(const AssetDefinitionRef& ref) {
    return { { "asset_id", std::to_string(ref.m_asset.m_value) },
            { "revision", std::to_string(ref.m_revision) },
            { "sha256", ref.m_sha256 } };
}

inline Json product_ref(const AssetRevisionRef& ref) {
    return { { "product_id", std::to_string(ref.m_product.m_value) },
            { "revision", std::to_string(ref.m_revision) },
            { "content_sha256", ref.m_content_sha256 } };
}

inline void transform_profile(const SimilarityTransform& transform) {
    const auto& q = transform.m_rotation;
    require(valid(transform) &&
                std::abs(q.m_w * q.m_w + q.m_x * q.m_x + q.m_y * q.m_y + q.m_z * q.m_z - 1) <=
                    1e-12 &&
                std::max({ std::abs(transform.m_translation.m_x),
                          std::abs(transform.m_translation.m_y),
                          std::abs(transform.m_translation.m_z) }) <= 1e6 &&
                transform.m_scale >= 1e-4 && transform.m_scale <= 1e4,
            "Unsupported static export transform range/rotation",
            ErrorCode::UNSUPPORTED_OPERATION);
}

inline EncodedStaticGlb encode_static_glb(const AdmittedAssetInstances& source,
                                          const std::vector<StaticAssetPayloads>& payloads,
                                          const StaticExportSource& policy,
                                          const std::stop_token& stop) {
    EncodedStaticGlb result;
    Json doc { { "asset",
               { { "version", "2.0" }, { "generator", "World Explorer generated constant profile v1" } } },
              { "scene", 0 },
              { "scenes", Json::array() },
              { "nodes", Json::array() },
              { "meshes", Json::array() },
              { "materials", Json::array() },
              { "buffers", Json::array() },
              { "bufferViews", Json::array() },
              { "accessors", Json::array() },
              { "extras",
               { { "profile", "WE_GENERATED_CONSTANT_GLB_V1" },
                { "source_instances_sha256", source.sha256() },
                { "lighting", "GENERATED_PBR_UNQUALIFIED" } } } };
    std::vector<std::byte> binary;
    std::map<AssetId, std::size_t> resources;
    for (std::size_t asset { 0uz }; asset < payloads.size(); ++asset) {
        check_stop(stop);
        const auto& admitted = source.definitions()[asset];
        const auto& definition = admitted.definition();
        const auto& payload = payloads[asset];
        const auto& mesh = payload.m_mesh;
        const auto& material = payload.m_material;
        transform_profile(definition.m_pivot.m_asset_from_geometry);
        require(material.m_occlusion == 1 && std::max({ material.m_emission.m_x,
                                                       material.m_emission.m_y,
                                                       material.m_emission.m_z }) <= 1,
                "Static GLB requires unit occlusion and core emission <=1",
                ErrorCode::UNSUPPORTED_OPERATION);
        resources.emplace(definition.m_asset, asset);
        const auto K_OFFSET = binary.size();
        const auto K_VIEW = doc["bufferViews"].size();
        const auto K_ACCESSOR = doc["accessors"].size();
        std::vector<Vec3d> positions, normals;
        std::vector<std::array<double, 2>> uv;
        positions.reserve(mesh.m_positions.size());
        normals.reserve(mesh.m_positions.size());
        uv.reserve(mesh.m_positions.size());
        Bounds3d bounds {};
        for (std::size_t vertex { 0uz }; vertex < mesh.m_positions.size(); ++vertex) {
            const auto K_ORIGINAL =
                transform_point(definition.m_pivot.m_asset_from_geometry, mesh.m_positions[vertex]);
            require(std::max({ std::abs(K_ORIGINAL.m_x),
                              std::abs(K_ORIGINAL.m_y),
                              std::abs(K_ORIGINAL.m_z) }) <= 1e6,
                    "Static export local coordinate budget exceeded",
                    ErrorCode::UNSUPPORTED_OPERATION);
            const auto K_POSITION = vector(binary, K_ORIGINAL);
            const auto K_ROTATED =
                rotate(definition.m_pivot.m_asset_from_geometry.m_rotation, mesh.m_normals[vertex]);
            const auto K_NORMALIZED = K_ROTATED * (1 / std::sqrt(dot(K_ROTATED, K_ROTATED)));
            const auto K_NORMAL = vector(binary, K_NORMALIZED);
            const auto K_DELTA = K_NORMAL - K_ROTATED;
            result.m_normal_error =
                std::max(result.m_normal_error, std::sqrt(dot(K_DELTA, K_DELTA)));
            uv.push_back(
                { scalar(binary, mesh.m_uv[vertex][0uz]), scalar(binary, mesh.m_uv[vertex][1uz]) });
            positions.push_back(K_POSITION);
            normals.push_back(K_NORMAL);
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
        const auto K_INDEX_OFFSET = binary.size();
        for (const auto& face : mesh.m_triangles) {
            const auto K_CROSS = cross(positions[face[1uz]] - positions[face[0uz]],
                                       positions[face[2uz]] - positions[face[0uz]]);
            require(finite(K_CROSS) && dot(K_CROSS, K_CROSS) > 0 &&
                        std::isfinite(dot(K_CROSS, K_CROSS)),
                    "Static GLB float32 conversion collapses a face");
            const auto& a = uv[face[0uz]];
            const auto& b = uv[face[1uz]];
            const auto& c = uv[face[2uz]];
            require((b[0uz] - a[0uz]) * (c[1uz] - a[1uz]) - (b[1uz] - a[1uz]) * (c[0uz] - a[0uz]) !=
                        0,
                    "Static GLB float32 UV collapse");
            for (auto vertex : face) {
                require(dot(K_CROSS, normals[vertex]) > 0, "Static GLB float32 winding differs");
                word(binary, vertex);
            }
        }
        const Json K_MATERIAL { { "pbrMetallicRoughness",
                                { { "baseColorFactor",
                                  { material.m_base_color.m_x,
                                   material.m_base_color.m_y,
                                   material.m_base_color.m_z,
                                   1 } },
                                 { "metallicFactor", material.m_metallic },
                                 { "roughnessFactor", material.m_roughness } } },
                               { "emissiveFactor", xyz(material.m_emission) },
                               { "alphaMode", "OPAQUE" },
                               { "doubleSided", true } };
        doc["materials"].push_back(K_MATERIAL);
        doc["bufferViews"].push_back({ { "buffer", 0 },
                                      { "byteOffset", K_OFFSET },
                                      { "byteLength", K_INDEX_OFFSET - K_OFFSET },
                                      { "byteStride", 32 },
                                      { "target", 34962 } });
        doc["bufferViews"].push_back({ { "buffer", 0 },
                                      { "byteOffset", K_INDEX_OFFSET },
                                      { "byteLength", binary.size() - K_INDEX_OFFSET },
                                      { "target", 34963 } });
        doc["accessors"].push_back({ { "bufferView", K_VIEW },
                                    { "byteOffset", 0 },
                                    { "componentType", 5126 },
                                    { "count", positions.size() },
                                    { "type", "VEC3" },
                                    { "min", xyz(bounds.m_minimum) },
                                    { "max", xyz(bounds.m_maximum) } });
        doc["accessors"].push_back({ { "bufferView", K_VIEW },
                                    { "byteOffset", 12 },
                                    { "componentType", 5126 },
                                    { "count", normals.size() },
                                    { "type", "VEC3" } });
        doc["accessors"].push_back({ { "bufferView", K_VIEW },
                                    { "byteOffset", 24 },
                                    { "componentType", 5126 },
                                    { "count", uv.size() },
                                    { "type", "VEC2" } });
        doc["accessors"].push_back({ { "bufferView", K_VIEW + 1uz },
                                    { "byteOffset", 0 },
                                    { "componentType", 5125 },
                                    { "count", mesh.m_triangles.size() * 3uz },
                                    { "type", "SCALAR" } });
        doc["meshes"].push_back({ { "primitives",
                                  Json::array({ { { "attributes",
                                                 { { "POSITION", K_ACCESSOR },
                                                  { "NORMAL", K_ACCESSOR + 1uz },
                                                  { "TEXCOORD_0", K_ACCESSOR + 2uz } } },
                                                { "indices", K_ACCESSOR + 3uz },
                                                { "material", asset },
                                                { "mode", 4 } } }) } });
        const auto K_MATERIAL_TEXT = K_MATERIAL.dump();
        result.m_assets.push_back(
            { { "definition", definition_ref(admitted.reference()) },
             { "geometry", product_ref(definition.m_geometry) },
             { "material", product_ref(definition.m_materials.front().m_product) },
             { "variant", std::to_string(mesh.m_variant.m_value) },
             { "mesh_index", asset },
             { "material_index", asset },
             { "geometry_bytes_sha256",
              sha256(std::span(binary).subspan(K_OFFSET, binary.size() - K_OFFSET)) },
             { "material_json_sha256", sha256(std::as_bytes(std::span(K_MATERIAL_TEXT))) },
             { "vertex_count", positions.size() },
             { "triangle_count", mesh.m_triangles.size() } });
        for (const auto& instance : source.instances().m_instances) {
            if (instance.m_definition.m_asset != definition.m_asset) {
                continue;
            }
            transform_profile(instance.m_world_from_asset);
            for (std::size_t vertex { 0uz }; vertex < positions.size(); ++vertex) {
                const auto K_ORIGINAL =
                    transform_point(instance.m_world_from_asset,
                                    transform_point(definition.m_pivot.m_asset_from_geometry,
                                                    mesh.m_positions[vertex]));
                const auto K_EXPORTED =
                    transform_point(instance.m_world_from_asset, positions[vertex]);
                const auto K_ERROR = K_EXPORTED - K_ORIGINAL;
                require(finite(K_ERROR) && std::isfinite(dot(K_ERROR, K_ERROR)),
                        "Static GLB world error overflow");
                result.m_position_error =
                    std::max(result.m_position_error, std::sqrt(dot(K_ERROR, K_ERROR)));
            }
        }
    }
    Json scene_nodes = Json::array();
    for (const auto& instance : source.instances().m_instances) {
        check_stop(stop);
        require(instance.m_visible && !instance.m_room && !instance.m_support &&
                    instance.m_lineage.empty(),
                "Unsupported static export visibility/scene relations",
                ErrorCode::UNSUPPORTED_OPERATION);
        const auto K_MESH = resources.at(instance.m_definition.m_asset);
        const auto& transform = instance.m_world_from_asset;
        const auto& q = transform.m_rotation;
        const auto K_NODE = doc["nodes"].size();
        const Json K_TRS { { "translation", xyz(transform.m_translation) },
                          { "rotation", { q.m_x, q.m_y, q.m_z, q.m_w } },
                          { "scale", { transform.m_scale, transform.m_scale, transform.m_scale } } };
        auto node = K_TRS;
        node["mesh"] = K_MESH;
        doc["nodes"].push_back(node);
        scene_nodes.push_back(K_NODE);
        result.m_nodes.push_back({ { "instance_id", std::to_string(instance.m_instance.m_value) },
                                  { "definition", definition_ref(instance.m_definition) },
                                  { "node_index", K_NODE },
                                  { "mesh_index", K_MESH },
                                  { "trs", K_TRS } });
    }
    require(result.m_position_error <= policy.m_max_position_error_units &&
                result.m_normal_error <= policy.m_max_normal_error,
            "Static GLB exceeds independently selected conversion-error policy");
    doc["scenes"].push_back({ { "nodes", scene_nodes } });
    doc["buffers"].push_back({ { "byteLength", binary.size() } });
    auto text = doc.dump();
    while (text.size() % 4uz) {
        text.push_back(' ');
    }
    const auto K_TOTAL = 28uz + text.size() + binary.size();
    require(K_TOTAL <= 1024uz * 1024uz,
            "Static GLB encoded budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    word(result.m_bytes, 0x46546c67U);
    word(result.m_bytes, 2);
    word(result.m_bytes, static_cast<std::uint32_t>(K_TOTAL));
    word(result.m_bytes, static_cast<std::uint32_t>(text.size()));
    word(result.m_bytes, 0x4e4f534aU);
    const auto K_TEXT_BYTES = std::as_bytes(std::span(text));
    result.m_bytes.insert(result.m_bytes.end(), K_TEXT_BYTES.begin(), K_TEXT_BYTES.end());
    word(result.m_bytes, static_cast<std::uint32_t>(binary.size()));
    word(result.m_bytes, 0x004e4942U);
    result.m_bytes.insert(result.m_bytes.end(), binary.begin(), binary.end());
    return result;
}

}  // namespace world_explorer::static_export_detail
