#pragma once

#include "evaluation_corpus_fixture.hpp"
#include "world_explorer/map_store/evaluation_inventory.hpp"

#include <array>
#include <chrono>
#include <functional>
#include <map>

namespace world_explorer::evaluation_inventory_test {

using namespace asset_corpus_test;

struct InventoryFixture {
    SelectionFixture m_views;
    RecipeFixture m_output;
    EvaluationInventorySelection m_selected;
    Json m_inventory, m_visibility;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;

    using Mutation = std::function<void(Json&, EvaluationInventorySelection&, Json&)>;
    using GraphMutation =
        std::function<void(AssetDependencyGraph&, const EvaluationInventorySelection&)>;

    struct ByteMutation {
        std::function<void(std::string&)> m_inventory, m_visibility;
    };

    explicit InventoryFixture(const Mutation& change = {},
                              const GraphMutation& graph = {},
                              const ByteMutation& bytes = {},
                              bool fully_occluded = false,
                              bool extra_view = false)
        : m_receipt(stage(change, graph, bytes, fully_occluded, extra_view)),
          m_capture(checked(m_views.inspect(m_receipt))) { }

    AdmittedAssetReceipt stage(const Mutation& change,
                               const GraphMutation& graph,
                               const ByteMutation& bytes,
                               bool fully_occluded,
                               bool extra_view) {
        if (fully_occluded) {
            m_views.m_object.m_mask["runs"][0uz]["class"] = "OCCLUDED";
        }
        if (extra_view) {
            auto& map = *m_views.m_object.m_map;
            auto observation = map.m_observations.at({ 10 });
            observation.m_id = { 20 };
            observation.m_timestamp = { 2, 1, 30 };
            observation.m_capture_time = std::chrono::nanoseconds { 18 };
            observation.m_image_sha256 =
                m_views.m_object.m_region.m_graph.m_products[1uz].m_product.m_content_sha256;
            auto pose = map.m_poses.at({ 10 });
            pose.m_frame = { 20 };
            pose.m_capture_time = observation.m_capture_time;
            map.m_observations.emplace(observation.m_id, observation);
            map.m_poses.emplace(pose.m_frame, pose);
            auto view = m_views.m_object.m_selection.m_view;
            view.m_frame = { 20 };
            view.m_image = m_views.m_object.m_region.m_graph.m_products[1uz].m_product;
            m_views.m_selection.m_frames.push_back({ view, CaptureFrameRole::EVALUATION_ONLY, {} });
        }
        const auto K_INITIAL = m_views.freeze();
        (void)K_INITIAL;
        m_selected.m_items = { { { 42 }, EvaluationItemKind::PROP, "chair", true },
                              { { 43 }, EvaluationItemKind::FLOOR, "floor", true },
                              { { 44 }, EvaluationItemKind::WALL, "wall", true },
                              { { 45 }, EvaluationItemKind::CEILING, "ceiling", false },
                              { { 46 }, EvaluationItemKind::OPENING, "opening", true },
                              { { 47 }, EvaluationItemKind::WALL, "wall", true } };
        for (const auto& frame : m_views.m_selection.m_frames) {
            if (frame.m_role == CaptureFrameRole::INFERENCE) {
                continue;
            }
            const auto& view = frame.m_view;
            for (const auto& item : m_selected.m_items) {
                const bool K_CHAIR { item.m_track == ObjectTrackId { 42 } &&
                                    view.m_frame == FrameId { 10 } };
                m_selected.m_cells.push_back(
                    { item.m_track,
                     view.m_capture,
                     view.m_frame,
                     K_CHAIR           ? fully_occluded ? EvaluationVisibility::FULLY_OCCLUDED
                                                        : EvaluationVisibility::VISIBLE
                     : item.m_required ? EvaluationVisibility::OUT_OF_VIEW
                                       : EvaluationVisibility::UNKNOWN,
                     K_CHAIR ? std::optional { m_views.m_object.m_selection.m_product }
                             : std::nullopt });
            }
        }
        auto& region = m_views.m_object.m_region;
        const std::string K_RECIPE {
            "synthetic evaluation inventory import; never inference labels" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        m_inventory = { { "schema_version", 1 },
                       { "world_epoch", 3 },
                       { "pose_revision", 5 },
                       { "recipe_sha256", hash(K_RECIPE) },
                       { "capture", region_ref(m_views.m_selection.m_product) },
                       { "manual_ancestor", false },
                       { "items", Json::array() } };
        constexpr std::array K_KINDS { "PROP", "FLOOR", "WALL", "CEILING", "OPENING" };
        for (const auto& item : m_selected.m_items) {
            m_inventory["items"].push_back(
                { { "track_id", item.m_track.m_value },
                 { "kind", K_KINDS[static_cast<std::size_t>(item.m_kind)] },
                 { "label", item.m_label },
                 { "required", item.m_required } });
        }
        m_visibility = { { "schema_version", 1 },
                        { "world_epoch", 3 },
                        { "pose_revision", 5 },
                        { "recipe_sha256", hash(K_RECIPE) },
                        { "capture", region_ref(m_views.m_selection.m_product) },
                        { "manual_ancestor", false },
                        { "inventory", nullptr },
                        { "views", Json::array() },
                        { "cells", Json::array() } };
        for (const auto& frame : m_views.m_selection.m_frames) {
            if (frame.m_role != CaptureFrameRole::INFERENCE) {
                m_visibility["views"].push_back(object_view(frame.m_view));
            }
        }
        constexpr std::array K_STATES { "VISIBLE", "FULLY_OCCLUDED", "OUT_OF_VIEW", "UNKNOWN" };
        for (const auto& cell : m_selected.m_cells) {
            m_visibility["cells"].push_back(
                { { "track_id", cell.m_track.m_value },
                 { "capture_id", cell.m_capture.m_value },
                 { "frame_id", cell.m_frame.m_value },
                 { "state", K_STATES[static_cast<std::size_t>(cell.m_state)] },
                 { "annotation",
                  cell.m_annotation ? region_ref(*cell.m_annotation) : Json(nullptr) } });
        }
        if (change) {
            change(m_inventory, m_selected, m_visibility);
        }
        auto inventory_bytes = m_inventory.dump();
        if (bytes.m_inventory) {
            bytes.m_inventory(inventory_bytes);
        }
        auto inventory = region.node(9000, inventory_bytes);
        inventory.m_recipe_sha256 = hash(K_RECIPE);
        inventory.m_dependencies = { m_views.m_selection.m_product };
        m_visibility["inventory"] = region_ref(inventory.m_product);
        auto visibility_bytes = m_visibility.dump();
        if (bytes.m_visibility) {
            bytes.m_visibility(visibility_bytes);
        }
        auto visibility = region.node(9001, visibility_bytes);
        visibility.m_recipe_sha256 = hash(K_RECIPE);
        std::map<AssetProductId, AssetRevisionRef> parents {
            { inventory.m_product.m_product, inventory.m_product },
            { m_views.m_object.m_selection.m_product.m_product,
             m_views.m_object.m_selection.m_product } };
        for (const auto& frame : m_views.m_selection.m_frames) {
            if (frame.m_role != CaptureFrameRole::INFERENCE) {
                parents.emplace(frame.m_view.m_image.m_product, frame.m_view.m_image);
                parents.emplace(frame.m_view.m_pose_asset.m_product, frame.m_view.m_pose_asset);
            }
        }
        for (const auto& [K_ID, K_REF] : parents) {
            visibility.m_dependencies.push_back(K_REF);
        }
        m_selected.m_inventory = inventory.m_product;
        m_selected.m_visibility = visibility.m_product;
        region.m_graph.m_products.insert(region.m_graph.m_products.end(), { inventory, visibility });
        if (graph) {
            m_views.m_change_graph = [&](AssetDependencyGraph& value) {
                graph(value, m_selected);
            };
        }
        // Only this synthetic fixture's owned tree is restaged.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto result = m_views.freeze();
        m_views.m_change_graph = {};
        return result;
    }

    Result<AdmittedEvaluationInventory> inspect(const std::stop_token& stop = {}) const {
        return inspect_evaluation_inventory(
            m_selected, m_capture, m_receipt, m_receipt.graph().m_context, stop);
    }
};

}  // namespace world_explorer::evaluation_inventory_test
