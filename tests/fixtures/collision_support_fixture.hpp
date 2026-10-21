#pragma once

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <nlohmann/json.hpp>

namespace world_explorer::testing {

inline std::string collision_digest(std::string_view text) {
    return sha256(std::as_bytes(std::span(text.data(), text.size())));
}

inline SceneState collision_source(bool with_wall = false,
                                   bool duplicate_wall = false,
                                   std::string_view wall_face = "NEG_X",
                                   float rise_x = 0,
                                   float rise_z = 0,
                                   float step_height = 0) {
    SceneState source;
    source.m_id = { 39 };
    source.m_epoch = { 7 };
    source.m_revision = { 13 };
    source.m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "flat_collision_grid_v1" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 9007199254740993ULL };
    chunk->m_pose_revision = { 4 };
    chunk->m_geometry_valid = true;
    chunk->m_bounds = { {}, { 8, 0, 8 } };
    chunk->m_mesh = SceneMesh { { { { 0, 0, 0 }, { 0, 1, 0 } },
                                { { 8, 0, 0 }, { 0, 1, 0 } },
                                { { 0, 0, 8 }, { 0, 1, 0 } },
                                { { 8, 0, 8 }, { 0, 1, 0 } } },
                               { { 0, 2, 1 }, { 1, 2, 3 } },
                               {} };
    if (step_height > 0) {
        chunk->m_bounds.m_maximum.m_x = 4;
        chunk->m_mesh->m_vertices[1uz].m_position[0uz] = 4;
        chunk->m_mesh->m_vertices[3uz].m_position[0uz] = 4;
        auto landing = std::make_shared<SceneChunk>();
        landing->m_id = { 9007199254740995ULL };
        landing->m_pose_revision = { 4 };
        landing->m_geometry_valid = true;
        landing->m_bounds = { { 4, step_height, 0 }, { 8, step_height, 8 } };
        landing->m_mesh = SceneMesh { { { { 4, step_height, 0 }, { 0, 1, 0 } },
                                      { { 8, step_height, 0 }, { 0, 1, 0 } },
                                      { { 4, step_height, 8 }, { 0, 1, 0 } },
                                      { { 8, step_height, 8 }, { 0, 1, 0 } } },
                                     { { 0, 2, 1 }, { 1, 2, 3 } },
                                     {} };
        source.m_chunks.emplace(landing->m_id, landing);
        auto riser = std::make_shared<SceneChunk>();
        riser->m_id = { 9007199254740996ULL };
        riser->m_pose_revision = { 4 };
        riser->m_geometry_valid = true;
        riser->m_bounds = { { 4, 0, 0 }, { 4, step_height, 8 } };
        riser->m_mesh = SceneMesh { { { { 4, 0, 0 }, { -1, 0, 0 } },
                                    { { 4, step_height, 0 }, { -1, 0, 0 } },
                                    { { 4, 0, 8 }, { -1, 0, 0 } },
                                    { { 4, step_height, 8 }, { -1, 0, 0 } } },
                                   { { 0, 2, 1 }, { 1, 2, 3 } },
                                   {} };
        source.m_chunks.emplace(riser->m_id, riser);
        source.m_provenance["fixture"] = "selected_step_collision_v4";
    }
    source.m_chunks.emplace(chunk->m_id, chunk);
    if (with_wall) {
        auto wall = std::make_shared<SceneChunk>();
        wall->m_id = { 9007199254740994ULL };
        wall->m_pose_revision = { 4 };
        wall->m_geometry_valid = true;
        wall->m_bounds = { { 3, 0, 1 }, { 3, 1, 2 } };
        wall->m_mesh = SceneMesh { { { { 3, 0, 1 }, { -1, 0, 0 } },
                                   { { 3, 1, 1 }, { -1, 0, 0 } },
                                   { { 3, 0, 2 }, { -1, 0, 0 } },
                                   { { 3, 1, 2 }, { -1, 0, 0 } } },
                                  { { 0, 2, 1 },
                                   duplicate_wall ? std::array<std::uint32_t, 3uz> { 0, 2, 1 }
                                                  : std::array<std::uint32_t, 3uz> { 1, 2, 3 } },
                                  {} };
        if (wall_face == "POS_X") {
            wall->m_bounds = { { 4, 0, 1 }, { 4, 1, 2 } };
            wall->m_mesh = SceneMesh { { { { 4, 0, 1 }, { 1, 0, 0 } },
                                       { { 4, 1, 1 }, { 1, 0, 0 } },
                                       { { 4, 0, 2 }, { 1, 0, 0 } },
                                       { { 4, 1, 2 }, { 1, 0, 0 } } },
                                      { { 0, 1, 2 }, { 1, 3, 2 } },
                                      {} };
        } else if (wall_face == "NEG_Z" || wall_face == "POS_Z") {
            const float K_Z { wall_face == "NEG_Z" ? 1.F : 2.F };
            const float K_NORMAL_Z { wall_face == "NEG_Z" ? -1.F : 1.F };
            wall->m_bounds = { { 3, 0, K_Z }, { 4, 1, K_Z } };
            wall->m_mesh =
                SceneMesh { { { { 3, 0, K_Z }, { 0, 0, K_NORMAL_Z } },
                            { { 4, 0, K_Z }, { 0, 0, K_NORMAL_Z } },
                            { { 3, 1, K_Z }, { 0, 0, K_NORMAL_Z } },
                            { { 4, 1, K_Z }, { 0, 0, K_NORMAL_Z } } },
                           wall_face == "NEG_Z"
                               ? std::vector<std::array<std::uint32_t, 3uz>> { { 0, 2, 1 }, { 1, 2, 3 } }
                               : std::vector<std::array<std::uint32_t, 3uz>> { { 0, 1, 2 }, { 1, 3, 2 } },
                           {} };
        } else if (wall_face != "NEG_X") {
            throw std::runtime_error("Unknown synthetic wall face");
        }
        source.m_chunks.emplace(wall->m_id, wall);
    }
    if (rise_x != 0 || rise_z != 0) {
        source.m_provenance["fixture"] = "inclined_collision_grid_v3";
        for (auto& [id, owner] : source.m_chunks) {
            static_cast<void>(id);
            auto inclined = std::make_shared<SceneChunk>(*owner);
            if (!inclined->m_mesh) {
                throw std::runtime_error("Expected synthetic mesh");
            }
            bool first { true };
            for (auto& vertex : inclined->m_mesh->m_vertices) {
                vertex.m_position[1uz] +=
                    rise_x * vertex.m_position[0uz] + rise_z * vertex.m_position[2uz];
                auto& normal = vertex.m_normal;
                normal[0uz] -= rise_x * normal[1uz];
                normal[2uz] -= rise_z * normal[1uz];
                const float K_LENGTH { std::sqrt(normal[0uz] * normal[0uz] +
                                                normal[1uz] * normal[1uz] +
                                                normal[2uz] * normal[2uz]) };
                for (auto& component : normal) {
                    component /= K_LENGTH;
                }
                const Vec3d K_POINT {
                    vertex.m_position[0uz], vertex.m_position[1uz], vertex.m_position[2uz] };
                if (first) {
                    inclined->m_bounds = { K_POINT, K_POINT };
                    first = false;
                } else {
                    auto& low = inclined->m_bounds.m_minimum;
                    auto& high = inclined->m_bounds.m_maximum;
                    low = { std::min(low.m_x, K_POINT.m_x),
                           std::min(low.m_y, K_POINT.m_y),
                           std::min(low.m_z, K_POINT.m_z) };
                    high = { std::max(high.m_x, K_POINT.m_x),
                            std::max(high.m_y, K_POINT.m_y),
                            std::max(high.m_z, K_POINT.m_z) };
                }
            }
            owner = std::move(inclined);
        }
    }
    const auto K_ANCHOR = [&](std::array<double, 3uz> weights) {
        return nlohmann::json {
            { "chunk_id", chunk->m_id.m_value }, { "triangle", 0U }, { "barycentric", weights } };
    };
    const nlohmann::json K_FRAME {
        { "schema", "WORLD_FRAME_ALIGNMENT_V1" },
        { "source_sha256", scene_content_hash(source) },
        { "source_epoch", source.m_epoch.m_value },
        { "source_revision", source.m_revision.m_value },
        { "evidence_kind", "SYNTHETIC_DIAGNOSTIC" },
        { "distance",
         { { "a", K_ANCHOR({ 1, 0, 0 }) },
          { "b", K_ANCHOR({ 0, 0, 1 }) },
          { "metres",
           step_height > 0 ? nlohmann::json(4)
           : rise_x == 0 ? nlohmann::json(8)
                         : nlohmann::json(std::hypot(8.0, static_cast<double>(8.F * rise_x), 0.0)) },
          { "uncertainty_metres", 0 },
          { "method", "SYNTHETIC" },
          { "record", "Analytic synthetic length; no physical measurement" } } },
        { "up",
         { { "direction", { 0, 1, 0 } },
          { "uncertainty_degrees", 0 },
          { "method", "SYNTHETIC" },
          { "record", "Analytic synthetic up" } } },
        { "origin", K_ANCHOR({ 1, 0, 0 }) } };
    const auto K_TEXT = K_FRAME.dump();
    const auto K_ADMITTED = admit_world_frame(source, K_TEXT, collision_digest(K_TEXT));
    if (!K_ADMITTED) {
        throw std::runtime_error(K_ADMITTED.error().m_message);
    }
    source.m_world_frame = *K_ADMITTED;
    ++source.m_epoch.m_value;
    ++source.m_revision.m_value;
    return source;
}

inline nlohmann::json collision_document(const SceneState& source) {
    if (!source.m_world_frame) {
        throw std::runtime_error("Expected aligned collision fixture");
    }
    const auto& chunk = *source.m_chunks.begin()->second;
    return { { "schema", "SYNTHETIC_COLLISION_SUPPORT_V1" },
            { "scene_sha256", scene_content_hash(source) },
            { "epoch", source.m_epoch.m_value },
            { "revision", source.m_revision.m_value },
            { "world_frame_sha256", source.m_world_frame->m_document_sha256 },
            { "grid",
             { { "origin", { 0, 0, 0 } },
              { "cell_size", 1 },
              { "dimensions", { 8U, 4U, 8U } },
              { "states", std::string(256uz, '.') } } },
            { "floors",
             nlohmann::json::array({ { { "chunk_id", chunk.m_id.m_value },
                                     { "generation", chunk.m_generation },
                                     { "pose_revision", chunk.m_pose_revision.m_value },
                                     { "chunk_sha256", scene_chunk_content_hash(chunk) },
                                     { "triangles", { 0U, 1U } } } }) } };
}

inline nlohmann::json collision_wall_document(const SceneState& source,
                                              std::string_view face = "NEG_X") {
    auto document = collision_document(source);
    document["schema"] = "SYNTHETIC_COLLISION_SUPPORT_V2";
    auto& cells = document["grid"]["states"].get_ref<std::string&>();
    cells[3uz + 8uz * (0uz + 4uz * 1uz)] = '#';
    const auto K_FOUND = source.m_chunks.find(ChunkId { 9007199254740994ULL });
    if (K_FOUND == source.m_chunks.end()) {
        throw std::runtime_error("Expected synthetic wall chunk");
    }
    const auto& wall = *K_FOUND->second;
    document["walls"] = nlohmann::json::array({ { { "chunk_id", wall.m_id.m_value },
                                                { "generation", wall.m_generation },
                                                { "pose_revision", wall.m_pose_revision.m_value },
                                                { "chunk_sha256", scene_chunk_content_hash(wall) },
                                                { "occupied_cell", { 3U, 0U, 1U } },
                                                { "face", face },
                                                { "triangles", { 0U, 1U } } } });
    return document;
}

inline nlohmann::json collision_ramp_document(const SceneState& source,
                                              double rise_x,
                                              double rise_z,
                                              bool with_wall = false,
                                              std::string_view face = "NEG_X") {
    auto document = with_wall ? collision_wall_document(source, face) : collision_document(source);
    document["schema"] = "SYNTHETIC_COLLISION_SUPPORT_V3";
    document["plane"] = { { "rise_x", rise_x }, { "rise_z", rise_z } };
    if (!with_wall) {
        document["walls"] = nlohmann::json::array();
    }
    return document;
}

inline nlohmann::json collision_step_document(const SceneState& source, double height) {
    auto document = collision_document(source);
    document["schema"] = "SYNTHETIC_COLLISION_SUPPORT_V4";
    document["walls"] = nlohmann::json::array();
    const auto K_LANDING = source.m_chunks.find(ChunkId { 9007199254740995ULL });
    const auto K_RISER = source.m_chunks.find(ChunkId { 9007199254740996ULL });
    if (K_LANDING == source.m_chunks.end() || K_RISER == source.m_chunks.end()) {
        throw std::runtime_error("Expected synthetic step chunks");
    }
    const auto& landing = *K_LANDING->second;
    const auto& riser = *K_RISER->second;
    document["floors"].push_back({ { "chunk_id", landing.m_id.m_value },
                                  { "generation", landing.m_generation },
                                  { "pose_revision", landing.m_pose_revision.m_value },
                                  { "chunk_sha256", scene_chunk_content_hash(landing) },
                                  { "triangles", { 0U, 1U } } });
    document["step"] = { { "boundary_x", 4U },
                        { "height", height },
                        { "riser",
                         { { "chunk_id", riser.m_id.m_value },
                          { "generation", riser.m_generation },
                          { "pose_revision", riser.m_pose_revision.m_value },
                          { "chunk_sha256", scene_chunk_content_hash(riser) },
                          { "triangles", { 0U, 1U } } } } };
    return document;
}

inline double
collision_ramp_feet_y(double x, double z, double rise_x, double rise_z, double radius = .25) {
    return rise_x * x + rise_z * z +
           (radius + static_cast<double>(.005F)) *
               (std::sqrt(1 + rise_x * rise_x + rise_z * rise_z) - 1) +
           1e-6;
}

}  // namespace world_explorer::testing
