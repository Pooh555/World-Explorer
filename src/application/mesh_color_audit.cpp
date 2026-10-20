#include "world_explorer/application/mesh_color_audit.hpp"

#include "color_candidates.hpp"
#include "color_inputs.hpp"
#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/image_decoder.hpp"
#include "world_explorer/scene_store/color_projection.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <set>

namespace world_explorer {
namespace {

using Json = nlohmann::json;
constexpr std::size_t K_INTERIOR_SAMPLES { 6uz }, K_SAMPLES_PER_FACE { 9uz };
constexpr std::size_t K_MAX_SAMPLES { 1800000uz };
constexpr std::array<std::array<double, 2>, K_INTERIOR_SAMPLES> K_UV {
    std::array { .125, .125 }, { .375, .125 }, { .625, .125 }, { .125, .375 }, { .375, .375 }, { .125, .625 } };
constexpr std::array K_STATUS_NAMES { "accepted",
                                     "invalid_sample",
                                     "behind_camera",
                                     "outside_image",
                                     "unknown_depth",
                                     "unsupported_depth",
                                     "grazing",
                                     "depth_mismatch" };

struct Sample {
    Vec3d m_position, m_normal;
    FrameId m_owner {};
    double m_score {};
    color_inputs::CandidateViews m_mask {};
    std::array<float, 3> m_rgb {}, m_minimum { 1, 1, 1 }, m_maximum {};
};

struct Edge {
    std::size_t m_incidence {};
    std::array<std::size_t, 2> m_samples {};
};

template <class ValueType> void integer(std::vector<std::byte>& bytes, ValueType value) {
    for (std::size_t i { 0uz }; i < sizeof(ValueType); ++i) {
        bytes.push_back(static_cast<std::byte>(value & 255U));
        value >>= 8U;
    }
}

std::uint8_t srgb_byte(float linear) {
    const double K_LINEAR { linear };
    const auto K_SRGB =
        K_LINEAR <= .0031308 ? 12.92 * K_LINEAR : 1.055 * std::pow(K_LINEAR, 1 / 2.4) - .055;
    return static_cast<std::uint8_t>(std::clamp(std::round(K_SRGB * 255), 0.0, 255.0));
}

}  // namespace

Result<std::string> audit_mesh_colors(const std::filesystem::path& request,
                                      const std::string& expected_sha256,
                                      const std::filesystem::path& destination,
                                      const std::stop_token& stop) {
    try {
        const auto K_INPUT = color_inputs::load(request, expected_sha256, destination, stop);
        if (!K_INPUT) {
            return std::unexpected(K_INPUT.error());
        }
        const auto& scene = K_INPUT->m_scene;
        const auto& output = K_INPUT->m_output;
        const auto& receipt = K_INPUT->m_receipt;
        const auto& chunk = *scene->m_chunks.begin()->second;
        if (!chunk.m_mesh || !chunk.m_mesh->m_observed) {
            return fail(ErrorCode::INVALID_DATA, "Missing color source");
        }
        const auto& mesh = *chunk.m_mesh;
        const auto& source = *mesh.m_observed;
        const auto K_VOXEL = K_INPUT->m_voxel_size;
        const auto& frames = receipt.at("frames");
        if (mesh.m_triangles.size() > K_MAX_SAMPLES / K_SAMPLES_PER_FACE) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color audit sample budget exceeded");
        }
        std::vector<Sample> samples;
        samples.reserve(mesh.m_triangles.size() * K_SAMPLES_PER_FACE);
        std::map<std::array<std::uint32_t, 2>, Edge> edges;
        for (const auto& triangle : mesh.m_triangles) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Color audit cancelled");
            }
            std::array<Vec3d, 3> points;
            for (std::size_t i { 0uz }; i < 3uz; ++i) {
                const auto& position = mesh.m_vertices[triangle[i]].m_position;
                points[i] = transform_point(chunk.m_world_from_chunk,
                                            { position[0], position[1], position[2] });
            }
            const auto K_NORMAL = cross(points[1] - points[0], points[2] - points[0]);
            for (const auto& uv : K_UV) {
                samples.push_back({ .m_position = points[0] * (1 - uv[0] - uv[1]) +
                                                 points[1] * uv[0] + points[2] * uv[1],
                                   .m_normal = K_NORMAL });
            }
            for (std::size_t i { 0uz }; i < 3uz; ++i) {
                const auto K_NEXT = (i + 1uz) % 3uz;
                std::array key { triangle[i], triangle[K_NEXT] };
                std::ranges::sort(key);
                auto& edge = edges[key];
                if (edge.m_incidence < 2uz) {
                    edge.m_samples[edge.m_incidence] = samples.size();
                }
                ++edge.m_incidence;
                samples.push_back(
                    { .m_position = (points[i] + points[K_NEXT]) * .5, .m_normal = K_NORMAL });
            }
        }
        auto ledgers = Json::array();
        for (std::size_t view_index { 0uz }; view_index < frames.size(); ++view_index) {
            const FrameId K_FRAME_ID { frames[view_index].at("frame").get<std::uint64_t>() };
            auto view = color_inputs::view(*K_INPUT, view_index, stop);
            if (!view) {
                return std::unexpected(view.error());
            }
            std::array<std::size_t, static_cast<std::size_t>(ColorProjectionStatus::COUNT)>
                counts {};
            for (std::size_t i { 0uz }; i < samples.size(); ++i) {
                if (i % 4096uz == 0uz && stop.stop_requested()) {
                    return fail(ErrorCode::CANCELLED, "Color audit cancelled");
                }
                auto& sample = samples[i];
                const auto K_COLOR = view->project_color(sample.m_position, sample.m_normal);
                ++counts[static_cast<std::size_t>(K_COLOR.m_status)];
                if (K_COLOR.m_status != ColorProjectionStatus::ACCEPTED) {
                    continue;
                }
                sample.m_mask.add(view_index);
                for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                    sample.m_minimum[channel] =
                        std::min(sample.m_minimum[channel], K_COLOR.m_linear_rgb[channel]);
                    sample.m_maximum[channel] =
                        std::max(sample.m_maximum[channel], K_COLOR.m_linear_rgb[channel]);
                }
                const auto K_FRAME = K_FRAME_ID;
                if (!sample.m_owner.m_value || K_COLOR.m_score > sample.m_score ||
                    (K_COLOR.m_score == sample.m_score && K_FRAME < sample.m_owner)) {
                    sample.m_owner = K_FRAME;
                    sample.m_score = K_COLOR.m_score;
                    sample.m_rgb = K_COLOR.m_linear_rgb;
                }
            }
            Json ledger { { "frame", K_FRAME_ID.m_value }, { "attempts", samples.size() } };
            for (std::size_t i { 0uz }; i < counts.size(); ++i) {
                ledger[K_STATUS_NAMES[i]] = counts[i];
            }
            ledgers.push_back(std::move(ledger));
        }
        std::array<std::size_t, 3> face_counts {};
        std::vector<std::size_t> coverage_histogram(K_INPUT->m_sequence ? frames.size() + 1uz
                                                                        : 19uz);
        std::size_t known {}, boundary {}, nonmanifold {}, shared {}, compared {},
            different_owner {};
        double maximum_seam {}, sum_seam {}, maximum_spread {};
        auto owners = Json::object();
        for (const auto& frame : frames) {
            owners[std::to_string(frame.at("frame").get<std::uint64_t>())] = 0uz;
        }
        for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
            std::size_t face_known {};
            for (std::size_t i { 0uz }; i < K_INTERIOR_SAMPLES; ++i) {
                const auto& sample = samples[face * K_SAMPLES_PER_FACE + i];
                const auto K_VIEWS = sample.m_mask.count();
                ++coverage_histogram[K_VIEWS];
                if (!sample.m_owner.m_value) {
                    continue;
                }
                ++known;
                ++face_known;
                auto& count = owners[std::to_string(sample.m_owner.m_value)];
                count = count.get<std::size_t>() + 1uz;
                for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                    maximum_spread = std::max(
                        maximum_spread,
                        static_cast<double>(sample.m_maximum[channel] - sample.m_minimum[channel]));
                }
            }
            ++face_counts[face_known == 0uz ? 0uz : face_known == K_INTERIOR_SAMPLES ? 2uz : 1uz];
        }
        for (const auto& [K_KEY, K_EDGE] : edges) {
            static_cast<void>(K_KEY);
            if (K_EDGE.m_incidence == 1uz) {
                ++boundary;
                continue;
            }
            if (K_EDGE.m_incidence != 2uz) {
                ++nonmanifold;
                continue;
            }
            ++shared;
            const auto &left = samples[K_EDGE.m_samples[0]], &right = samples[K_EDGE.m_samples[1]];
            if (!left.m_owner.m_value || !right.m_owner.m_value) {
                continue;
            }
            ++compared;
            different_owner += left.m_owner != right.m_owner;
            double difference {};
            for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                difference = std::max(
                    difference,
                    std::abs(static_cast<double>(left.m_rgb[channel]) - right.m_rgb[channel]));
            }
            maximum_seam = std::max(maximum_seam, difference);
            sum_seam += difference;
        }
        const auto K_COLUMNS = static_cast<std::size_t>(
            std::ceil(std::sqrt(static_cast<double>(mesh.m_triangles.size()))));
        const auto K_ROWS = (mesh.m_triangles.size() + K_COLUMNS - 1uz) / K_COLUMNS;
        const auto K_WIDTH = K_COLUMNS * 4uz, K_HEIGHT = K_ROWS * 4uz;
        const auto K_HEADER =
            "P6\n" + std::to_string(K_WIDTH) + " " + std::to_string(K_HEIGHT) + "\n255\n";
        std::vector<std::byte> atlas(std::as_bytes(std::span(K_HEADER)).begin(),
                                     std::as_bytes(std::span(K_HEADER)).end());
        atlas.resize(atlas.size() + K_WIDTH * K_HEIGHT * 3uz, std::byte { 32 });
        std::vector<std::byte> encoded;
        const auto K_RECORD_BYTES = K_INPUT->m_sequence ? 56uz : 44uz;
        encoded.reserve(32uz + samples.size() * K_RECORD_BYTES);
        for (char character : std::string_view(K_INPUT->m_sequence ? "WECLR002" : "WECLR001")) {
            encoded.push_back(static_cast<std::byte>(character));
        }
        integer<std::uint64_t>(encoded, mesh.m_triangles.size());
        integer<std::uint64_t>(encoded, frames.size());
        integer<std::uint64_t>(encoded, samples.size());
        for (const auto& sample : samples) {
            integer<std::uint64_t>(encoded, sample.m_owner.m_value);
            for (std::size_t word { 0uz }; word < (K_INPUT->m_sequence ? 4uz : 1uz); ++word) {
                integer<std::uint32_t>(encoded, sample.m_mask.words()[word]);
            }
            for (auto color : sample.m_rgb) {
                integer<std::uint32_t>(encoded, std::bit_cast<std::uint32_t>(color));
            }
            integer<std::uint64_t>(encoded, std::bit_cast<std::uint64_t>(sample.m_score));
            for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                integer<std::uint32_t>(encoded,
                                       std::bit_cast<std::uint32_t>(
                                           sample.m_owner.m_value ? sample.m_maximum[channel] -
                                                                        sample.m_minimum[channel]
                                                                  : 0.0F));
            }
        }
        for (std::size_t face { 0uz }; face < mesh.m_triangles.size(); ++face) {
            for (std::size_t i { 0uz }; i < K_INTERIOR_SAMPLES; ++i) {
                const auto& sample = samples[face * K_SAMPLES_PER_FACE + i];
                const auto K_X = face % K_COLUMNS * 4uz + static_cast<std::size_t>(K_UV[i][0] * 4);
                const auto K_Y = face / K_COLUMNS * 4uz + static_cast<std::size_t>(K_UV[i][1] * 4);
                const auto K_OFFSET = K_HEADER.size() + (K_Y * K_WIDTH + K_X) * 3uz;
                for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                    atlas[K_OFFSET + channel] = static_cast<std::byte>(
                        sample.m_owner.m_value ? srgb_byte(sample.m_rgb[channel])
                        : channel == 1uz       ? 0
                                               : 255);
                }
            }
        }
        const Json K_REPORT {
            { "schema_version", K_INPUT->m_sequence ? 2 : 1 },
            { "kind",
             K_INPUT->m_sequence ? "CAPTURE_COLOR_PROJECTION_AUDIT_V2"
                                 : "CAPTURE_COLOR_PROJECTION_AUDIT_V1" },
            { "scope",
             "diagnostic sampled atlas/edge audit; no texture export or continuous coverage "
             "certification" },
            { "color_domain", "captured sRGB decoded to linear RGB; no albedo" },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "walking_enabled", false },
            { "scene_sha256", scene_content_hash(*scene) },
            { "parent_asset_sha256", source.m_asset_sha256 },
            { "pose_asset_sha256", source.m_context.m_pose_sha256 },
            { "cache_sha256", source.m_context.m_receipt_sha256 },
            { "cleanup_sha256", source.m_cleanup_sha256 },
            { "triangles", mesh.m_triangles.size() },
            { "vertices", mesh.m_vertices.size() },
            { "views", frames.size() },
            { "policy",
             { { "voxel_size", K_VOXEL },
              { "depth_world_voxels", 2 },
              { "relative_axial_depth", .01 },
              { "minimum_support", 2 },
              { "minimum_absolute_incidence_cosine", .1 },
              { "full_bilinear_footprint_required", true },
              { "uncertainty_used", false },
              { "atlas_tile_size", 4 },
              { "interior_uv", K_UV },
              { "owner_score",
               "absolute_incidence_cosine/(1+maximum_corner_depth_residual/tolerance); ties lower "
               "frame ID" } } },
            { "atlas_samples",
             { { "total", mesh.m_triangles.size() * K_INTERIOR_SAMPLES },
              { "known", known },
              { "unknown", mesh.m_triangles.size() * K_INTERIOR_SAMPLES - known },
              { "view_count_histogram", coverage_histogram },
              { "selected_owners", owners },
              { "maximum_candidate_linear_rgb_spread", maximum_spread } } },
            { "faces",
             { { "all_samples_unknown", face_counts[0] },
              { "partially_sampled", face_counts[1] },
              { "all_six_samples_known", face_counts[2] } } },
            { "edges",
             { { "boundary", boundary },
              { "nonmanifold", nonmanifold },
              { "two_face", shared },
              { "both_midpoints_known", compared },
              { "unknown_comparisons", shared - compared },
              { "different_owner", different_owner },
              { "maximum_linear_rgb_linf", compared ? Json(maximum_seam) : Json(nullptr) },
              { "mean_linear_rgb_linf",
               compared ? Json(sum_seam / static_cast<double>(compared)) : Json(nullptr) } } },
            { "view_ledgers", ledgers },
            { "atlas_diagnostic",
             "magenta=unknown; dark gray=outside sampled triangle; no dilation or filling" } };
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Color audit cancelled");
        }
        if (!std::filesystem::create_directory(output)) {
            return fail(ErrorCode::IO_ERROR, "Cannot reserve color audit directory");
        }
        const auto K_FLUSH = asset_files::flush_directory_entry(output.parent_path());
        if (!K_FLUSH) {
            return std::unexpected(K_FLUSH.error());
        }
        auto files = Json::object();
        const auto K_PUBLISH = [&](const std::string& name,
                                   std::span<const std::byte> bytes) -> Result<void> {
            const auto K_HASH = asset_files::publish_asset(output / name, bytes, stop);
            if (!K_HASH) {
                return std::unexpected(K_HASH.error());
            }
            files[name] = { { "bytes", bytes.size() }, { "sha256", *K_HASH } };
            return {};
        };
        for (const auto& [K_NAME, K_DATA] :
             std::array<std::pair<std::string, std::span<const std::byte>>, 2> {
                 std::pair<std::string, std::span<const std::byte>> { "samples.bin", encoded },
                 { "atlas.ppm", atlas } }) {
            const auto K_WRITTEN = K_PUBLISH(K_NAME, K_DATA);
            if (!K_WRITTEN) {
                return std::unexpected(K_WRITTEN.error());
            }
        }
        const auto K_REPORT_TEXT = K_REPORT.dump(2) + "\n";
        const auto K_WRITTEN_REPORT =
            K_PUBLISH("report.json", std::as_bytes(std::span(K_REPORT_TEXT)));
        if (!K_WRITTEN_REPORT) {
            return std::unexpected(K_WRITTEN_REPORT.error());
        }
        const Json K_MANIFEST {
            { "schema_version", K_INPUT->m_sequence ? 2 : 1 },
            { "kind",
             K_INPUT->m_sequence ? "CAPTURE_COLOR_PROJECTION_AUDIT_V2"
                                 : "CAPTURE_COLOR_PROJECTION_AUDIT_V1" },
            { "request_sha256", expected_sha256 },
            { "scene_sha256", scene_content_hash(*scene) },
            { "files", files },
            { "scope", "diagnostic samples only; source RGB and original geometry unchanged" } };
        const auto K_TEXT = K_MANIFEST.dump(2) + "\n";
        return asset_files::publish_asset(
            output / "manifest.json", std::as_bytes(std::span(K_TEXT)), stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Color audit allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Color audit failed: ") + exception.what());
    }
}

}  // namespace world_explorer
