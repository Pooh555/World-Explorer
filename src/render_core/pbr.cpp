#include "world_explorer/render_core/pbr.hpp"

#include "world_explorer/scene_store/world_frame.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace world_explorer {

namespace {

struct PbrFailure : std::runtime_error {
    ErrorCode m_code;

    PbrFailure(ErrorCode code, const std::string& message)
        : std::runtime_error(message), m_code(code) { }
};

void require(bool ok, const std::string& message, ErrorCode code = ErrorCode::INVALID_DATA) {
    if (!ok) {
        throw PbrFailure(code, message);
    }
}

}  // namespace

Result<void> validate_pbr_lighting(const PbrLighting& lighting) {
    const auto K_LENGTH = dot(lighting.m_to_light, lighting.m_to_light);
    if (!finite(lighting.m_to_light) || !std::isfinite(K_LENGTH) || K_LENGTH < 1e-12 ||
        K_LENGTH > 1e12 || !finite(lighting.m_ambient) ||
        std::min({ lighting.m_ambient.m_x, lighting.m_ambient.m_y, lighting.m_ambient.m_z }) < 0 ||
        std::max({ lighting.m_ambient.m_x, lighting.m_ambient.m_y, lighting.m_ambient.m_z }) > 16 ||
        !std::isfinite(lighting.m_intensity) || lighting.m_intensity < 0 ||
        lighting.m_intensity > 64 || !std::isfinite(lighting.m_exposure) ||
        lighting.m_exposure < 0 || lighting.m_exposure > 16) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid PBR diagnostic lighting");
    }
    return {};
}

Result<PbrUpload>
prepare_pbr_upload(const SceneState& scene, Vec3d origin, std::size_t base_offset) {
    try {
        require(finite(origin) && base_offset % 4uz == 0 && base_offset <= UINT32_MAX,
                "Invalid PBR upload base");
        PbrUpload result;
        require(scene.m_chunks.size() <= 4096uz,
                "PBR source chunk budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        const bool K_HAS = std::ranges::any_of(scene.m_chunks, [](const auto& item) {
            return item.second && item.second->m_mesh && item.second->m_mesh->m_pbr;
        });
        if (!K_HAS) {
            return result;
        }
        const auto K_VALID = validate_scene(scene);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        const auto K_AT = [&] {
            require(result.m_bytes.size() <= 16uz * 1024uz * 1024uz &&
                        base_offset + result.m_bytes.size() <= UINT32_MAX,
                    "PBR upload byte budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            return base_offset + result.m_bytes.size();
        };
        const auto K_WORD = [&](std::uint32_t word) {
            (void)K_AT();
            require(base_offset <= UINT32_MAX - 4uz &&
                        result.m_bytes.size() <= UINT32_MAX - base_offset - 4uz &&
                        result.m_bytes.size() <= 16uz * 1024uz * 1024uz - 4uz,
                    "PBR upload word exceeds byte budget",
                    ErrorCode::RESOURCE_EXHAUSTED);
            for (std::size_t i { 0uz }; i < 4uz; ++i) {
                result.m_bytes.push_back(static_cast<std::byte>((word >> (8uz * i)) & 255U));
            }
        };
        const auto K_REAL = [&](double value) {
            const float K_VALUE { static_cast<float>(value) };
            require(std::isfinite(K_VALUE), "PBR upload float overflow");
            K_WORD(std::bit_cast<std::uint32_t>(K_VALUE));
        };
        for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
            (void)K_ID;
            if (!K_CHUNK->m_geometry_valid || !K_CHUNK->m_mesh || !K_CHUNK->m_mesh->m_pbr) {
                continue;
            }
            const auto& mesh = *K_CHUNK->m_mesh;
            const auto& pbr = *mesh.m_pbr;
            const auto K_VERTEX = K_AT();
            for (std::size_t i { 0uz }; i < mesh.m_vertices.size(); ++i) {
                const auto& vertex = mesh.m_vertices[i];
                for (auto x : vertex.m_position) {
                    K_REAL(x);
                }
                for (auto x : vertex.m_normal) {
                    K_REAL(x);
                }
                for (auto x : pbr.m_tangents[i]) {
                    K_REAL(x);
                }
                for (auto x : pbr.m_uv[i]) {
                    K_REAL(x);
                }
            }
            result.m_vertices += mesh.m_vertices.size();
            result.m_triangles += mesh.m_triangles.size();
            std::vector<std::pair<std::size_t, std::uint32_t>> groups;
            for (std::size_t material { 0uz }; material < pbr.m_materials.size(); ++material) {
                const auto K_BEGIN = K_AT();
                std::uint32_t count {};
                for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
                    if (pbr.m_triangle_materials[face] != material) {
                        continue;
                    }
                    for (auto index : mesh.m_triangles[face]) {
                        K_WORD(index);
                        ++count;
                    }
                }
                groups.emplace_back(K_BEGIN, count);
            }
            const auto K_INSTANCES = static_cast<std::uint32_t>(K_AT() / 4uz);
            const auto K_WORLD = scene_world_from_chunk(scene, *K_CHUNK);
            for (const auto& instance : pbr.m_instances) {
                const auto& local = instance.m_chunk_from_asset;
                const auto K_POSITION = transform_point(K_WORLD, local.m_translation) - origin;
                const auto K_COLUMN = [&](Vec3d axis) {
                    return rotate(K_WORLD.m_rotation, rotate(local.m_rotation, axis)) *
                           (K_WORLD.m_scale * local.m_scale);
                };
                const auto K_X = K_COLUMN({ 1, 0, 0 }), K_Y = K_COLUMN({ 0, 1, 0 }),
                           K_Z = K_COLUMN({ 0, 0, 1 });
                for (const auto& vertex : mesh.m_vertices) {
                    const auto K_POINT =
                        transform_point(K_WORLD,
                                        transform_point(local,
                                                        { vertex.m_position[0uz],
                                                         vertex.m_position[1uz],
                                                         vertex.m_position[2uz] })) -
                        origin;
                    require(finite(K_POINT) && std::max({ std::abs(K_POINT.m_x),
                                                         std::abs(K_POINT.m_y),
                                                         std::abs(K_POINT.m_z) }) <= 1e7,
                            "PBR exceeds camera-local extent",
                            ErrorCode::UNSUPPORTED_OPERATION);
                }
                for (auto x : { K_X.m_x,
                               K_Y.m_x,
                               K_Z.m_x,
                               K_POSITION.m_x,
                               K_X.m_y,
                               K_Y.m_y,
                               K_Z.m_y,
                               K_POSITION.m_y,
                               K_X.m_z,
                               K_Y.m_z,
                               K_Z.m_z,
                               K_POSITION.m_z }) {
                    K_REAL(x);
                }
            }
            for (std::size_t material { 0uz }; material < pbr.m_materials.size(); ++material) {
                const auto& source = pbr.m_materials[material];
                std::array<std::uint32_t, 4> offsets {};
                for (std::size_t image { 0uz }; image < 4uz; ++image) {
                    offsets[image] = static_cast<std::uint32_t>(K_AT() / 4uz);
                    const auto& pixels = source.m_images[image];
                    for (std::size_t pixel { 0uz }; pixel < pixels.size(); pixel += 4uz) {
                        std::uint32_t rgba {};
                        for (std::size_t channel { 0uz }; channel < 4uz; ++channel) {
                            rgba |= std::to_integer<std::uint32_t>(pixels[pixel + channel])
                                    << (8uz * channel);
                        }
                        K_WORD(rgba);
                    }
                }
                const auto K_MATERIAL = static_cast<std::uint32_t>(K_AT() / 4uz);
                for (auto x : offsets) {
                    K_WORD(x);
                }
                K_WORD(source.m_width);
                K_WORD(source.m_height);
                K_WORD(source.m_linear_filter);
                K_WORD(source.m_repeat);
                result.m_draws.push_back({ K_VERTEX,
                                          groups[material].first,
                                          groups[material].second,
                                          static_cast<std::uint32_t>(pbr.m_instances.size()),
                                          K_INSTANCES,
                                          K_MATERIAL });
            }
        }
        (void)K_AT();
        return result;
    } catch (const PbrFailure& e) {
        return fail(e.m_code, e.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "PBR upload allocation failed");
    }
}

}  // namespace world_explorer
