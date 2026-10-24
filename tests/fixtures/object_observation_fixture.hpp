#pragma once

#include "asset_region_fixture.hpp"
#include "world_explorer/map_store/object_observation.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer::object_observation_test {

using namespace asset_region_test;

inline Json object_view(const ObjectViewSelection& value) {
    return { { "capture_id", value.m_capture.m_value },
            { "frame_id", value.m_frame.m_value },
            { "calibration_id", value.m_calibration.m_value },
            { "frame_pose_revision", value.m_frame_pose_revision.m_value },
            { "image", region_ref(value.m_image) },
            { "pose_asset", region_ref(value.m_pose_asset) } };
}

inline Json object_grid(const ObjectImageGrid& value) {
    return { { "width", value.m_width },
            { "height", value.m_height },
            { "image_to_mask", value.m_image_to_mask } };
}

struct ObjectFixture {
    RegionFixture m_region;
    std::shared_ptr<MapState> m_map = std::make_shared<MapState>();
    ObjectObservationSelection m_selection;
    Json m_record, m_mask;
    std::optional<std::string> m_pose_bytes;
    std::function<void(AssetDependencyGraph&)> m_change_graph;

    ObjectFixture() {
        m_map->m_epoch = { 3 };
        m_map->m_revision = { 5 };
        m_map->m_calibrations.emplace(CalibrationId { 8 },
                                      CalibrationRecord { { 8 }, { 500, 500, 320, 240 }, 640, 480 });
        m_map->m_observations.emplace(
            FrameId { 10 },
            ObservationRecord { { 10 },
                               { 7 },
                               { 8 },
                               { 1, 1, 30 },
                               std::chrono::nanoseconds(17),
                               m_region.m_graph.m_products[0uz].m_product.m_content_sha256 });
        m_map->m_poses.emplace(FrameId { 10 },
                               PoseEstimate { { 10 },
                                             { 55 },
                                             { {}, { 1, 2, 3 }, 1 },
                                             std::chrono::nanoseconds(17),
                                             { 4 },
                                             TrackingState::TRACKING,
                                             {} });
        m_map->m_world_from_submap.emplace(SubmapId { 55 },
                                           SimilarityTransform { { 0, 1, 0, 0 }, { 10, -20, 30 }, 2.5 });
        for (auto id : { 5000, 5001, 5002, 5003, 5004 }) {
            m_region.m_graph.m_products.push_back(
                m_region.node(static_cast<std::uint64_t>(id), "synthetic object declaration"));
        }
        m_selection = { m_region.m_graph.m_products[6uz].m_product,
                       { 91 },
                       1,
                       { { 7 },
                        { 10 },
                        { 8 },
                        { 4 },
                        m_region.m_graph.m_products[0uz].m_product,
                        m_region.m_graph.m_products[4uz].m_product },
                       m_region.m_graph.m_products[5uz].m_product,
                       { 4, 2, { 1, 1, 0, 0 } },
                       { m_region.m_graph.m_products[7uz].m_product,
                        m_region.m_graph.m_products[8uz].m_product } };
        m_mask = { { "schema_version", 1 },
                  { "world_epoch", 3 },
                  { "pose_revision", 5 },
                  { "view", object_view(m_selection.m_view) },
                  { "grid", object_grid(m_selection.m_grid) },
                  { "runs",
                   Json::array({ { { "first", 0 }, { "count", 2 }, { "class", "VISIBLE" } },
                                { { "first", 2 }, { "count", 2 }, { "class", "OCCLUDED" } },
                                { { "first", 4 }, { "count", 2 }, { "class", "UNKNOWN" } },
                                { { "first", 6 }, { "count", 2 }, { "class", "BACKGROUND" } } }) },
                  { "manual_ancestor", false } };
        m_record = {
            { "schema_version", 1 },
            { "observation_id", 91 },
            { "revision", 1 },
            { "world_epoch", 3 },
            { "pose_revision", 5 },
            { "recipe_sha256", m_region.m_graph.m_products[6uz].m_recipe_sha256 },
            { "view", object_view(m_selection.m_view) },
            { "mask", region_ref(m_selection.m_mask) },
            { "grid", object_grid(m_selection.m_grid) },
            { "classes",
             Json::array({ { { "label", "chair" }, { "confidence", 0.75 } },
                          { { "label", "unknown" }, { "confidence", nullptr } } }) },
            { "associations",
             Json::array({ { { "track_id", 42 }, { "decision", "ACCEPTED" }, { "confidence", 0.75 } },
                          { { "track_id", 43 }, { "decision", "REJECTED" }, { "confidence", 1.0 } } }) },
            { "evidence",
             Json::array({ { { "product", region_ref(m_selection.m_evidence[0uz]) },
                           { "accepted", true },
                           { "reason", "VISIBLE_SUPPORT" } },
                          { { "product", region_ref(m_selection.m_evidence[1uz]) },
                           { "accepted", false },
                           { "reason", "OCCLUSION_CONFLICT" } } }) },
            { "manual_ancestor", false } };
    }

    void set_grid(ObjectImageGrid value) {
        m_selection.m_grid = value;
        m_record["grid"] = object_grid(value);
        m_mask["grid"] = object_grid(value);
    }

    void partition(std::uint64_t pixels, std::size_t count) {
        m_mask["runs"] = Json::array();
        for (std::size_t index { 0uz }; index < count; ++index) {
            m_mask["runs"].push_back({ { "first", index },
                                      { "count", index + 1uz == count ? pixels - index : 1 },
                                      { "class", "UNKNOWN" } });
        }
    }

    AdmittedAssetReceipt freeze(const std::optional<std::string>& raw_record = {},
                                const std::optional<std::string>& raw_mask = {}) {
        const auto K_POSE = encode_pose_asset(
            { m_map,
             { hash("synthetic video"), hash("synthetic decode"), hash("synthetic solve"), {} } });
        REQUIRE(K_POSE);
        const auto K_POSE_TEXT = m_pose_bytes.value_or(
            std::string(reinterpret_cast<const char*>(K_POSE->data()), K_POSE->size()));
        m_region.m_data[hash(K_POSE_TEXT)] = K_POSE_TEXT;
        m_region.m_graph.m_products[4uz].m_product.m_content_sha256 = hash(K_POSE_TEXT);
        m_selection.m_view.m_pose_asset = m_region.m_graph.m_products[4uz].m_product;
        m_record["view"]["pose_asset"] = region_ref(m_selection.m_view.m_pose_asset);
        m_mask["view"]["pose_asset"] = region_ref(m_selection.m_view.m_pose_asset);
        const auto K_MASK = raw_mask.value_or(m_mask.dump());
        m_region.m_data[hash(K_MASK)] = K_MASK;
        auto& mask = m_region.m_graph.m_products[5uz];
        mask.m_product.m_content_sha256 = hash(K_MASK);
        mask.m_dependencies = { m_selection.m_view.m_image, m_selection.m_view.m_pose_asset };
        m_selection.m_mask = mask.m_product;
        m_record["mask"] = region_ref(mask.m_product);
        const auto K_RECORD = raw_record.value_or(m_record.dump());
        m_region.m_data[hash(K_RECORD)] = K_RECORD;
        auto& target = m_region.m_graph.m_products[6uz];
        target.m_product.m_content_sha256 = hash(K_RECORD);
        target.m_dependencies = {
            m_selection.m_view.m_image, m_selection.m_view.m_pose_asset, m_selection.m_mask };
        target.m_dependencies.insert(target.m_dependencies.end(),
                                     m_selection.m_evidence.begin(),
                                     m_selection.m_evidence.end());
        m_selection.m_product = target.m_product;
        if (m_change_graph) {
            m_change_graph(m_region.m_graph);
        }
        return m_region.freeze();
    }

    Result<AdmittedObjectObservation> inspect(const AdmittedAssetReceipt& receipt,
                                              const std::stop_token& stop = {}) const {
        return inspect_object_observation(receipt, m_selection, m_region.m_graph.m_context, stop);
    }
};

}  // namespace world_explorer::object_observation_test
