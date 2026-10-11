#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

namespace world_explorer {
namespace {
struct splat_import_context {
    std::map<std::string, std::string> provenance;
    std::string expected_hash;
    std::optional<std::uint64_t> expected_count;
};
result<void> publish_scene(const scene_import_request& request, splat_import_context context) {
    const auto& ply = request.input;
    const auto& destination = request.destination;
    const auto source_hash = sha256_file(ply, 8ULL * 1024 * 1024 * 1024);
    if (!source_hash)
        return std::unexpected(source_hash.error());
    if (!context.expected_hash.empty() && *source_hash != context.expected_hash)
        return fail(error_code::INVALID_DATA,
                    "Fit asset checksum mismatch; previous scene retained");
    std::uint64_t identity{};
    const auto converted =
        std::from_chars(source_hash->data(), source_hash->data() + 16, identity, 16);
    if (converted.ec != std::errc{})
        return fail(error_code::INVALID_DATA, "Invalid source digest");
    auto imported = load_gsplat_ply(ply, scene_id{identity ? identity : 1});
    if (!imported)
        return std::unexpected(imported.error());
    if (context.expected_count) {
        std::uint64_t count{};
        for (const auto& [id, chunk] : (*imported)->chunks) {
            (void)id;
            count += chunk->splats.size();
        }
        if (count != *context.expected_count)
            return fail(error_code::INVALID_DATA,
                        "Fit asset count differs from its completion manifest");
    }
    const auto rechecked_hash = sha256_file(ply, 8ULL * 1024 * 1024 * 1024);
    if (!rechecked_hash || *rechecked_hash != *source_hash)
        return fail(error_code::INVALID_DATA,
                    "Input asset changed during compilation; previous scene retained");
    auto scene = std::make_shared<scene_state>(**imported);
    scene->provenance = std::move(context.provenance);
    scene->provenance["input_sha256"] = *source_hash;
    scene->provenance["exporter"] = "gsplat-1.5.3";
    scene->provenance["quality_gate"] = "UNVERIFIED";
    return write_bundle(destination, scene);
}

bool valid_digest(const std::string& value) {
    return value.size() == 64 && std::ranges::all_of(value, [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

result<std::string> inspection_view_metadata(const std::filesystem::path& path,
                                             const std::string& source_hash) {
    try {
        if (std::filesystem::file_size(path) > std::uintmax_t{1024} * 1024)
            return fail(error_code::RESOURCE_EXHAUSTED, "Fit input manifest exceeds 1 MiB");
        std::ifstream input(path);
        const auto manifest = nlohmann::json::parse(input);
        if (manifest.at("source_sha256") != source_hash)
            return fail(error_code::INVALID_DATA, "Fit input and result source identities differ");
        const auto& frame = manifest.at("dataset").at("frames").at(0);
        const auto view = frame.at("view").get<std::array<std::array<double, 4>, 4>>();
        const auto intrinsic = frame.at("intrinsic").get<std::array<std::array<double, 3>, 3>>();
        for (const auto& row : view)
            for (const auto element : row)
                if (!std::isfinite(element))
                    return fail(error_code::INVALID_DATA, "Non-finite source camera transform");
        for (const auto& row : intrinsic)
            for (const auto element : row)
                if (!std::isfinite(element))
                    return fail(error_code::INVALID_DATA, "Non-finite source camera calibration");
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column) {
                double product{};
                for (std::size_t index = 0; index < 3; ++index)
                    product += view[row][index] * view[column][index];
                if (std::abs(product - (row == column ? 1. : 0.)) > 1e-5)
                    return fail(error_code::INVALID_DATA, "Non-orthonormal source camera rotation");
            }
        const vec3d row_0{view[0][0], view[0][1], view[0][2]};
        const vec3d row_1{view[1][0], view[1][1], view[1][2]};
        const vec3d row_2{view[2][0], view[2][1], view[2][2]};
        if (std::abs(dot(cross(row_0, row_1), row_2) - 1) > 1e-5)
            return fail(error_code::INVALID_DATA, "Reflected source camera rotation");
        if (view[3] != std::array<double, 4>{0, 0, 0, 1} || intrinsic[1][1] <= 0)
            return fail(error_code::INVALID_DATA, "Invalid pinhole source camera");
        const auto height = frame.at("height").get<double>();
        const auto scene_scale = manifest.at("dataset").at("scene_scale").get<double>();
        if (!std::isfinite(height) || height < 11 || height > 4096 || !std::isfinite(scene_scale) ||
            scene_scale <= 0)
            return fail(error_code::INVALID_DATA, "Invalid source camera dimensions or scale");
        std::array<double, 3> position{};
        for (std::size_t index = 0; index < 3; ++index)
            for (std::size_t row = 0; row < 3; ++row)
                position[index] -= view[row][index] * view[row][3];
        // Same native-COLMAP → canonical Rx(pi) as the appearance chunk.
        position[1] = -position[1];
        position[2] = -position[2];
        const nlohmann::json result{
            {"schema_version", 1},
            {"position", position},
            {"forward", {view[2][0], -view[2][1], -view[2][2]}},
            {"vertical_fov_radians", 2 * std::atan(height / (2 * intrinsic[1][1]))},
            {"speed", std::clamp(scene_scale * 0.2, 0.01, 10000.)},
            {"frame_name", frame.at("name").get<std::string>()}};
        return result.dump();
    } catch (const std::exception& error) {
        return fail(error_code::INVALID_DATA, std::string("Fit camera invalid: ") + error.what());
    }
}
} // namespace

result<void> compile_splat_scene(const scene_import_request& request) {
    return publish_scene(request, {{{"source_kind", "gsplat_ply_import"}}, {}, {}});
}

result<void> compile_fit_scene(const scene_import_request& request) {
    const auto& result_manifest = request.input;
    const auto& destination = request.destination;
    try {
        if (!std::filesystem::is_regular_file(result_manifest) ||
            std::filesystem::file_size(result_manifest) > std::uintmax_t{1024} * 1024)
            return fail(error_code::RESOURCE_EXHAUSTED, "Fit manifest missing or exceeds 1 MiB");
        std::ifstream input(result_manifest);
        const auto manifest = nlohmann::json::parse(input);
        if (manifest.at("schema_version") != 1 || manifest.at("status") != "complete" ||
            manifest.at("recipe") != "local_gsplat_v1")
            return fail(error_code::INVALID_DATA, "Unsupported or incomplete fit manifest");
        const auto identity = manifest.at("identity").get<std::string>();
        const auto source_hash = manifest.at("source_sha256").get<std::string>();
        const auto ply_hash = manifest.at("ply_sha256").get<std::string>();
        if (!valid_digest(identity) || !valid_digest(source_hash) || !valid_digest(ply_hash))
            return fail(error_code::INVALID_DATA, "Invalid fit provenance digest");
        if (!manifest.at("steps").is_number_unsigned() ||
            !manifest.at("splats").is_number_unsigned())
            return fail(error_code::INVALID_DATA, "Invalid fit counts");
        const auto steps = manifest.at("steps").get<std::uint64_t>();
        const auto splats = manifest.at("splats").get<std::uint64_t>();
        if ((steps != 7000 && steps != 30000) || !splats || splats > 2000000)
            return fail(error_code::INVALID_DATA, "Fit counts exceed the supported profile");
        const auto relative = std::filesystem::path(manifest.at("ply").get<std::string>());
        if (relative.empty() || relative.is_absolute())
            return fail(error_code::INVALID_DATA, "Fit asset path must be relative");
        for (const auto& component : relative)
            if (component == "..")
                return fail(error_code::INVALID_DATA, "Fit asset path escapes its job directory");
        const auto root = std::filesystem::canonical(result_manifest).parent_path();
        const auto ply = std::filesystem::canonical(root / relative);
        const auto contained = ply.lexically_relative(root);
        if (contained.empty() || *contained.begin() == "..")
            return fail(error_code::INVALID_DATA, "Fit asset symlink escapes its job directory");
        splat_import_context context{{{"source_kind", "video_reconstruction"},
                                      {"capture_sha256", source_hash},
                                      {"fit_identity", identity},
                                      {"recipe", "local_gsplat_v1"},
                                      {"fit_steps", std::to_string(steps)},
                                      {"scale_status", "ARBITRARY"},
                                      {"geometry", "UNAVAILABLE"},
                                      {"walk", "UNAVAILABLE"}},
                                     ply_hash,
                                     splats};
        const auto input_manifest = root / "input_manifest.json";
        if (std::filesystem::exists(input_manifest)) {
            if (std::filesystem::canonical(input_manifest).parent_path() != root)
                return fail(error_code::INVALID_DATA,
                            "Fit input manifest escapes its job directory");
            const auto inspection = inspection_view_metadata(input_manifest, source_hash);
            if (!inspection)
                return std::unexpected(inspection.error());
            scene_state camera_check;
            camera_check.provenance["source_inspection_view"] = *inspection;
            const auto checked = source_view(camera_check);
            if (!checked)
                return std::unexpected(checked.error());
            context.provenance["source_inspection_view"] = *inspection;
        }
        return publish_scene({.input = ply, .destination = destination}, std::move(context));
    } catch (const std::exception& error) {
        return fail(error_code::INVALID_DATA,
                    std::string("Fit compilation failed: ") + error.what());
    }
}
} // namespace world_explorer
