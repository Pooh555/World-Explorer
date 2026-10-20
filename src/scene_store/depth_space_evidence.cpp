#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/depth_space.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace world_explorer {
namespace {

using Json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void fields(const Json& value, std::initializer_list<const char*> names) {
    require(value.is_object() && value.size() == names.size(), "Unexpected depth-space fields");
    for (const auto* name : names) {
        require(value.contains(name), "Missing depth-space field");
    }
}

Json parse(std::string_view text) {
    require(text.size() <= K_DEPTH_SPACE_REPORT_BYTES && text.find('\0') == text.npos,
            "Depth-space text budget or raw NUL");
    std::vector<std::set<std::string>> keys;
    return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 16, "Depth-space nesting exceeds budget");
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.resize(K_DEPTH + 1uz);
            keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key) {
            require(keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second,
                    "Duplicate depth-space key");
        }
        return true;
    });
}

std::uint64_t integer(const Json& value) {
    require(value.is_number_unsigned(),
            "Depth-space counts and identities must be unsigned integers");
    return value.get<std::uint64_t>();
}

double real(const Json& value) {
    require(value.is_number() && std::isfinite(value.get<double>()), "Invalid depth-space scalar");
    return value.get<double>();
}

bool digest_valid(std::string_view value) {
    return value.size() == 64uz && value.find_first_not_of("0123456789abcdef") == value.npos;
}

std::string digest(const Json& value) {
    auto result = value.get<std::string>();
    require(digest_valid(result), "Invalid depth-space digest");
    return result;
}

template <std::size_t Size>
std::array<std::size_t, Size> counts(const Json& value, std::size_t maximum) {
    require(value.is_array() && value.size() == Size, "Invalid depth-space count array");
    std::array<std::size_t, Size> result {};
    for (std::size_t index { 0uz }; index < Size; ++index) {
        const auto K_COUNT = integer(value.at(index));
        require(K_COUNT <= maximum, "Depth-space count exceeds denominator");
        result[index] = static_cast<std::size_t>(K_COUNT);
    }
    return result;
}

std::filesystem::path directory(const std::filesystem::path& path) {
    const auto K_PATH = std::filesystem::absolute(path).lexically_normal();
    require(std::filesystem::canonical(K_PATH) == K_PATH && std::filesystem::is_directory(K_PATH),
            "Depth-space directory must be a real unlinked directory");
    return K_PATH;
}

const ObservedMeshSource& observed_source(const SceneState& scene) {
    require(!scene.m_world_frame && scene.m_scale == ScaleStatus::ARBITRARY &&
                scene.m_chunks.size() == 1uz,
            "Depth-space V1 requires one original arbitrary-scale raw source chunk");
    const auto& chunk = *scene.m_chunks.begin()->second;
    require(chunk.m_geometry_valid && chunk.m_mesh.has_value(),
            "Depth-space source geometry is invalid");
    if (!chunk.m_mesh || !chunk.m_mesh->m_observed) {
        throw std::runtime_error("Missing observed depth-space source");
    }
    const auto& mesh = *chunk.m_mesh;
    require(!mesh.m_baked && mesh.m_observed->m_cleanup_json.empty(),
            "Depth-space V1 requires raw source geometry");
    return *mesh.m_observed;
}

}  // namespace

Result<void> DepthSpaceEvidence::validate_source(const SceneState& current,
                                                 const std::stop_token& stop) const {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Depth-space source check cancelled");
    }
    if (current.m_id != m_source->m_id) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Depth-space source scene differs");
    }
    if (current.m_epoch != m_source->m_epoch) {
        return fail(ErrorCode::WRONG_EPOCH, "Depth-space source epoch changed; resync required");
    }
    if (current.m_revision != m_source->m_revision) {
        return fail(ErrorCode::WRONG_BASE, "Depth-space source revision changed; resync required");
    }
    const auto K_VALID = validate_scene(current);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (scene_content_hash(current) != m_source_sha256) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Depth-space source content changed");
    }
    return {};
}

Result<std::shared_ptr<const DepthSpaceEvidence>>
load_depth_space_evidence(const DepthSpaceLoad& request, const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Depth-space reload cancelled");
    }
    if (!request.m_source || !digest_valid(request.m_report_sha256) ||
        !digest_valid(request.m_request_sha256)) {
        return fail(ErrorCode::INVALID_ARGUMENT,
                    "Depth-space reload requires source and independent pins");
    }
    const auto K_VALID = validate_scene(*request.m_source);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    try {
        const auto& source = observed_source(*request.m_source);
        const auto K_DIRECTORY = directory(request.m_directory);
        const auto K_BYTES = asset_files::read_bytes(
            K_DIRECTORY / "manifest.json", K_DEPTH_SPACE_REPORT_BYTES, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        require(sha256(*K_BYTES) == request.m_report_sha256,
                "Depth-space terminal checksum differs");
        const auto K_REPORT = parse(
            std::string_view(reinterpret_cast<const char*>(K_BYTES->data()), K_BYTES->size()));
        fields(K_REPORT,
               { "kind",
                "request_sha256",
                "source_scene_sha256",
                "source_receipt",
                "grid",
                "recipe",
                "view_counts",
                "categories_unknown_front_blocked_conflict",
                "relation_codes",
                "relations_bytes",
                "relations_sha256",
                "diagnostic_manifest_sha256",
                "scale",
                "measured_collision",
                "walking_enabled" });
        require(K_REPORT.at("kind").get<std::string>() == "OBSERVED_DEPTH_SPACE_DIAGNOSTIC_V1" &&
                    K_REPORT.at("scale").get<std::string>() == "ARBITRARY" &&
                    K_REPORT.at("measured_collision").is_boolean() &&
                    !K_REPORT.at("measured_collision").get<bool>() &&
                    K_REPORT.at("walking_enabled").is_boolean() &&
                    !K_REPORT.at("walking_enabled").get<bool>(),
                "Depth-space diagnostic policy differs");
        require(digest(K_REPORT.at("request_sha256")) == request.m_request_sha256,
                "Depth-space request pin differs");
        const auto K_SOURCE_HASH = scene_content_hash(*request.m_source);
        if (digest(K_REPORT.at("source_scene_sha256")) != K_SOURCE_HASH) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Depth-space source changed; resync required");
        }
        const auto K_RECEIPT = parse(source.m_receipt_json);
        require(K_REPORT.at("source_receipt") == K_RECEIPT, "Depth-space source receipt differs");
        require(integer(K_RECEIPT.at("world_epoch")) == source.m_context.m_epoch.m_value &&
                    integer(K_RECEIPT.at("pose_revision")) ==
                        source.m_context.m_pose_revision.m_value &&
                    digest(K_RECEIPT.at("pose_asset_sha256")) == source.m_context.m_pose_sha256,
                "Depth-space pose context differs");
        const auto& frames = K_RECEIPT.at("frames");
        require(frames.is_array() && !frames.empty() && frames.size() <= 120uz,
                "Depth-space frame budget exceeded");
        const auto& source_frames = request.m_source->m_chunks.begin()->second->m_source_frames;
        require(source_frames.size() == frames.size(),
                "Depth-space source frame inventory differs");
        const auto K_GRID = make_depth_space_grid(*request.m_source, stop);
        if (!K_GRID) {
            return std::unexpected(K_GRID.error());
        }
        const auto& grid = K_REPORT.at("grid");
        fields(grid, { "origin", "cell_size", "dimensions", "order", "domain" });
        const auto& origin = grid.at("origin");
        require(origin.is_array() && origin.size() == 3uz, "Invalid depth-space origin");
        require(real(origin.at(0uz)) == K_GRID->m_origin.m_x &&
                    real(origin.at(1uz)) == K_GRID->m_origin.m_y &&
                    real(origin.at(2uz)) == K_GRID->m_origin.m_z &&
                    real(grid.at("cell_size")) == K_GRID->m_size &&
                    counts<3uz>(grid.at("dimensions"), 32uz) == K_GRID->m_dimensions &&
                    grid.at("order").get<std::string>() == "view-major; cells x then y then z" &&
                    grid.at("domain").get<std::string>() ==
                        "full source chunk bounds, rounded out to cells",
                "Depth-space grid differs from complete source extent");
        const auto K_MANIFEST = parse(source.m_manifest_json);
        const auto& recipe = K_REPORT.at("recipe");
        fields(recipe,
               { "minimum_support",
                "confidence",
                "sigma_multiplier",
                "world_guard",
                "missing_sigma",
                "footprint" });
        require(integer(recipe.at("minimum_support")) == 2 &&
                    integer(recipe.at("sigma_multiplier")) == 2 &&
                    real(recipe.at("world_guard")) > 0 &&
                    real(recipe.at("world_guard")) == real(K_MANIFEST.at("voxel_size")) &&
                    recipe.at("confidence").get<std::string>() ==
                        "positive recipe weight, not probability" &&
                    recipe.at("missing_sigma").get<std::string>() ==
                        "UNAVAILABLE; guard is not a confidence interval" &&
                    recipe.at("footprint").get<std::string>() ==
                        "outward floor/ceil with 1e-7 pixel guard; enclosing aligned power-of-two "
                        "mip blocks",
                "Depth-space recipe differs");
        require(K_REPORT.at("relation_codes") == Json::array({ "OUTSIDE_VIEW",
                                                              "UNKNOWN_DEPTH",
                                                              "FRONT_DIAGNOSTIC",
                                                              "SURFACE_BAND",
                                                              "BEHIND_SURFACE" }),
                "Depth-space relation code table differs");
        const auto K_CELL_COUNT = K_GRID->m_cells.size();
        const auto K_ATTEMPTS = K_CELL_COUNT * frames.size();
        require(integer(K_REPORT.at("relations_bytes")) == K_ATTEMPTS,
                "Depth-space ledger denominator differs");
        const auto K_LEDGER =
            asset_files::read_bytes(K_DIRECTORY / "relations.u8", K_DEPTH_SPACE_LEDGER_BYTES, stop);
        if (!K_LEDGER) {
            return std::unexpected(K_LEDGER.error());
        }
        require(K_LEDGER->size() == K_ATTEMPTS &&
                    sha256(*K_LEDGER) == digest(K_REPORT.at("relations_sha256")),
                "Depth-space ledger checksum or length differs");
        const auto& ledgers = K_REPORT.at("view_counts");
        require(ledgers.is_array() && ledgers.size() == frames.size(),
                "Depth-space view denominator differs");
        auto result = std::make_shared<DepthSpaceEvidence>(DepthSpaceEvidence::AdmissionToken {});
        result->m_source = request.m_source;
        result->m_source_sha256 = K_SOURCE_HASH;
        result->m_report_sha256 = request.m_report_sha256;
        result->m_grid = *K_GRID;
        result->m_cells.resize(K_CELL_COUNT);
        result->m_relations.reserve(K_ATTEMPTS);
        std::set<std::uint64_t> identities;
        for (std::size_t view { 0uz }; view < frames.size(); ++view) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Depth-space ledger admission cancelled");
            }
            const auto& frame = frames.at(view);
            const auto K_ID = integer(frame.at("frame"));
            require(K_ID != 0 && identities.insert(K_ID).second &&
                        source_frames[view].m_value == K_ID,
                    "Depth-space frame inventory is incomplete duplicate or reordered");
            const auto& entry = ledgers.at(view);
            fields(entry, { "frame", "uncertainty_available", "counts" });
            const auto K_SIGMA = frame.at("uncertainty").get<std::string>();
            require(K_SIGMA == "UNAVAILABLE" || K_SIGMA == "CAMERA_UNIT_STANDARD_DEVIATION",
                    "Invalid source uncertainty availability");
            require(integer(entry.at("frame")) == K_ID &&
                        entry.at("uncertainty_available").is_boolean() &&
                        entry.at("uncertainty_available").get<bool>() == (K_SIGMA != "UNAVAILABLE"),
                    "Depth-space view identity or uncertainty differs");
            result->m_frames.push_back({ K_ID });
            result->m_uncertainty.push_back(K_SIGMA != "UNAVAILABLE" ? 1 : 0);
            std::array<std::size_t, 5uz> actual {};
            for (std::size_t cell { 0uz }; cell < K_CELL_COUNT; ++cell) {
                const auto K_CODE =
                    std::to_integer<std::uint8_t>((*K_LEDGER)[view * K_CELL_COUNT + cell]);
                require(K_CODE < 5, "Depth-space ledger contains an invalid relation code");
                result->m_relations.push_back(static_cast<DepthCellRelation>(K_CODE));
                ++actual[K_CODE];
                ++result->m_cells[cell].m_counts[K_CODE];
            }
            require(counts<5uz>(entry.at("counts"), K_CELL_COUNT) == actual,
                    "Depth-space view counts differ from ledger");
        }
        for (const auto& cell : result->m_cells) {
            ++result->m_categories[static_cast<std::size_t>(cell.state())];
        }
        require(counts<4uz>(K_REPORT.at("categories_unknown_front_blocked_conflict"),
                            K_CELL_COUNT) == result->m_categories,
                "Depth-space categories differ from ledger");
        const auto K_EXPECTED =
            make_depth_space_diagnostic(request.m_source, result->m_cells, stop);
        if (!K_EXPECTED) {
            return std::unexpected(K_EXPECTED.error());
        }
        const auto K_DIAGNOSTIC_PATH = directory(K_DIRECTORY / "diagnostic.wexplore");
        const auto K_EXPECTED_BYTES = validate_scene(**K_EXPECTED);
        if (!K_EXPECTED_BYTES) {
            return std::unexpected(K_EXPECTED_BYTES.error());
        }
        BundleLimits limits;
        limits.m_scene.m_max_chunks = 2uz;
        limits.m_scene.m_max_splats = K_CELL_COUNT;
        const auto& source_chunk = *request.m_source->m_chunks.begin()->second;
        if (!source_chunk.m_mesh) {
            return fail(ErrorCode::INVALID_DATA, "Missing raw depth-space mesh");
        }
        const auto& source_mesh = *source_chunk.m_mesh;
        limits.m_scene.m_max_mesh_vertices = source_mesh.m_vertices.size();
        limits.m_scene.m_max_mesh_triangles = source_mesh.m_triangles.size();
        limits.m_scene.m_max_source_frames = frames.size();
        limits.m_scene.m_max_texture_texels = 0uz;
        limits.m_scene.m_max_payload_bytes =
            std::min(limits.m_scene.m_max_payload_bytes, *K_EXPECTED_BYTES + 4uz * 1024uz * 1024uz);
        const auto K_DIAGNOSTIC = load_bundle(
            K_DIAGNOSTIC_PATH, limits, digest(K_REPORT.at("diagnostic_manifest_sha256")));
        if (!K_DIAGNOSTIC) {
            return std::unexpected(K_DIAGNOSTIC.error());
        }
        require(scene_content_hash(**K_EXPECTED) == scene_content_hash(**K_DIAGNOSTIC),
                "Depth-space native scene differs from source and complete ledger");
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Depth-space reload cancelled before admission");
        }
        result->m_diagnostic = *K_DIAGNOSTIC;
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth-space evidence allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
