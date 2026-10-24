#pragma once

#include "object_observation_fixture.hpp"
#include "world_explorer/map_store/capture_selection.hpp"

namespace world_explorer::capture_selection_test {

using namespace object_observation_test;

inline std::string role_name(CaptureFrameRole value) {
    if (value == CaptureFrameRole::INFERENCE) {
        return "INFERENCE";
    }
    return value == CaptureFrameRole::HELD_OUT ? "HELD_OUT" : "EVALUATION_ONLY";
}

struct SelectionFixture {
    ObjectFixture m_object;
    std::shared_ptr<MapState> m_inference_map;
    CaptureSelection m_selection;
    Json m_record;
    std::function<void(Json&)> m_change_record;
    std::function<void(AssetDependencyGraph&)> m_change_graph;
    std::optional<std::string> m_raw;
    std::size_t m_image_index {}, m_pose_index {}, m_rights_index {}, m_manifest_index {};

    SelectionFixture() {
        auto& region = m_object.m_region;
        m_image_index = region.m_graph.m_products.size();
        auto image = region.node(6001, "synthetic inference pixels");
        m_pose_index = m_image_index + 1uz;
        auto pose = region.node(6002, "replaced inference pose bytes");
        m_rights_index = m_image_index + 2uz;
        auto rights = region.node(6003, "synthetic rights; no actual consent or license approval");
        m_manifest_index = m_image_index + 3uz;
        auto manifest = region.node(6004, "replaced capture selection");
        // Opaque inference recipes remain separate from annotation-side imports.
        const std::string K_RECIPE { "synthetic inference import; not label bytes" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        image.m_recipe_sha256 = hash(K_RECIPE);
        pose.m_recipe_sha256 = hash(K_RECIPE);
        region.m_graph.m_products.insert(region.m_graph.m_products.end(),
                                         { image, pose, rights, manifest });
        m_inference_map = std::make_shared<MapState>(*m_object.m_map);
        auto input = m_inference_map->m_observations.at({ 10 });
        input.m_id = { 110 };
        input.m_capture = { 17 };
        input.m_image_sha256 = image.m_product.m_content_sha256;
        auto estimate = m_inference_map->m_poses.at({ 10 });
        estimate.m_frame = input.m_id;
        m_inference_map->m_observations = { { input.m_id, input } };
        m_inference_map->m_poses = { { estimate.m_frame, estimate } };
        m_selection = { manifest.m_product,
                       { 81 },
                       1,
                       { { { 7 }, hash("synthetic video"), rights.m_product },
                        { { 17 }, hash("synthetic inference video"), rights.m_product } },
                       { { { { 17 }, { 110 }, { 8 }, { 4 }, image.m_product, pose.m_product },
                         CaptureFrameRole::INFERENCE,
                         {} },
                        { m_object.m_selection.m_view, CaptureFrameRole::HELD_OUT, {} } },
                       {} };
    }

    AdmittedAssetReceipt freeze(bool annotated = true) {
        auto& region = m_object.m_region;
        region.m_graph.m_products[m_manifest_index].m_dependencies.clear();
        region.m_graph.m_products[m_manifest_index].m_manual_ancestor =
            region.m_graph.m_products[m_manifest_index].m_manual_action_sha256.has_value();
        auto staged = m_object.freeze();
        (void)staged;
        const auto K_POSE = encode_pose_asset({ m_inference_map,
                                               { hash("synthetic inference video"),
                                                hash("synthetic inference decode"),
                                                hash("synthetic inference solve"),
                                                {} } });
        REQUIRE(K_POSE);
        const std::string K_RAW_POSE(reinterpret_cast<const char*>(K_POSE->data()), K_POSE->size());
        region.m_data[hash(K_RAW_POSE)] = K_RAW_POSE;
        region.m_graph.m_products[m_pose_index].m_product.m_content_sha256 = hash(K_RAW_POSE);
        m_selection.m_frames[0uz].m_view.m_pose_asset =
            region.m_graph.m_products[m_pose_index].m_product;
        m_selection.m_frames[1uz].m_view = m_object.m_selection.m_view;
        for (auto& selected : m_selection.m_frames) {
            if (selected.m_view.m_pose_asset.m_product ==
                region.m_graph.m_products[m_pose_index].m_product.m_product) {
                selected.m_view.m_pose_asset = region.m_graph.m_products[m_pose_index].m_product;
            }
            if (selected.m_view.m_pose_asset.m_product ==
                m_object.m_selection.m_view.m_pose_asset.m_product) {
                selected.m_view.m_pose_asset = m_object.m_selection.m_view.m_pose_asset;
            }
        }
        m_selection.m_frames[1uz].m_annotations = annotated
                                                      ? std::vector { m_object.m_selection.m_product }
                                                      : std::vector<AssetRevisionRef> {};
        auto sources = Json::array();
        auto frames = Json::array();
        std::map<AssetProductId, AssetRevisionRef> parents;
        for (const auto& source : m_selection.m_sources) {
            sources.push_back({ { "capture_id", source.m_capture.m_value },
                               { "source_sha256", source.m_source_sha256 },
                               { "rights", region_ref(source.m_rights) } });
            parents[source.m_rights.m_product] = source.m_rights;
        }
        for (const auto& selected : m_selection.m_frames) {
            auto annotations = Json::array();
            for (const auto& ref : selected.m_annotations) {
                annotations.push_back(region_ref(ref));
                parents[ref.m_product] = ref;
            }
            frames.push_back({ { "view", object_view(selected.m_view) },
                              { "role", role_name(selected.m_role) },
                              { "annotations", annotations } });
            parents[selected.m_view.m_image.m_product] = selected.m_view.m_image;
            parents[selected.m_view.m_pose_asset.m_product] = selected.m_view.m_pose_asset;
        }
        Json measurement = nullptr;
        if (m_selection.m_measurement.has_value()) {
            measurement = region_ref(m_selection.m_measurement.value());
            parents[m_selection.m_measurement.value().m_product] =
                m_selection.m_measurement.value();
        }
        auto& target = region.m_graph.m_products[m_manifest_index];
        target.m_manual_ancestor = target.m_manual_action_sha256.has_value();
        for (const auto& [K_ID, K_REF] : parents) {
            for (const auto& node : region.m_graph.m_products) {
                if (node.m_product == K_REF) {
                    target.m_manual_ancestor = target.m_manual_ancestor || node.m_manual_ancestor;
                }
            }
        }
        m_record = { { "schema_version", 1 },
                    { "selection_id", 81 },
                    { "revision", m_selection.m_revision },
                    { "world_epoch", 3 },
                    { "pose_revision", 5 },
                    { "recipe_sha256", target.m_recipe_sha256 },
                    { "sources", sources },
                    { "frames", frames },
                    { "measurement", measurement },
                    { "manual_ancestor", target.m_manual_ancestor } };
        if (m_change_record) {
            m_change_record(m_record);
        }
        const auto K_RAW = m_raw.value_or(m_record.dump());
        region.m_data[hash(K_RAW)] = K_RAW;
        target.m_product.m_content_sha256 = hash(K_RAW);
        target.m_dependencies.clear();
        for (const auto& [K_ID, K_REF] : parents) {
            target.m_dependencies.push_back(K_REF);
        }
        m_selection.m_product = target.m_product;
        if (m_change_graph) {
            m_change_graph(region.m_graph);
        }
        return region.freeze();
    }

    std::vector<AdmittedObjectObservation> annotations(const AdmittedAssetReceipt& receipt) const {
        const auto K_RESULT = m_object.inspect(receipt);
        REQUIRE(K_RESULT);
        return { *K_RESULT };
    }

    Result<AdmittedCaptureSelection> inspect(const AdmittedAssetReceipt& receipt,
                                             bool annotated = true) const {
        return inspect_capture_selection(receipt,
                                         m_selection,
                                         annotated ? annotations(receipt)
                                                   : std::vector<AdmittedObjectObservation> {},
                                         m_object.m_region.m_graph.m_context);
    }
};

}  // namespace world_explorer::capture_selection_test
