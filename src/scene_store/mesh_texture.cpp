#include "world_explorer/scene_store/mesh_texture.hpp"

#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <new>

namespace world_explorer {
namespace {

constexpr std::size_t K_TILE { 8uz }, K_SAMPLES { 36uz };

struct Accumulator {
    std::array<double, 3> m_rgb {};
    double m_weight {}, m_best_score {};
    FrameId m_owner {};
    std::array<std::uint32_t, 4uz> m_candidates {};
};

}  // namespace

struct MeshTextureBaker::State {
    SceneChunk m_chunk;
    MeshTextureProfile m_profile { MeshTextureProfile::SINGLE_CACHE };
    std::size_t m_columns {}, m_rows {}, m_views {};
    std::vector<Accumulator> m_samples;
    bool m_finished = false;
    bool m_failed = false;
};

MeshTextureBaker::MeshTextureBaker(std::unique_ptr<State> state) : m_state(std::move(state)) { }

MeshTextureBaker::~MeshTextureBaker() = default;

Result<std::unique_ptr<MeshTextureBaker>> MeshTextureBaker::create(SceneChunk chunk,
                                                                   MeshTextureProfile profile) {
    try {
        const auto K_VALID = validate_chunk(chunk);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        if (profile != MeshTextureProfile::SINGLE_CACHE &&
            profile != MeshTextureProfile::DEPTH_SEQUENCE) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Unsupported texture source profile");
        }
        if (!chunk.m_geometry_valid || !chunk.m_mesh || chunk.m_mesh->m_baked ||
            chunk.m_mesh->m_pbr || chunk.m_mesh->m_triangles.empty() ||
            chunk.m_source_frames.empty() ||
            chunk.m_source_frames.size() >
                (profile == MeshTextureProfile::DEPTH_SEQUENCE ? 120uz : 18uz)) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Texture bake needs current untextured geometry and source frames");
        }
        auto state = std::make_unique<State>();
        state->m_profile = profile;
        const auto K_TRIANGLES = chunk.m_mesh->m_triangles.size();
        if (K_TRIANGLES > 100000uz) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture triangle budget exceeded");
        }
        state->m_columns =
            static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(K_TRIANGLES))));
        state->m_rows = (K_TRIANGLES + state->m_columns - 1uz) / state->m_columns;
        state->m_samples.resize(K_TRIANGLES * K_SAMPLES);
        state->m_chunk = std::move(chunk);
        return std::unique_ptr<MeshTextureBaker>(new MeshTextureBaker(std::move(state)));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture bake allocation failed");
    }
}

Result<void> MeshTextureBaker::add_view(const ColorProjectionView& view,
                                        FrameId frame,
                                        const std::stop_token& stop) {
    auto& state = *m_state;
    if (state.m_finished || state.m_failed ||
        state.m_views >= state.m_chunk.m_source_frames.size() ||
        state.m_chunk.m_source_frames[state.m_views] != frame ||
        view.metadata().m_dependency.m_frame != frame) {
        return fail(ErrorCode::INVALID_ARGUMENT,
                    "Texture views must follow the source receipt once");
    }
    if (!state.m_chunk.m_mesh) {
        return fail(ErrorCode::INVALID_DATA, "Missing texture geometry");
    }
    const auto& mesh = *state.m_chunk.m_mesh;
    if (mesh.m_observed) {
        const auto& expected = mesh.m_observed->m_context;
        const auto& actual = view.metadata().m_dependency;
        if (actual.m_epoch != expected.m_epoch ||
            actual.m_pose_revision != expected.m_pose_revision ||
            actual.m_pose_asset_sha256 != expected.m_pose_sha256) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Texture view pose context differs");
        }
    }
    std::size_t sample_index {};
    for (const auto& triangle : mesh.m_triangles) {
        if (stop.stop_requested()) {
            state.m_failed = true;
            return fail(ErrorCode::CANCELLED, "Texture projection cancelled");
        }
        std::array<Vec3d, 3> positions, normals;
        for (std::size_t i { 0uz }; i < 3uz; ++i) {
            const auto& vertex = mesh.m_vertices[triangle[i]];
            positions[i] =
                transform_point(state.m_chunk.m_world_from_chunk,
                                { vertex.m_position[0], vertex.m_position[1], vertex.m_position[2] });
            normals[i] = rotate(state.m_chunk.m_world_from_chunk.m_rotation,
                                { vertex.m_normal[0], vertex.m_normal[1], vertex.m_normal[2] });
        }
        for (std::size_t y { 0uz }; y < K_TILE; ++y) {
            for (std::size_t x { 0uz }; x + y < K_TILE; ++x) {
                const auto K_U = static_cast<double>(x) / 7, K_V = static_cast<double>(y) / 7;
                const auto K_A = static_cast<double>(7uz - x - y) / 7;
                const auto K_POSITION =
                    positions[0] * K_A + positions[1] * K_U + positions[2] * K_V;
                const auto K_NORMAL = normals[0] * K_A + normals[1] * K_U + normals[2] * K_V;
                const auto K_COLOR = view.project_color(K_POSITION, K_NORMAL);
                auto& sample = state.m_samples[sample_index++];
                if (K_COLOR.m_status != ColorProjectionStatus::ACCEPTED) {
                    continue;
                }
                const auto K_WEIGHT = K_COLOR.m_score * K_COLOR.m_score;
                for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                    sample.m_rgb[channel] += K_WEIGHT * K_COLOR.m_linear_rgb[channel];
                }
                sample.m_weight += K_WEIGHT;
                sample.m_candidates[state.m_views / 32uz] |= std::uint32_t { 1 }
                                                             << (state.m_views % 32uz);
                if (!sample.m_owner.m_value || K_COLOR.m_score > sample.m_best_score ||
                    (K_COLOR.m_score == sample.m_best_score && frame < sample.m_owner)) {
                    sample.m_owner = frame;
                    sample.m_best_score = K_COLOR.m_score;
                }
            }
        }
    }
    ++state.m_views;
    return {};
}

Result<BakedMeshAppearance> MeshTextureBaker::finish(const std::stop_token& stop) {
    try {
        auto& state = *m_state;
        if (state.m_finished || state.m_failed ||
            state.m_views != state.m_chunk.m_source_frames.size() || !state.m_chunk.m_mesh) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Texture bake is incomplete or already finalized");
        }
        BakedMeshAppearance baked;
        baked.m_profile = state.m_profile;
        const bool K_SEQUENCE { state.m_profile == MeshTextureProfile::DEPTH_SEQUENCE };
        baked.m_width = static_cast<std::uint32_t>(state.m_columns * K_TILE);
        baked.m_height = static_cast<std::uint32_t>(state.m_rows * K_TILE);
        const auto K_PIXELS = static_cast<std::size_t>(baked.m_width) * baked.m_height;
        baked.m_rgba.resize(K_PIXELS);
        baked.m_owners.resize(K_PIXELS);
        if (K_SEQUENCE) {
            baked.m_sequence_candidates.resize(K_PIXELS);
        } else {
            baked.m_candidates.resize(K_PIXELS);
        }
        baked.m_frames = state.m_chunk.m_source_frames;
        baked.m_source_chunk_sha256 = scene_untextured_chunk_hash(state.m_chunk);
        std::size_t sample_index {}, known {};
        const auto K_TRIANGLES = state.m_chunk.m_mesh->m_triangles.size();
        baked.m_corner_uv.reserve(K_TRIANGLES * 3uz);
        for (std::size_t face { 0uz }; face < K_TRIANGLES; ++face) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Texture finalization cancelled");
            }
            const auto K_X = face % state.m_columns * K_TILE, K_Y = face / state.m_columns * K_TILE;
            const auto K_U = static_cast<float>(K_X) + .5F, K_V = static_cast<float>(K_Y) + .5F;
            baked.m_corner_uv.insert(baked.m_corner_uv.end(),
                                     { { K_U, K_V }, { K_U + 7, K_V }, { K_U, K_V + 7 } });
            for (std::size_t y { 0uz }; y < K_TILE; ++y) {
                for (std::size_t x { 0uz }; x + y < K_TILE; ++x) {
                    const auto& sample = state.m_samples[sample_index++];
                    if (!sample.m_owner.m_value) {
                        continue;
                    }
                    ++known;
                    const auto K_INDEX = (K_Y + y) * baked.m_width + K_X + x;
                    for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                        baked.m_rgba[K_INDEX][channel] = static_cast<float>(
                            std::clamp(sample.m_rgb[channel] / sample.m_weight, 0.0, 1.0));
                    }
                    baked.m_rgba[K_INDEX][3] = 1;
                    baked.m_owners[K_INDEX] = sample.m_owner;
                    if (K_SEQUENCE) {
                        baked.m_sequence_candidates[K_INDEX] = sample.m_candidates;
                    } else {
                        baked.m_candidates[K_INDEX] = sample.m_candidates[0];
                    }
                }
            }
        }
        const nlohmann::json K_DOCUMENT {
            { "schema_version", K_SEQUENCE ? 3 : 1 },
            { "kind", K_SEQUENCE ? "CAPTURED_UNLIT_ATLAS_SEQUENCE_V1" : "CAPTURED_UNLIT_ATLAS_V1" },
            { "color_domain", "linear_srgb" },
            { "lighting", "captured_unlit" },
            { "tile_size", K_TILE },
            { "blend", "score_squared_convex_linear_rgb" },
            { "normals", "interpolated_source_vertex_normals" },
            { "filter", "triangular_linear_valid_only" },
            { "source_chunk_sha256", baked.m_source_chunk_sha256 },
            { "surface_texels", state.m_samples.size() },
            { "known_texels", known },
            { "unknown_surface_texels", state.m_samples.size() - known },
            { "atlas_texels", K_PIXELS },
            { "padding_texels", K_PIXELS - state.m_samples.size() },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "walking_enabled", false } };
        baked.m_manifest_json = K_DOCUMENT.dump(2);
        baked.m_manifest_sha256 = sha256(std::as_bytes(std::span(baked.m_manifest_json)));
        state.m_finished = true;
        return baked;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture finalization allocation failed");
    }
}

}  // namespace world_explorer
