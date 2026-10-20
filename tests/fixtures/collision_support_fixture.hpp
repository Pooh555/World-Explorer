#pragma once

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <nlohmann/json.hpp>

namespace world_explorer::testing {

inline std::string collision_digest(std::string_view text) {
    return sha256(std::as_bytes(std::span(text.data(), text.size())));
}

inline SceneState collision_source() {
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
    source.m_chunks.emplace(chunk->m_id, chunk);
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
          { "metres", 8 },
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

}  // namespace world_explorer::testing
