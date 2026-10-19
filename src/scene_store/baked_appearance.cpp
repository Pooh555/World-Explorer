#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <algorithm>
#include <bit>
#include <set>

#include <nlohmann/json.hpp>

namespace world_explorer {

Result<std::size_t> validate_baked_appearance(const SceneChunk& chunk, SceneLimits limits) {
    if (!chunk.m_mesh || !chunk.m_mesh->m_baked) return 0uz;
    const auto& mesh = *chunk.m_mesh;
    const auto& baked = *mesh.m_baked;
    const auto K_HASH_VALID = [](const std::string& hash) {
        return hash.size() == 64uz && hash.find_first_not_of("0123456789abcdef") == hash.npos;
    };
    if (!baked.m_width || !baked.m_height || baked.m_width > 8192 || baked.m_height > 8192 ||
        baked.m_height > limits.m_max_texture_texels / baked.m_width)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture dimensions exceed budget");
    const auto K_COLUMNS = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<double>(mesh.m_triangles.size()))));
    if (!K_COLUMNS || mesh.m_triangles.size() > 100000uz || baked.m_width != K_COLUMNS * 8uz ||
        baked.m_height != ((mesh.m_triangles.size() + K_COLUMNS - 1uz) / K_COLUMNS) * 8uz)
        return fail(ErrorCode::INVALID_DATA, "Unsupported texture tile layout");
    const auto K_PIXELS = static_cast<std::size_t>(baked.m_width) * baked.m_height;
    if (baked.m_rgba.size() != K_PIXELS || baked.m_owners.size() != K_PIXELS ||
        baked.m_candidates.size() != K_PIXELS ||
        mesh.m_triangles.size() > limits.m_max_payload_bytes / 24uz ||
        baked.m_corner_uv.size() != mesh.m_triangles.size() * 3uz || baked.m_frames.empty() ||
        baked.m_frames.size() > 18uz || baked.m_frames != chunk.m_source_frames)
        return fail(ErrorCode::INVALID_DATA, "Texture arrays or source frames differ");
    const std::set<FrameId> K_FRAMES(baked.m_frames.begin(), baked.m_frames.end());
    if (K_FRAMES.size() != baked.m_frames.size() || K_FRAMES.contains(FrameId {}))
        return fail(ErrorCode::INVALID_DATA, "Invalid texture source inventory");
    static const auto K_SRGB8 = [] {
        std::array<std::uint32_t, 256> values {};
        for (std::size_t i { 0uz }; i < values.size(); ++i) {
            const auto K_SRGB = static_cast<double>(i) / 255;
            const auto K_LINEAR = static_cast<float>(
                K_SRGB <= .04045 ? K_SRGB / 12.92 : std::pow((K_SRGB + .055) / 1.055, 2.4));
            values[i] = std::bit_cast<std::uint32_t>(K_LINEAR);
        }
        return values;
    }();
    const auto K_MASK = (std::uint32_t { 1 } << baked.m_frames.size()) - 1U;
    std::size_t known {};
    for (std::size_t i { 0uz }; i < K_PIXELS; ++i) {
        const auto& color = baked.m_rgba[i];
        const auto K_X = i % baked.m_width, K_Y = i / baked.m_width;
        const auto K_FACE = K_Y / 8uz * K_COLUMNS + K_X / 8uz;
        if ((K_FACE >= mesh.m_triangles.size() || K_X % 8uz + K_Y % 8uz >= 8uz) &&
            color != std::array<float, 4> {})
            return fail(ErrorCode::INVALID_DATA, "Texture padding contains color");
        for (float value : color)
            if (!std::isfinite(value) || value < 0 || value > 1)
                return fail(ErrorCode::INVALID_DATA, "Invalid baked color");
        if (!baked.m_portable_parent_sha256.empty())
            for (std::size_t c { 0uz }; c < 3uz; ++c) {
                if (color[c] == 0 || color[c] == 1) continue;
                const auto K_BITS = std::bit_cast<std::uint32_t>(color[c]);
                const auto K_FOUND = std::ranges::lower_bound(K_SRGB8, K_BITS);
                // One float ULP admits library rounding differences in sRGB decoding.
                const bool K_UPPER = K_FOUND != K_SRGB8.end() && *K_FOUND - K_BITS <= 1U;
                const bool K_LOWER =
                    K_FOUND != K_SRGB8.begin() && K_BITS - *std::prev(K_FOUND) <= 1U;
                if (!K_UPPER && !K_LOWER)
                    return fail(ErrorCode::INVALID_DATA,
                                "Portable color is not an sRGB8 decoded value");
            }
        const auto K_OWNER = baked.m_owners[i];
        const auto K_CANDIDATES = baked.m_candidates[i];
        if (color[3] == 0) {
            if (color != std::array<float, 4> {} || K_OWNER.m_value || K_CANDIDATES)
                return fail(ErrorCode::INVALID_DATA, "Unknown texture texel contains observations");
        } else {
            ++known;
            const auto K_FOUND = std::ranges::find(baked.m_frames, K_OWNER);
            if (color[3] != 1 || !K_CANDIDATES || (K_CANDIDATES & ~K_MASK) ||
                K_FOUND == baked.m_frames.end() ||
                !(K_CANDIDATES & (std::uint32_t { 1 }
                                  << static_cast<std::size_t>(K_FOUND - baked.m_frames.begin()))))
                return fail(ErrorCode::INVALID_DATA, "Invalid texture candidate ownership");
        }
    }
    for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
        const auto K_X = static_cast<float>(face % K_COLUMNS * 8uz) + .5F;
        const auto K_ROW = face / K_COLUMNS;
        const auto K_Y = static_cast<float>(K_ROW * 8uz) + .5F;
        const std::array<std::array<float, 2>, 3> K_UV {
            std::array { K_X, K_Y }, { K_X + 7, K_Y }, { K_X, K_Y + 7 }};
        for (std::size_t corner { 0uz }; corner < 3uz; ++corner)
            if (baked.m_corner_uv[face * 3uz + corner] != K_UV[corner])
                return fail(ErrorCode::INVALID_DATA, "Invalid canonical texture UV");
    }
    if (!K_HASH_VALID(baked.m_source_chunk_sha256) || !K_HASH_VALID(baked.m_manifest_sha256) ||
        baked.m_manifest_json.empty() || baked.m_manifest_json.size() > 1024uz * 1024uz ||
        sha256(std::as_bytes(std::span(baked.m_manifest_json))) != baked.m_manifest_sha256)
        return fail(ErrorCode::INVALID_DATA, "Invalid texture authoring metadata");
    try {
        using Json = nlohmann::json;
        if (baked.m_manifest_json.find('\0') != baked.m_manifest_json.npos)
            return fail(ErrorCode::INVALID_DATA, "Raw NUL in texture metadata");
        std::set<std::string> keys;
        const auto K_DOCUMENT = Json::parse(
            baked.m_manifest_json, [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 1) throw std::runtime_error("Nested texture metadata");
                if (event == Json::parse_event_t::key &&
                    !keys.insert(value.get<std::string>()).second)
                    throw std::runtime_error("Duplicate texture metadata key");
                return true;
            });
        Json expected {{ "schema_version", 1 },
                       { "kind", "CAPTURED_UNLIT_ATLAS_V1" },
                       { "color_domain", "linear_srgb" },
                       { "lighting", "captured_unlit" },
                       { "tile_size", 8 },
                       { "blend", "score_squared_convex_linear_rgb" },
                       { "normals", "interpolated_source_vertex_normals" },
                       { "filter", "triangular_linear_valid_only" },
                       { "source_chunk_sha256", baked.m_source_chunk_sha256 },
                       { "surface_texels", mesh.m_triangles.size() * 36uz },
                       { "known_texels", known },
                       { "unknown_surface_texels", mesh.m_triangles.size() * 36uz - known },
                       { "atlas_texels", K_PIXELS },
                       { "padding_texels", K_PIXELS - mesh.m_triangles.size() * 36uz },
                       { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
                       { "walking_enabled", false }};
        if (!baked.m_portable_parent_sha256.empty()) {
            if (!K_HASH_VALID(baked.m_portable_parent_sha256))
                return fail(ErrorCode::INVALID_DATA, "Invalid portable appearance parent pin");
            expected["schema_version"] = 2;
            expected["kind"] = "CAPTURED_UNLIT_ATLAS_PNG_V1";
            expected["blend"] = "score_squared_convex_linear_rgb_then_srgb8";
            expected["filter"] = "bilinear_valid_only";
            expected["parent_chunk_sha256"] = baked.m_portable_parent_sha256;
        }
        if (K_DOCUMENT != expected)
            return fail(ErrorCode::INVALID_DATA,
                        "Texture metadata disagrees with compiled appearance");
        for (const auto* key : {"schema_version",
                                "tile_size",
                                "surface_texels",
                                "known_texels",
                                "unknown_surface_texels",
                                "atlas_texels",
                                "padding_texels"})
            if (!K_DOCUMENT.at(key).is_number_unsigned())
                return fail(ErrorCode::INVALID_DATA, "Invalid texture metadata integer");
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture metadata allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
    if (chunk.m_geometry_valid && scene_untextured_chunk_hash(chunk) != baked.m_source_chunk_sha256)
        return fail(ErrorCode::RESYNC_REQUIRED, "Texture geometry dependency differs");
    auto bytes = mesh.m_triangles.size() * 24uz;
    if (K_PIXELS > (limits.m_max_payload_bytes - bytes) / 28uz)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture payload exceeds budget");
    bytes += K_PIXELS * 28uz;
    const auto K_METADATA = baked.m_manifest_json.size() + baked.m_portable_parent_sha256.size() +
                            128uz + baked.m_frames.size() * 8uz;
    if (K_METADATA > limits.m_max_payload_bytes - bytes)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Texture metadata exceeds budget");
    return bytes + K_METADATA;
}

}  // namespace world_explorer
