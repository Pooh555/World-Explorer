#include "world_explorer/application/collision_inspection.hpp"

#include "collision_inputs.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/collision_support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace world_explorer {
namespace {

using Json = nlohmann::json;

using collision_inputs::fields;
using collision_inputs::real;
using collision_inputs::vector;

SceneSnapshot diagnostic_scene(const CollisionSupport& support) {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 0x434f4c4c };
    scene->m_epoch = { 1 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "collision_support_v1" },
                           { "collision_support",
                            "Synthetic space: green free, red occupied, blue unknown; amber "
                            "unsupported floor. Walking unavailable." },
                           { "source_scene_sha256", support.source_sha256() },
                           { "support_document_sha256", support.document_sha256() } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    const auto K_SIZE = support.dimensions();
    chunk->m_bounds = { {},
                       { static_cast<double>(K_SIZE[0uz]),
                        static_cast<double>(K_SIZE[1uz]),
                        static_cast<double>(K_SIZE[2uz]) } };
    chunk->m_world_from_chunk.m_translation = support.origin();
    chunk->m_world_from_chunk.m_scale = support.cell_size();

    struct DiagnosticSplat {
        std::array<float, 3uz> m_position, m_scale, m_color;
        float m_opacity {};
    };

    const auto K_SPLAT = [](const DiagnosticSplat& input) {
        Gaussian splat;
        splat.m_mean = input.m_position;
        splat.m_scale = input.m_scale;
        splat.m_opacity = input.m_opacity;
        for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
            splat.m_sh[channel] = (input.m_color[channel] - .5F) / .2820947918F;
        }
        return splat;
    };
    for (std::size_t z { 0uz }; z < K_SIZE[2uz]; ++z) {
        for (std::size_t x { 0uz }; x < K_SIZE[0uz]; ++x) {
            const bool K_SUPPORTED { support.floors()[x + K_SIZE[0uz] * z] != 0 };
            chunk->m_splats.push_back(
                K_SPLAT({ { static_cast<float>(x) + .5F, 0, static_cast<float>(z) + .5F },
                         { .45F, .015F, .45F },
                         K_SUPPORTED ? std::array { .1F, .8F, .15F } : std::array { 1.F, .5F, .05F },
                         .9F }));
            for (std::size_t y { 0uz }; y < K_SIZE[1uz]; ++y) {
                const auto K_STATE = support.cells()[x + K_SIZE[0uz] * (y + K_SIZE[1uz] * z)];
                const auto K_COLOR = K_STATE == SpaceState::FREE ? std::array { .1F, .8F, .15F }
                                     : K_STATE == SpaceState::OCCUPIED
                                         ? std::array { 1.F, .05F, .05F }
                                         : std::array { .1F, .3F, 1.F };
                chunk->m_splats.push_back(K_SPLAT({ { static_cast<float>(x) + .5F,
                                                    static_cast<float>(y) + .5F,
                                                    static_cast<float>(z) + .5F },
                                                   { .22F, .22F, .22F },
                                                   K_COLOR,
                                                   K_STATE == SpaceState::FREE ? .06F : .75F }));
            }
        }
    }
    scene->m_chunks.emplace(chunk->m_id, std::move(chunk));
    return scene;
}

}  // namespace

Result<void> inspect_collision_support(const std::filesystem::path& request,
                                       const std::string& expected_sha256,
                                       const std::filesystem::path& destination,
                                       const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Collision inspection cancelled");
        }
        if (destination.empty() ||
            std::filesystem::exists(std::filesystem::symlink_status(destination))) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Choose a new collision inspection directory");
        }
        const auto K_READ = collision_inputs::read_request(request, expected_sha256, stop);
        if (!K_READ) {
            return std::unexpected(K_READ.error());
        }
        const auto& request_document = *K_READ;
        fields(request_document,
               { "kind",
                "source_bundle",
                "source_manifest_sha256",
                "support",
                "support_sha256",
                "probes" });
        if (request_document.at("kind") != "COLLISION_SUPPORT_INSPECTION_V1") {
            throw std::runtime_error("Unsupported collision inspection request");
        }
        const auto K_INPUTS = collision_inputs::load(request, request_document, stop);
        if (!K_INPUTS) {
            return std::unexpected(K_INPUTS.error());
        }
        const auto& scene_snapshot = K_INPUTS->m_source;
        const auto& support = K_INPUTS->m_support;
        const auto K_OUTPUT =
            std::filesystem::weakly_canonical(std::filesystem::absolute(destination));
        const auto K_RELATIVE = K_OUTPUT.lexically_relative(K_INPUTS->m_source_path);
        if (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Collision diagnostic overlaps immutable source");
        }
        const auto K_MANIFEST_HASH =
            request_document.at("source_manifest_sha256").get<std::string>();
        const auto& probes = request_document.at("probes");
        if (!probes.is_array() || probes.size() > 256uz) {
            throw std::runtime_error("Collision inspection probe budget exceeded");
        }
        Json results = Json::array();
        for (const auto& probe : probes) {
            fields(probe, { "start_feet", "end_feet", "radius", "height" });
            const auto K_RESULT = probe_collision_support(*support,
                                                          *scene_snapshot,
                                                          { vector(probe.at("start_feet")),
                                                           vector(probe.at("end_feet")),
                                                           real(probe.at("radius")),
                                                           real(probe.at("height")) },
                                                          stop);
            if (!K_RESULT) {
                return std::unexpected(K_RESULT.error());
            }
            results.push_back({ { "state", clearance_label(K_RESULT->m_state) },
                               { "checked_cells", K_RESULT->m_checked_cells } });
        }
        auto diagnostic = diagnostic_scene(*support);
        const auto K_VALID = validate_scene(*diagnostic);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Collision inspection cancelled before publication");
        }
        if (!std::filesystem::create_directory(K_OUTPUT)) {
            return fail(ErrorCode::IO_ERROR, "Collision inspection output was not reserved");
        }
        const auto K_WRITTEN = write_bundle(K_OUTPUT / "diagnostic.wexplore", diagnostic);
        if (!K_WRITTEN) {
            return K_WRITTEN;
        }
        const auto K_RELOADED = load_bundle(K_OUTPUT / "diagnostic.wexplore");
        if (!K_RELOADED || scene_content_hash(**K_RELOADED) != scene_content_hash(*diagnostic)) {
            return fail(ErrorCode::INVALID_DATA, "Collision diagnostic failed independent reload");
        }
        const auto K_COUNTS = [&](SpaceState state) {
            return std::ranges::count(support->cells(), state);
        };
        const auto K_DIAGNOSTIC_HASH =
            sha256_file(K_OUTPUT / "diagnostic.wexplore/manifest.json", 1024uz * 1024uz, stop);
        if (!K_DIAGNOSTIC_HASH) {
            return std::unexpected(K_DIAGNOSTIC_HASH.error());
        }
        const Json K_REPORT {
            { "kind", "SYNTHETIC_COLLISION_INSPECTION_V1" },
            { "request_sha256", expected_sha256 },
            { "source_manifest_sha256", K_MANIFEST_HASH },
            { "source_scene_sha256", support->source_sha256() },
            { "support_sha256", support->document_sha256() },
            { "cells",
             { { "free", K_COUNTS(SpaceState::FREE) },
              { "occupied", K_COUNTS(SpaceState::OCCUPIED) },
              { "unknown", K_COUNTS(SpaceState::UNKNOWN) } } },
            { "supported_floor_tiles", std::ranges::count(support->floors(), std::uint8_t { 1 }) },
            { "probes", results },
            { "diagnostic_manifest_sha256", *K_DIAGNOSTIC_HASH },
            { "walking_enabled", false },
            { "measured_collision", false } };
        const auto K_REPORT_TEXT = K_REPORT.dump(2);
        const auto K_PUBLISHED = asset_files::publish_asset(
            K_OUTPUT / "manifest.json", std::as_bytes(std::span(K_REPORT_TEXT)), stop);
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Collision inspection allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
