#pragma once

#include "evaluation_inventory_fixture.hpp"
#include "world_explorer/map_store/controlled_references.hpp"

#include <array>
#include <map>

namespace world_explorer::controlled_references_test {

using namespace evaluation_inventory_test;

inline constexpr std::array K_KINDS { "BACKSIDE", "EMPTY_BACKGROUND", "CHANGED_LIGHT" };

inline Json controlled_pair(const ControlledReferencePair& value) {
    return { { "pair_id", value.m_id },
            { "subject_track_id", value.m_subject.m_value },
            { "baseline", object_view(value.m_baseline) },
            { "reference", object_view(value.m_reference) },
            { "baseline_condition", value.m_baseline_condition },
            { "reference_condition", value.m_reference_condition },
            { "reference_state",
             value.m_reference_state == ControlledSubjectState::PRESENT ? "PRESENT" : "REMOVED" },
            { "baseline_lighting",
             value.m_baseline_lighting ? region_ref(*value.m_baseline_lighting) : Json(nullptr) },
            { "reference_lighting",
             value.m_reference_lighting ? region_ref(*value.m_reference_lighting) : Json(nullptr) } };
}

struct ControlledFixture {
    InventoryFixture m_base { {}, {}, {}, false, true };
    RecipeFixture m_output;
    std::array<ControlledReferenceSelection, 3> m_selected;
    std::array<Json, 3> m_records, m_lighting_records;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    AdmittedEvaluationInventory m_inventory;

    using Mutation = std::function<void(Json&, ControlledReferenceSelection&)>;
    using LightMutation = std::function<void(Json&, std::size_t)>;
    using GraphMutation = std::function<void(AssetDependencyGraph&,
                                             const std::array<ControlledReferenceSelection, 3>&)>;

    struct ByteMutation {
        std::function<void(std::string&)> m_record, m_lighting;
    };

    explicit ControlledFixture(const Mutation& change = {},
                               const LightMutation& light = {},
                               const GraphMutation& graph = {},
                               const ByteMutation& bytes = {},
                               bool measured = true)
        : m_receipt(stage(change, light, graph, bytes, measured)),
          m_capture(checked(m_base.m_views.inspect(m_receipt))),
          m_inventory(checked(inspect_evaluation_inventory(
              m_base.m_selected, m_capture, m_receipt, m_receipt.graph().m_context))) { }

    AdmittedAssetReceipt stage(const Mutation& change,
                               const LightMutation& light,
                               const GraphMutation& graph,
                               const ByteMutation& bytes,
                               bool measured) {
        auto& region = m_base.m_views.m_object.m_region;
        const auto K_BASELINE = m_base.m_capture.inference().frames()[0uz].m_view;
        std::map<FrameId, ObjectViewSelection> views;
        for (const auto& frame : m_base.m_capture.evaluation()) {
            views.emplace(frame.m_inputs.m_view.m_frame, frame.m_inputs.m_view);
        }
        const std::array K_VIEWS { K_BASELINE, views.at({ 10 }), views.at({ 20 }) };
        const std::string K_RECIPE {
            "synthetic controlled-reference and measurement declarations; never inference" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        std::array<AssetRevisionRef, 3> lighting;
        for (std::size_t index { 0uz }; index < K_VIEWS.size(); ++index) {
            auto& record = m_lighting_records[index];
            record = { { "schema_version", 1 },
                      { "world_epoch", 3 },
                      { "pose_revision", 5 },
                      { "recipe_sha256", hash(K_RECIPE) },
                      { "view", object_view(K_VIEWS[index]) },
                      { "illuminance",
                       { { "value", 100.0 + static_cast<double>(index) * 50.0 }, { "unit", "lux" } } },
                      { "color_temperature", { { "value", 4000 }, { "unit", "kelvin" } } },
                      { "manual_ancestor", false } };
            if (light) {
                light(record, index);
            }
            auto text = record.dump();
            if (bytes.m_lighting) {
                bytes.m_lighting(text);
            }
            auto node = region.node(9300uz + index, text);
            node.m_recipe_sha256 = hash(K_RECIPE);
            node.m_dependencies = { K_VIEWS[index].m_image, K_VIEWS[index].m_pose_asset };
            lighting[index] = node.m_product;
            region.m_graph.m_products.push_back(node);
        }
        for (std::size_t index { 0uz }; index < m_selected.size(); ++index) {
            auto& selected = m_selected[index];
            selected.m_kind = static_cast<ControlledReferenceKind>(index);
            const bool K_EMPTY { selected.m_kind == ControlledReferenceKind::EMPTY_BACKGROUND };
            const auto K_REFERENCE = K_VIEWS[K_EMPTY ? 2uz : 1uz];
            selected.m_pairs = {
                { 1,
                 { 42 },
                 K_BASELINE,
                 K_REFERENCE,
                 "capture_light",
                 K_EMPTY ? "capture_light" : "inspection_light",
                 K_EMPTY ? ControlledSubjectState::REMOVED : ControlledSubjectState::PRESENT,
                 measured ? std::optional { lighting[0uz] } : std::nullopt,
                 measured ? std::optional { lighting[K_EMPTY ? 2uz : 1uz] } : std::nullopt } };
            auto& record = m_records[index];
            record = { { "schema_version", 1 },
                      { "world_epoch", 3 },
                      { "pose_revision", 5 },
                      { "recipe_sha256", hash(K_RECIPE) },
                      { "capture", region_ref(m_base.m_capture.record().m_selection.m_product) },
                      { "inventory", region_ref(m_base.m_selected.m_inventory) },
                      { "visibility", region_ref(m_base.m_selected.m_visibility) },
                      { "kind", K_KINDS[index] },
                      { "pairs", Json::array({ controlled_pair(selected.m_pairs[0uz]) }) },
                      { "manual_ancestor", false } };
            if (change) {
                change(record, selected);
            }
            auto text = record.dump();
            if (bytes.m_record) {
                bytes.m_record(text);
            }
            auto node = region.node(9400uz + index, text);
            node.m_recipe_sha256 = hash(K_RECIPE);
            std::map<AssetProductId, AssetRevisionRef> parents {
                { m_base.m_capture.record().m_selection.m_product.m_product,
                 m_base.m_capture.record().m_selection.m_product },
                { m_base.m_selected.m_inventory.m_product, m_base.m_selected.m_inventory },
                { m_base.m_selected.m_visibility.m_product, m_base.m_selected.m_visibility } };
            for (const auto& pair : selected.m_pairs) {
                for (const auto& ref : { pair.m_baseline.m_image,
                                        pair.m_baseline.m_pose_asset,
                                        pair.m_reference.m_image,
                                        pair.m_reference.m_pose_asset }) {
                    parents.emplace(ref.m_product, ref);
                }
                for (const auto& ref : { pair.m_baseline_lighting, pair.m_reference_lighting }) {
                    if (ref) {
                        parents.emplace(ref->m_product, *ref);
                    }
                }
            }
            for (const auto& [K_ID, K_REF] : parents) {
                node.m_dependencies.push_back(K_REF);
            }
            selected.m_product = node.m_product;
            region.m_graph.m_products.push_back(node);
        }
        if (graph) {
            m_base.m_views.m_change_graph = [&](AssetDependencyGraph& value) {
                graph(value, m_selected);
            };
        }
        // Restage only this synthetic fixture's owned tree; no production source repair.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto receipt = m_base.m_views.freeze();
        m_base.m_views.m_change_graph = {};
        return receipt;
    }

    Result<AdmittedControlledReference>
    inspect(ControlledReferenceKind kind = ControlledReferenceKind::BACKSIDE,
            const std::stop_token& stop = {}) const {
        return inspect_controlled_reference(m_selected[static_cast<std::size_t>(kind)],
                                            m_inventory,
                                            m_capture,
                                            m_receipt,
                                            m_receipt.graph().m_context,
                                            stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "controlled";
    }
};

}  // namespace world_explorer::controlled_references_test
