#pragma once

#include "evaluation_paths_fixture.hpp"
#include "world_explorer/map_store/evaluation_protocol.hpp"

namespace world_explorer::evaluation_protocol_test {

using namespace evaluation_paths_test;
inline constexpr std::array K_TRACKS { "OBSERVED", "COMPLETED" };
inline constexpr std::array K_ARTIFACTS {
    "GEOMETRY_SAMPLER", "UV_SAMPLER", "RENDERER", "TONE_MAPPER", "METRICS", "REVIEW_RUBRIC" };

inline Json protocol_mask_pin(const EvaluationMaskPin& pin) {
    return { { "track", K_TRACKS[static_cast<std::size_t>(pin.m_track)] },
            { "view", object_view(pin.m_view) },
            { "product", region_ref(pin.m_product) } };
}

inline Json protocol_light(const EvaluationLightPin& pin) {
    return { { "label", pin.m_label },
            { "product", region_ref(pin.m_product) },
            { "seed", pin.m_seed },
            { "near_light", pin.m_near_light } };
}

struct ProtocolFixture {
    PathsFixture m_base;
    RecipeFixture m_output;
    EvaluationProtocolSelection m_selected;
    Json m_record;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    using Mutation = std::function<void(Json&, EvaluationProtocolSelection&)>;
    using MaskMutation = std::function<void(Json&, const EvaluationMaskPin&)>;
    using GraphMutation =
        std::function<void(AssetDependencyGraph&, const EvaluationProtocolSelection&)>;

    struct ByteMutation {
        std::function<void(std::string&)> m_record, m_mask, m_config;
    };

    explicit ProtocolFixture(const Mutation& change = {},
                             const MaskMutation& mask = {},
                             const GraphMutation& graph = {},
                             const ByteMutation& bytes = {},
                             const PathsFixture::CaptureMutation& native = {})
        : m_base({}, {}, {}, native), m_receipt(stage(change, mask, graph, bytes)),
          m_capture(checked(m_base.m_views.inspect(m_receipt, false))) { }

    AdmittedAssetReceipt stage(const Mutation& change,
                               const MaskMutation& mask,
                               const GraphMutation& graph,
                               const ByteMutation& bytes) {
        auto& region = m_base.m_views.m_object.m_region;
        const std::string K_RECIPE {
            "synthetic evaluation protocol/mask/config import; never inference" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        const auto K_HEADER = [&] {
            return Json { { "schema_version", 1 },
                         { "world_epoch", 3 },
                         { "pose_revision", 5 },
                         { "recipe_sha256", hash(K_RECIPE) },
                         { "capture", region_ref(m_base.m_capture.record().m_selection.m_product) },
                         { "manual_ancestor", false } };
        };
        for (std::size_t index { 0uz }; index < K_ARTIFACTS.size(); ++index) {
            std::string config { "synthetic configuration identity: " };
            config += K_ARTIFACTS[index];
            if (bytes.m_config) {
                bytes.m_config(config);
            }
            auto node = region.node(9600uz + index, config);
            node.m_recipe_sha256 = hash(K_RECIPE);
            m_selected.m_artifacts.emplace(static_cast<EvaluationArtifact>(index), node.m_product);
            region.m_graph.m_products.push_back(node);
        }
        for (std::size_t index { 0uz }; index < 3uz; ++index) {
            auto node = region.node(9700uz + index,
                                    "synthetic unseen evaluation light configuration " +
                                        std::to_string(index));
            node.m_recipe_sha256 = hash(K_RECIPE);
            region.m_graph.m_products.push_back(node);
            m_selected.m_lights.push_back(
                { "light_" + std::to_string(index), node.m_product, 100uz + index, index == 0uz });
        }
        std::uint64_t id { 9800 };
        for (const auto& frame : m_base.m_capture.evaluation()) {
            for (auto track : { EvaluationTrack::OBSERVED, EvaluationTrack::COMPLETED }) {
                EvaluationMaskPin pin { track, frame.m_inputs.m_view, {} };
                const auto K_PIXELS =
                    static_cast<std::uint64_t>(frame.m_inputs.m_calibration.m_width) *
                    frame.m_inputs.m_calibration.m_height;
                auto record = K_HEADER();
                record["view"] = object_view(pin.m_view);
                record["track"] = K_TRACKS[static_cast<std::size_t>(track)];
                record["width"] = frame.m_inputs.m_calibration.m_width;
                record["height"] = frame.m_inputs.m_calibration.m_height;
                record["runs"] = Json::array(
                    { { { "first", 0 }, { "count", 1 }, { "eligible", true }, { "surface", "OBSERVED" } },
                     { { "first", 1 },
                      { "count", 1 },
                      { "eligible", track == EvaluationTrack::COMPLETED },
                      { "surface", "COMPLETED" } },
                     { { "first", 2 },
                      { "count", K_PIXELS - 2 },
                      { "eligible", false },
                      { "surface", "UNKNOWN" } } });
                if (mask) {
                    mask(record, pin);
                }
                auto text = record.dump();
                if (bytes.m_mask) {
                    bytes.m_mask(text);
                }
                auto node = region.node(id++, text);
                node.m_recipe_sha256 = hash(K_RECIPE);
                node.m_dependencies = { m_base.m_capture.record().m_selection.m_product,
                                       pin.m_view.m_image,
                                       pin.m_view.m_pose_asset };
                pin.m_product = node.m_product;
                region.m_graph.m_products.push_back(node);
                m_selected.m_masks.push_back(pin);
            }
        }
        m_selected.m_geometry_samples = 1000;
        m_selected.m_geometry_seed = 123;
        m_selected.m_uv_resolution = 2048;
        m_selected.m_reviewers_min = 3;
        m_record = K_HEADER();
        m_record["sampling"] = { { "geometry_method", "AREA_WEIGHTED_SURFACE" },
                                { "geometry_samples", m_selected.m_geometry_samples },
                                { "geometry_seed", m_selected.m_geometry_seed },
                                { "uv_method", "TEXEL_CENTERS" },
                                { "uv_resolution", m_selected.m_uv_resolution } };
        m_record["metrics"] = { { "color_space", "DISPLAY_RGB_0_1" },
                               { "psnr_reduction", "PER_IMAGE_EQUAL_SCENE" },
                               { "ssim_window", 11 },
                               { "ssim_sigma", 1.5 },
                               { "ssim_k1", 0.01 },
                               { "ssim_k2", 0.03 },
                               { "data_range", 1 },
                               { "lpips_backbone", "ALEXNET" },
                               { "lpips_normalize", true },
                               { "fit_test_exposure", false },
                               { "fit_test_color", false } };
        m_record["render_columns"] = Json::array({ "CAPTURED_APPEARANCE", "PBR" });
        m_record["review"] = { { "reviewers_min", m_selected.m_reviewers_min },
                              { "blinded", true },
                              { "rating_min", 1 },
                              { "rating_max", 5 },
                              { "dimensions", Json::array({ "GEOMETRY", "MATERIAL", "SCENE" }) } };
        m_record["artifacts"] = Json::object();
        m_record["masks"] = Json::array();
        m_record["lights"] = Json::array();
        for (const auto& [K_KIND, K_REF] : m_selected.m_artifacts) {
            m_record["artifacts"][K_ARTIFACTS[static_cast<std::size_t>(K_KIND)]] =
                region_ref(K_REF);
        }
        for (const auto& pin : m_selected.m_masks) {
            m_record["masks"].push_back(protocol_mask_pin(pin));
        }
        for (const auto& pin : m_selected.m_lights) {
            m_record["lights"].push_back(protocol_light(pin));
        }
        if (change) {
            change(m_record, m_selected);
        }
        auto text = m_record.dump();
        if (bytes.m_record) {
            bytes.m_record(text);
        }
        auto node = region.node(9900, text);
        node.m_recipe_sha256 = hash(K_RECIPE);
        std::map<AssetProductId, AssetRevisionRef> parents {
            { m_base.m_capture.record().m_selection.m_product.m_product,
             m_base.m_capture.record().m_selection.m_product } };
        for (const auto& [K_KIND, K_REF] : m_selected.m_artifacts) {
            parents.emplace(K_REF.m_product, K_REF);
        }
        for (const auto& pin : m_selected.m_masks) {
            parents.emplace(pin.m_product.m_product, pin.m_product);
        }
        for (const auto& pin : m_selected.m_lights) {
            parents.emplace(pin.m_product.m_product, pin.m_product);
        }
        for (const auto& [K_ID, K_REF] : parents) {
            node.m_dependencies.push_back(K_REF);
        }
        m_selected.m_product = node.m_product;
        region.m_graph.m_products.push_back(node);
        if (graph) {
            m_base.m_views.m_change_graph = [&](AssetDependencyGraph& value) {
                graph(value, m_selected);
            };
        }
        // Restage only this fixture-owned synthetic tree.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto result = m_base.m_views.freeze(false);
        m_base.m_views.m_change_graph = {};
        return result;
    }

    Result<AdmittedEvaluationProtocol> inspect(const std::stop_token& stop = {}) const {
        return inspect_evaluation_protocol(
            m_selected, m_capture, m_receipt, m_receipt.graph().m_context, stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "protocol";
    }
};

}  // namespace world_explorer::evaluation_protocol_test
