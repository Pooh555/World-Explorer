#pragma once

#include "evaluation_paths_fixture.hpp"
#include "world_explorer/map_store/capture_envelope.hpp"

namespace world_explorer::capture_envelope_test {

using namespace evaluation_paths_test;

inline Json envelope_source(const CaptureSourceEnvelope& value) {
    constexpr std::array K_MOTION { "UNASSESSED", "STATIC", "DYNAMIC" };
    constexpr std::array K_EXPOSURE { "UNASSESSED", "LOCKED", "AUTOMATIC" };
    return { { "capture_id", value.m_source.m_capture.m_value },
            { "source_sha256", value.m_source.m_source_sha256 },
            { "rights", region_ref(value.m_source.m_rights) },
            { "source_frame_count",
             value.m_source_frame_count ? Json(*value.m_source_frame_count) : Json(nullptr) },
            { "condition", value.m_condition },
            { "scene_motion", K_MOTION[static_cast<std::size_t>(value.m_scene_motion)] },
            { "exposure_control", K_EXPOSURE[static_cast<std::size_t>(value.m_exposure_control)] } };
}

inline Json envelope_calibration(const CalibrationRecord& value) {
    return { { "calibration_id", value.m_id.m_value },
            { "width", value.m_width },
            { "height", value.m_height },
            { "model", value.m_model == CalibrationModel::PINHOLE ? "PINHOLE" : "OPENCV" },
            { "intrinsics",
             { value.m_camera.m_fx, value.m_camera.m_fy, value.m_camera.m_cx, value.m_camera.m_cy } },
            { "distortion", value.m_distortion },
            { "image_transform", value.m_image_transform } };
}

struct EnvelopeFixture {
    PathsFixture m_base;
    RecipeFixture m_output;
    CaptureEnvelopeSelection m_selected;
    Json m_record;
    AdmittedAssetReceipt m_receipt;
    AdmittedCaptureSelection m_capture;
    using Mutation = std::function<void(Json&, CaptureEnvelopeSelection&)>;
    using GraphMutation =
        std::function<void(AssetDependencyGraph&, const CaptureEnvelopeSelection&)>;
    using ByteMutation = std::function<void(std::string&)>;

    explicit EnvelopeFixture(const Mutation& change = {},
                             const GraphMutation& graph = {},
                             const ByteMutation& bytes = {},
                             const PathsFixture::CaptureMutation& native = {})
        : m_base({}, {}, {}, native), m_receipt(stage(change, graph, bytes)),
          m_capture(checked(m_base.m_views.inspect(m_receipt, false))) { }

    AdmittedAssetReceipt
    stage(const Mutation& change, const GraphMutation& graph, const ByteMutation& bytes) {
        auto& region = m_base.m_views.m_object.m_region;
        for (const auto& source : m_base.m_capture.record().m_selection.m_sources) {
            m_selected.m_sources.push_back({ source,
                                            10,
                                            "synthetic_static_room",
                                            CaptureSceneMotion::STATIC,
                                            CaptureExposureControl::LOCKED });
        }
        for (const auto& frame : m_base.m_capture.record().m_selection.m_frames) {
            m_selected.m_views.push_back({ frame.m_view, frame.m_role });
        }
        const std::string K_RECIPE {
            "synthetic capture-envelope declarations; not inference pixels" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        m_record = { { "schema_version", 1 },
                    { "world_epoch", 3 },
                    { "pose_revision", 5 },
                    { "recipe_sha256", hash(K_RECIPE) },
                    { "capture", region_ref(m_base.m_capture.record().m_selection.m_product) },
                    { "sources", Json::array() },
                    { "views", Json::array() },
                    { "manual_ancestor", false } };
        for (const auto& source : m_selected.m_sources) {
            m_record["sources"].push_back(envelope_source(source));
        }
        const auto K_ADD = [&](const CaptureFrameInputs& inputs, CaptureFrameRole role) {
            m_record["views"].push_back(
                { { "camera", path_camera(inputs) },
                 { "calibration", envelope_calibration(inputs.m_calibration) },
                 { "role", role_name(role) } });
        };
        const auto K_INFERENCE = m_base.m_capture.inference();
        for (const auto& frame : K_INFERENCE.frames()) {
            K_ADD(frame, CaptureFrameRole::INFERENCE);
        }
        for (const auto& frame : m_base.m_capture.evaluation()) {
            K_ADD(frame.m_inputs, frame.m_role);
        }
        if (change) {
            change(m_record, m_selected);
        }
        auto text = m_record.dump();
        if (bytes) {
            bytes(text);
        }
        auto node = region.node(9500, text);
        node.m_recipe_sha256 = hash(K_RECIPE);
        node.m_dependencies = { m_base.m_capture.record().m_selection.m_product };
        m_selected.m_product = node.m_product;
        region.m_graph.m_products.push_back(node);
        if (graph) {
            m_base.m_views.m_change_graph = [&](AssetDependencyGraph& value) {
                graph(value, m_selected);
            };
        }
        // Restage only the fixture's owned synthetic tree, preserving production inputs.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        auto result = m_base.m_views.freeze(false);
        m_base.m_views.m_change_graph = {};
        return result;
    }

    Result<AdmittedCaptureEnvelope> inspect(const std::stop_token& stop = {}) const {
        return inspect_capture_envelope(
            m_selected, m_capture, m_receipt, m_receipt.graph().m_context, stop);
    }

    std::filesystem::path directory() const {
        return m_output.m_root / "envelope";
    }
};

}  // namespace world_explorer::capture_envelope_test
