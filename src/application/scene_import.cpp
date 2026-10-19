#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <set>

namespace world_explorer {
namespace {

struct SplatImportContext {
    std::map<std::string, std::string> m_provenance;
    std::string m_expected_hash;
    std::optional<std::uint64_t> m_expected_count;
    std::optional<FitPoseDependency> m_poses {};
};

Result<void> publish_scene(const SceneImportRequest& request, SplatImportContext context) {
    const auto& ply = request.m_input;
    const auto& destination = request.m_destination;
    const auto K_SOURCE_HASH = sha256_file(ply, 8ULL * 1024 * 1024 * 1024, request.m_stop);
    if (!K_SOURCE_HASH) {
        return std::unexpected(K_SOURCE_HASH.error());
    }
    if (!context.m_expected_hash.empty() && *K_SOURCE_HASH != context.m_expected_hash) {
        return fail(ErrorCode::INVALID_DATA,
                    "Fit asset checksum mismatch; previous scene retained");
    }
    std::uint64_t identity {};
    const auto K_CONVERTED =
        std::from_chars(K_SOURCE_HASH->data(), K_SOURCE_HASH->data() + 16, identity, 16);
    if (K_CONVERTED.ec != std::errc {}) {
        return fail(ErrorCode::INVALID_DATA, "Invalid source digest");
    }
    auto imported = load_gsplat_ply(ply, SceneId { identity ? identity : 1 });
    if (!imported) {
        return std::unexpected(imported.error());
    }
    if (context.m_expected_count) {
        std::uint64_t count {};
        for (const auto& [K_ID, K_CHUNK] : (*imported)->m_chunks) {
            (void)K_ID;
            count += K_CHUNK->m_splats.size();
        }
        if (count != *context.m_expected_count) {
            return fail(ErrorCode::INVALID_DATA,
                        "Fit asset count differs from its completion manifest");
        }
    }
    const auto K_RECHECKED_HASH = sha256_file(ply, 8ULL * 1024 * 1024 * 1024, request.m_stop);
    if (!K_RECHECKED_HASH && K_RECHECKED_HASH.error().m_code == ErrorCode::CANCELLED) {
        return std::unexpected(K_RECHECKED_HASH.error());
    }
    if (!K_RECHECKED_HASH || *K_RECHECKED_HASH != *K_SOURCE_HASH) {
        return fail(ErrorCode::INVALID_DATA,
                    "Input asset changed during compilation; previous scene retained");
    }
    auto scene = std::make_shared<SceneState>(**imported);
    scene->m_provenance = std::move(context.m_provenance);
    scene->m_provenance["input_sha256"] = *K_SOURCE_HASH;
    scene->m_provenance["exporter"] = "gsplat-1.5.3";
    scene->m_provenance["quality_gate"] = "UNVERIFIED";
    if (context.m_poses) {
        scene->m_epoch = context.m_poses->m_epoch;
        for (auto& [K_ID, chunk] : scene->m_chunks) {
            (void)K_ID;
            auto dependent = std::make_shared<SceneChunk>(*chunk);
            dependent->m_pose_revision = { context.m_poses->m_revision.m_value };
            dependent->m_source_frames = context.m_poses->m_source_frames;
            chunk = std::move(dependent);
        }
    }
    if (request.m_stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Scene publication cancelled; previous scene retained");
    }
    return write_bundle(destination, scene);
}

bool valid_digest(const std::string& value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

Result<std::string> inspection_view_metadata(const std::filesystem::path& path,
                                             const std::string& source_hash) {
    try {
        if (std::filesystem::file_size(path) > std::uintmax_t { 1024 } * 1024) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fit input manifest exceeds 1 MiB");
        }
        std::ifstream input(path);
        const auto K_MANIFEST = nlohmann::json::parse(input);
        if (K_MANIFEST.at("source_sha256") != source_hash) {
            return fail(ErrorCode::INVALID_DATA, "Fit input and result source identities differ");
        }
        const auto& frame = K_MANIFEST.at("dataset").at("frames").at(0);
        const auto K_VIEW = frame.at("view").get<std::array<std::array<double, 4>, 4>>();
        const auto K_INTRINSIC = frame.at("intrinsic").get<std::array<std::array<double, 3>, 3>>();
        for (const auto& row : K_VIEW) {
            for (const auto K_ELEMENT : row) {
                if (!std::isfinite(K_ELEMENT)) {
                    return fail(ErrorCode::INVALID_DATA, "Non-finite source camera transform");
                }
            }
        }
        for (const auto& row : K_INTRINSIC) {
            for (const auto K_ELEMENT : row) {
                if (!std::isfinite(K_ELEMENT)) {
                    return fail(ErrorCode::INVALID_DATA, "Non-finite source camera calibration");
                }
            }
        }
        for (std::size_t row { 0uz }; row < 3uz; ++row) {
            for (std::size_t column { 0uz }; column < 3uz; ++column) {
                double product {};
                for (std::size_t index { 0uz }; index < 3uz; ++index) {
                    product += K_VIEW[row][index] * K_VIEW[column][index];
                }
                if (std::abs(product - (row == column ? 1. : 0.)) > 1e-5) {
                    return fail(ErrorCode::INVALID_DATA, "Non-orthonormal source camera rotation");
                }
            }
        }
        const Vec3d K_ROW_0 { K_VIEW[0][0], K_VIEW[0][1], K_VIEW[0][2] };
        const Vec3d K_ROW_1 { K_VIEW[1][0], K_VIEW[1][1], K_VIEW[1][2] };
        const Vec3d K_ROW_2 { K_VIEW[2][0], K_VIEW[2][1], K_VIEW[2][2] };
        if (std::abs(dot(cross(K_ROW_0, K_ROW_1), K_ROW_2) - 1) > 1e-5) {
            return fail(ErrorCode::INVALID_DATA, "Reflected source camera rotation");
        }
        if (K_VIEW[3] != std::array<double, 4> { 0, 0, 0, 1 } || K_INTRINSIC[1][1] <= 0) {
            return fail(ErrorCode::INVALID_DATA, "Invalid pinhole source camera");
        }
        const auto K_HEIGHT = frame.at("height").get<double>();
        const auto K_SCENE_SCALE = K_MANIFEST.at("dataset").at("scene_scale").get<double>();
        if (!std::isfinite(K_HEIGHT) || K_HEIGHT < 11 || K_HEIGHT > 4096 ||
            !std::isfinite(K_SCENE_SCALE) || K_SCENE_SCALE <= 0) {
            return fail(ErrorCode::INVALID_DATA, "Invalid source camera dimensions or scale");
        }
        std::array<double, 3> position {};
        for (std::size_t index { 0uz }; index < 3uz; ++index) {
            for (std::size_t row { 0uz }; row < 3uz; ++row) {
                position[index] -= K_VIEW[row][index] * K_VIEW[row][3];
            }
        }
        // Same native-COLMAP → canonical Rx(pi) as the appearance chunk.
        position[1] = -position[1];
        position[2] = -position[2];
        const nlohmann::json K_RESULT {
            { "schema_version", 2 },
            { "position", position },
            { "forward", { K_VIEW[2][0], -K_VIEW[2][1], -K_VIEW[2][2] } },
            { "up", { -K_VIEW[1][0], K_VIEW[1][1], K_VIEW[1][2] } },
            { "up_reference", "CAPTURE_ORIENTATION" },
            { "vertical_fov_radians", 2 * std::atan(K_HEIGHT / (2 * K_INTRINSIC[1][1])) },
            { "speed", std::clamp(K_SCENE_SCALE * 0.2, 0.01, 10000.) },
            { "frame_name", frame.at("name").get<std::string>() } };
        return K_RESULT.dump();
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, std::string("Fit camera invalid: ") + error.what());
    }
}

}  // namespace

Result<void> compile_splat_scene(const SceneImportRequest& request) {
    return publish_scene(request, { { { "source_kind", "gsplat_ply_import" } }, {}, {} });
}

Result<void> compile_fit_scene(const SceneImportRequest& request,
                               const FitImportExpectation& expectation) {
    const auto& result_manifest = request.m_input;
    const auto& destination = request.m_destination;
    try {
        if (!std::filesystem::is_regular_file(result_manifest) ||
            std::filesystem::file_size(result_manifest) > std::uintmax_t { 1024 } * 1024) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fit manifest missing or exceeds 1 MiB");
        }
        std::ifstream input(result_manifest);
        const auto K_MANIFEST = nlohmann::json::parse(input);
        if (K_MANIFEST.at("schema_version") != 1 || K_MANIFEST.at("status") != "complete" ||
            K_MANIFEST.at("recipe") != "local_gsplat_v1") {
            return fail(ErrorCode::INVALID_DATA, "Unsupported or incomplete fit manifest");
        }
        const auto K_IDENTITY = K_MANIFEST.at("identity").get<std::string>();
        const auto K_SOURCE_HASH = K_MANIFEST.at("source_sha256").get<std::string>();
        const auto K_PLY_HASH = K_MANIFEST.at("ply_sha256").get<std::string>();
        if (!valid_digest(K_IDENTITY) || !valid_digest(K_SOURCE_HASH) ||
            !valid_digest(K_PLY_HASH)) {
            return fail(ErrorCode::INVALID_DATA, "Invalid fit provenance digest");
        }
        if ((!expectation.m_capture_sha256.empty() &&
             K_SOURCE_HASH != expectation.m_capture_sha256) ||
            (!expectation.m_fit_identity.empty() && K_IDENTITY != expectation.m_fit_identity)) {
            return fail(ErrorCode::INVALID_DATA,
                        "Fit changed after job verification; previous scene retained");
        }
        if (!K_MANIFEST.at("steps").is_number_unsigned() ||
            !K_MANIFEST.at("splats").is_number_unsigned()) {
            return fail(ErrorCode::INVALID_DATA, "Invalid fit counts");
        }
        const auto K_STEPS = K_MANIFEST.at("steps").get<std::uint64_t>();
        const auto K_SPLATS = K_MANIFEST.at("splats").get<std::uint64_t>();
        if ((K_STEPS != 7000 && K_STEPS != 30000) || !K_SPLATS || K_SPLATS > 2000000) {
            return fail(ErrorCode::INVALID_DATA, "Fit counts exceed the supported profile");
        }
        const auto K_RELATIVE = std::filesystem::path(K_MANIFEST.at("ply").get<std::string>());
        if (K_RELATIVE.empty() || K_RELATIVE.is_absolute()) {
            return fail(ErrorCode::INVALID_DATA, "Fit asset path must be relative");
        }
        for (const auto& component : K_RELATIVE) {
            if (component == "..") {
                return fail(ErrorCode::INVALID_DATA, "Fit asset path escapes its job directory");
            }
        }
        const auto K_ROOT = std::filesystem::canonical(result_manifest).parent_path();
        const auto K_PLY = std::filesystem::canonical(K_ROOT / K_RELATIVE);
        const auto K_CONTAINED = K_PLY.lexically_relative(K_ROOT);
        if (K_CONTAINED.empty() || *K_CONTAINED.begin() == "..") {
            return fail(ErrorCode::INVALID_DATA, "Fit asset symlink escapes its job directory");
        }
        SplatImportContext context { { { "source_kind", "video_reconstruction" },
                                     { "capture_sha256", K_SOURCE_HASH },
                                     { "fit_identity", K_IDENTITY },
                                     { "recipe", "local_gsplat_v1" },
                                     { "fit_steps", std::to_string(K_STEPS) },
                                     { "scale_status", "ARBITRARY" },
                                     { "geometry", "UNAVAILABLE" },
                                     { "walk", "UNAVAILABLE" } },
                                    K_PLY_HASH,
                                    K_SPLATS };
        if (!expectation.m_inspection_completion_sha256.empty()) {
            if (!valid_digest(expectation.m_inspection_completion_sha256)) {
                return fail(ErrorCode::INVALID_ARGUMENT, "Invalid video inspection dependency");
            }
            context.m_provenance["inspection_completion_sha256"] =
                expectation.m_inspection_completion_sha256;
        }
        if (expectation.m_poses) {
            const auto& dependency = *expectation.m_poses;
            std::set<FrameId> frames;
            if (!valid_digest(dependency.m_asset_sha256) ||
                !valid_digest(dependency.m_pose_completion_sha256) || dependency.m_epoch.m_value ||
                dependency.m_revision.m_value != 1 || dependency.m_source_frames.empty() ||
                dependency.m_source_frames.size() > 120) {
                return fail(ErrorCode::INVALID_DATA, "Invalid initial fit pose dependency");
            }
            for (const auto K_FRAME : dependency.m_source_frames) {
                if (!K_FRAME.m_value || !frames.insert(K_FRAME).second) {
                    return fail(ErrorCode::INVALID_DATA, "Invalid fit source frame dependency");
                }
            }
            context.m_provenance["pose_asset_sha256"] = dependency.m_asset_sha256;
            context.m_provenance["pose_completion_sha256"] = dependency.m_pose_completion_sha256;
            context.m_provenance["map_epoch"] = std::to_string(dependency.m_epoch.m_value);
            context.m_provenance["map_revision"] = std::to_string(dependency.m_revision.m_value);
            context.m_poses = dependency;
        }
        const auto K_INPUT_MANIFEST = K_ROOT / "input_manifest.json";
        if (std::filesystem::exists(K_INPUT_MANIFEST)) {
            if (std::filesystem::canonical(K_INPUT_MANIFEST).parent_path() != K_ROOT) {
                return fail(ErrorCode::INVALID_DATA,
                            "Fit input manifest escapes its job directory");
            }
            const auto K_INSPECTION = inspection_view_metadata(K_INPUT_MANIFEST, K_SOURCE_HASH);
            if (!K_INSPECTION) {
                return std::unexpected(K_INSPECTION.error());
            }
            SceneState camera_check;
            camera_check.m_provenance["source_inspection_view"] = *K_INSPECTION;
            const auto K_CHECKED = source_view(camera_check);
            if (!K_CHECKED) {
                return std::unexpected(K_CHECKED.error());
            }
            context.m_provenance["source_inspection_view"] = *K_INSPECTION;
        }
        return publish_scene(
            { .m_input = K_PLY, .m_destination = destination, .m_stop = request.m_stop },
            std::move(context));
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Fit compilation failed: ") + error.what());
    }
}

}  // namespace world_explorer
