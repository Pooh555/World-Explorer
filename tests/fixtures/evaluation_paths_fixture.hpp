#pragma once

#include "evaluation_corpus_fixture.hpp"
#include "world_explorer/map_store/evaluation_paths.hpp"

#include <array>
#include <map>

namespace world_explorer::evaluation_paths_test {

using namespace asset_corpus_test;

inline Json path_transform(const SimilarityTransform& value) {
    const auto& q = value.m_rotation;
    return { { "rotation", { q.m_w, q.m_x, q.m_y, q.m_z } },
            { "translation",
             { value.m_translation.m_x, value.m_translation.m_y, value.m_translation.m_z } },
            { "scale", value.m_scale } };
}

inline Json path_camera(const CaptureFrameInputs& value) {
    const auto& timestamp = value.m_observation.m_timestamp;
    return { { "view", object_view(value.m_view) },
            { "pts", timestamp.m_pts },
            { "time_base", { timestamp.m_time_base_numerator, timestamp.m_time_base_denominator } },
            { "capture_time_ns", value.m_observation.m_capture_time.count() },
            { "world_from_camera", path_transform(value.m_world_from_camera) } };
}

struct PathsFixture {
    SelectionFixture m_views;
    RecipeFixture m_output;
    EvaluationPathSelection m_selected;
    Json m_temporal, m_inspection;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;

    using Mutation = std::function<void(Json&, EvaluationPathSelection&, Json&)>;
    using GraphMutation =
        std::function<void(AssetDependencyGraph&, const EvaluationPathSelection&)>;
    using CaptureMutation = std::function<void(SelectionFixture&)>;

    struct ByteMutation {
        std::function<void(std::string&)> m_temporal, m_inspection;
    };

    explicit PathsFixture(const Mutation& change = {},
                          const GraphMutation& graph = {},
                          const ByteMutation& bytes = {},
                          const CaptureMutation& source = {})
        : m_receipt(stage(change, graph, bytes, source)),
          m_capture(checked(m_views.inspect(m_receipt, false))) { }

    AdmittedAssetReceipt stage(const Mutation& change,
                               const GraphMutation& graph,
                               const ByteMutation& bytes,
                               const CaptureMutation& source) {
        auto& region = m_views.m_object.m_region;
        auto evaluation_map = std::make_shared<MapState>(*m_views.m_inference_map);
        evaluation_map->m_observations.clear();
        evaluation_map->m_poses.clear();
        const auto K_ADD = [&](MapState& map,
                               std::uint64_t id,
                               CaptureId capture,
                               SourceTimestamp timestamp,
                               std::chrono::nanoseconds capture_time,
                               AssetRevisionRef image,
                               AssetRevisionRef pose,
                               CaptureFrameRole role) {
            auto observation = m_views.m_inference_map->m_observations.at({ 110 });
            observation.m_id = { id };
            observation.m_capture = capture;
            observation.m_timestamp = timestamp;
            observation.m_capture_time = capture_time;
            observation.m_image_sha256 = image.m_content_sha256;
            auto estimate = m_views.m_inference_map->m_poses.at({ 110 });
            estimate.m_frame = { id };
            estimate.m_capture_time = observation.m_capture_time;
            estimate.m_submap_from_camera.m_translation.m_x += static_cast<double>(id);
            map.m_observations.emplace(observation.m_id, observation);
            map.m_poses.emplace(estimate.m_frame, estimate);
            m_views.m_selection.m_frames.push_back(
                { { capture, { id }, { 8 }, { 4 }, image, pose }, role, {} });
        };
        auto image_a = region.node(9100, "synthetic temporal holdout pixels a");
        auto pose = region.node(9101, "replaced temporal holdout poses");
        auto image_b = region.node(9102, "synthetic temporal holdout pixels b");
        auto image_c = region.node(9103, "synthetic independent inspection pixels");
        auto image_d = region.node(9104, "synthetic evaluation-only pixels");
        region.m_graph.m_products.insert(region.m_graph.m_products.end(),
                                         { image_a, pose, image_b, image_c, image_d });
        K_ADD(*evaluation_map,
              120,
              { 17 },
              { 2, 1, 30 },
              std::chrono::nanoseconds { 18 },
              image_a.m_product,
              pose.m_product,
              CaptureFrameRole::HELD_OUT);
        K_ADD(*evaluation_map,
              121,
              { 17 },
              { 3, 1, 30 },
              std::chrono::nanoseconds { 19 },
              image_b.m_product,
              pose.m_product,
              CaptureFrameRole::HELD_OUT);
        const auto K_BYTES = checked(encode_pose_asset({ evaluation_map,
                                                        { hash("synthetic inference video"),
                                                         hash("synthetic holdout decode"),
                                                         hash("synthetic holdout localization"),
                                                         {} } }));
        const std::string K_TEXT(reinterpret_cast<const char*>(K_BYTES.data()), K_BYTES.size());
        region.m_data[hash(K_TEXT)] = K_TEXT;
        const auto K_POSE_INDEX = m_views.m_manifest_index + 2uz;
        region.m_graph.m_products[K_POSE_INDEX].m_product.m_content_sha256 = hash(K_TEXT);
        for (auto& frame : m_views.m_selection.m_frames) {
            if (frame.m_view.m_pose_asset.m_product == pose.m_product.m_product) {
                frame.m_view.m_pose_asset = region.m_graph.m_products[K_POSE_INDEX].m_product;
            }
        }
        K_ADD(*m_views.m_object.m_map,
              20,
              { 7 },
              { 2, 1, 30 },
              std::chrono::nanoseconds { 18 },
              image_c.m_product,
              m_views.m_object.m_selection.m_view.m_pose_asset,
              CaptureFrameRole::HELD_OUT);
        K_ADD(*m_views.m_object.m_map,
              30,
              { 7 },
              { 3, 1, 30 },
              std::chrono::nanoseconds { 19 },
              image_d.m_product,
              m_views.m_object.m_selection.m_view.m_pose_asset,
              CaptureFrameRole::EVALUATION_ONLY);
        if (source) {
            source(m_views);
        }
        const auto K_INITIAL = m_views.freeze(false);
        const auto K_CAPTURE = checked(m_views.inspect(K_INITIAL, false));
        m_selected.m_blocks = { { 1, { 17 }, CaptureFrameRole::INFERENCE, { { 110 } } },
                               { 2, { 17 }, CaptureFrameRole::HELD_OUT, { { 120 }, { 121 } } },
                               { 3, { 7 }, CaptureFrameRole::HELD_OUT, { { 10 }, { 20 } } },
                               { 4, { 7 }, CaptureFrameRole::EVALUATION_ONLY, { { 30 } } } };
        m_selected.m_path = { { { 7 }, { 10 }, EvaluationPoseConfidence::UNCERTAIN },
                             { { 7 }, { 20 }, EvaluationPoseConfidence::DECLARED_RELIABLE } };
        std::map<std::pair<CaptureId, FrameId>, Json> cameras;
        for (const auto& inputs : K_CAPTURE.inference().frames()) {
            cameras.emplace(std::pair { inputs.m_view.m_capture, inputs.m_view.m_frame },
                            path_camera(inputs));
        }
        for (const auto& frame : K_CAPTURE.evaluation()) {
            cameras.emplace(
                std::pair { frame.m_inputs.m_view.m_capture, frame.m_inputs.m_view.m_frame },
                path_camera(frame.m_inputs));
        }
        const std::string K_RECIPE { "synthetic evaluation path declarations; not inference" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        const auto K_HEADER = [&] {
            return Json { { "schema_version", 1 },
                         { "world_epoch", 3 },
                         { "pose_revision", 5 },
                         { "recipe_sha256", hash(K_RECIPE) },
                         { "capture", region_ref(m_views.m_selection.m_product) },
                         { "manual_ancestor", false } };
        };
        m_temporal = K_HEADER();
        m_temporal["blocks"] = Json::array();
        for (const auto& block : m_selected.m_blocks) {
            auto frames = Json::array();
            for (auto id : block.m_frames) {
                frames.push_back(cameras.at({ block.m_capture, id }));
            }
            const auto K_SOURCE = std::ranges::find(
                m_views.m_selection.m_sources, block.m_capture, &CaptureSourceSelection::m_capture);
            if (K_SOURCE == m_views.m_selection.m_sources.end()) {
                throw std::logic_error("Missing fixture source");
            }
            m_temporal["blocks"].push_back({ { "block_id", block.m_id },
                                            { "capture_id", block.m_capture.m_value },
                                            { "source_sha256", K_SOURCE->m_source_sha256 },
                                            { "role", role_name(block.m_role) },
                                            { "frames", frames } });
        }
        m_inspection = K_HEADER();
        m_inspection["path"] = { { "temporal", nullptr }, { "entries", Json::array() } };
        constexpr std::array K_CONFIDENCE { "UNASSESSED", "UNCERTAIN", "DECLARED_RELIABLE" };
        for (const auto& entry : m_selected.m_path) {
            m_inspection["path"]["entries"].push_back(
                { { "camera", cameras.at({ entry.m_capture, entry.m_frame }) },
                 { "confidence", K_CONFIDENCE[static_cast<std::size_t>(entry.m_confidence)] } });
        }
        if (change) {
            change(m_temporal, m_selected, m_inspection);
        }
        auto temporal_bytes = m_temporal.dump();
        if (bytes.m_temporal) {
            bytes.m_temporal(temporal_bytes);
        }
        auto temporal = region.node(9200, temporal_bytes);
        temporal.m_recipe_sha256 = hash(K_RECIPE);
        temporal.m_dependencies = { m_views.m_selection.m_product };
        m_inspection["path"]["temporal"] = region_ref(temporal.m_product);
        auto inspection_bytes = m_inspection.dump();
        if (bytes.m_inspection) {
            bytes.m_inspection(inspection_bytes);
        }
        auto inspection = region.node(9201, inspection_bytes);
        inspection.m_recipe_sha256 = hash(K_RECIPE);
        std::map<AssetProductId, AssetRevisionRef> parents {
            { temporal.m_product.m_product, temporal.m_product } };
        for (const auto& frame : K_CAPTURE.evaluation()) {
            if (frame.m_inputs.m_view.m_capture == CaptureId { 7 } &&
                frame.m_role == CaptureFrameRole::HELD_OUT) {
                parents.emplace(frame.m_inputs.m_view.m_image.m_product,
                                frame.m_inputs.m_view.m_image);
                parents.emplace(frame.m_inputs.m_view.m_pose_asset.m_product,
                                frame.m_inputs.m_view.m_pose_asset);
            }
        }
        for (const auto& [K_ID, K_REF] : parents) {
            inspection.m_dependencies.push_back(K_REF);
        }
        m_selected.m_temporal = temporal.m_product;
        m_selected.m_inspection = inspection.m_product;
        region.m_graph.m_products.insert(region.m_graph.m_products.end(), { temporal, inspection });
        if (graph) {
            m_views.m_change_graph = [&](AssetDependencyGraph& value) {
                graph(value, m_selected);
            };
        }
        // Only the fixture-owned synthetic blob tree is restaged.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto receipt = m_views.freeze(false);
        m_views.m_change_graph = {};
        return receipt;
    }

    Result<AdmittedEvaluationPaths> inspect(const std::stop_token& stop = {}) const {
        return inspect_evaluation_paths(
            m_selected, m_capture, m_receipt, m_receipt.graph().m_context, stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "paths";
    }
};

}  // namespace world_explorer::evaluation_paths_test
