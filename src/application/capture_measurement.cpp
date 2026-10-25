#include "world_explorer/application/capture_measurement.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <span>
#include <string_view>

namespace world_explorer {
namespace {

using namespace json_record_detail;

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

ObjectViewSelection view(const Json& value) {
    keys(
        value,
        { "capture_id", "frame_id", "calibration_id", "frame_pose_revision", "image", "pose_asset" });
    return { { integer(value.at("capture_id")) },
            { integer(value.at("frame_id")) },
            { integer(value.at("calibration_id")) },
            { integer(value.at("frame_pose_revision")) },
            reference(value.at("image")),
            reference(value.at("pose_asset")) };
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    require(ref.m_product.m_value != 0 && ref.m_revision != 0, "Unselected measurement product");
    const auto K_FOUND = std::ranges::find(receipt.graph().m_products, ref, [](const auto& node) {
        return node.m_product;
    });
    require(K_FOUND != receipt.graph().m_products.end(),
            "Measurement product/revision differs",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

std::string measurement_key(const AssetRevisionRef& product) {
    return "measurement-" + std::to_string(product.m_product.m_value) + "-" +
           std::to_string(product.m_revision);
}

}  // namespace

struct AdmittedCaptureMeasurement::Implementation {
    CaptureMeasurementSelection m_selection;
    AssetRevisionRef m_capture_product;
    std::string m_receipt_sha256;
    AssetDependencyContext m_context;
    WorldFrameAlignment m_alignment;
    double m_metres {}, m_uncertainty {};
    bool m_manual_ancestor {};
    std::vector<std::byte> m_bytes;
};

AdmittedCaptureMeasurement::AdmittedCaptureMeasurement(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const CaptureMeasurementSelection& AdmittedCaptureMeasurement::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedCaptureMeasurement::capture_product() const {
    return m_state->m_capture_product;
}

const std::string& AdmittedCaptureMeasurement::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const AssetDependencyContext& AdmittedCaptureMeasurement::context() const {
    return m_state->m_context;
}

const WorldFrameAlignment& AdmittedCaptureMeasurement::alignment() const {
    return m_state->m_alignment;
}

double AdmittedCaptureMeasurement::metres() const {
    return m_state->m_metres;
}

double AdmittedCaptureMeasurement::uncertainty_metres() const {
    return m_state->m_uncertainty;
}

bool AdmittedCaptureMeasurement::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

Result<AdmittedCaptureMeasurement>
inspect_capture_measurement(const CaptureMeasurementSelection& selected,
                            const AdmittedCaptureSelection& capture,
                            const AdmittedAssetReceipt& receipt,
                            const SceneState& source,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        const auto& capture_record = capture.record();
        const auto& selection = capture_record.m_selection;
        require(capture.receipt_sha256() == receipt.receipt_sha256() &&
                    capture_record.m_context.m_world_epoch == expected.m_world_epoch &&
                    capture_record.m_context.m_pose_revision == expected.m_pose_revision &&
                    selection.m_measurement && *selection.m_measurement == selected.m_product,
                "Measurement differs from selected capture/context",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_INPUTS = capture.inference();
        const auto K_VIEW =
            std::ranges::find(K_INPUTS.frames(), selected.m_view, &CaptureFrameInputs::m_view);
        require(K_VIEW != K_INPUTS.frames().end(),
                "Measurement view is not selected for inference",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FIREWALL =
            capture.validate_inference_dependency(receipt, selected.m_product, expected, stop);
        if (!K_FIREWALL) {
            return std::unexpected(K_FIREWALL.error());
        }
        const auto& node = bind(selected.m_product, receipt);
        const auto& alignment_node = bind(selected.m_alignment, receipt);
        auto parents = node.m_dependencies;
        auto wanted = std::vector {
            selected.m_view.m_image, selected.m_view.m_pose_asset, selected.m_alignment };
        std::ranges::sort(parents);
        std::ranges::sort(wanted);
        require(parents == wanted && alignment_node.m_dependencies.empty(),
                "Measurement dependency closure differs");
        const auto K_BYTES =
            receipt.copy_blob(selected.m_product.m_content_sha256, 16uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_DOC = parse(*K_BYTES, 16uz * 1024uz, stop);
        keys(K_DOC,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "source_sha256",
              "view",
              "scene",
              "alignment",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1,
                "Unsupported measurement schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    digest(K_DOC.at("recipe_sha256")) == node.m_recipe_sha256 &&
                    view(K_DOC.at("view")) == selected.m_view &&
                    reference(K_DOC.at("alignment")) == selected.m_alignment,
                "Measurement context/view/recipe differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_SOURCE = std::ranges::find(
            selection.m_sources, selected.m_view.m_capture, &CaptureSourceSelection::m_capture);
        require(K_SOURCE != selection.m_sources.end() &&
                    digest(K_DOC.at("source_sha256")) == K_SOURCE->m_source_sha256,
                "Measurement full capture source differs",
                ErrorCode::RESYNC_REQUIRED);
        require(K_DOC.at("manual_ancestor").is_boolean() &&
                    K_DOC.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
                "Measurement manual ancestry differs");
        const auto& scene = K_DOC.at("scene");
        keys(scene, { "scene_id", "world_epoch", "revision", "sha256" });
        const CaptureMeasurementScene K_SCENE { { integer(scene.at("scene_id")) },
                                               { integer(scene.at("world_epoch")) },
                                               { integer(scene.at("revision")) },
                                               digest(scene.at("sha256")) };
        require(K_SCENE == selected.m_scene && K_SCENE.m_scene == source.m_id &&
                    K_SCENE.m_epoch == source.m_epoch && K_SCENE.m_revision == source.m_revision &&
                    source.m_epoch == expected.m_world_epoch &&
                    source.m_scale == ScaleStatus::ARBITRARY && !source.m_world_frame &&
                    source.m_id.m_value != 0 && source.m_revision.m_value != 0,
                "Measurement source scene identity/context differs",
                ErrorCode::RESYNC_REQUIRED);
        const SceneLimits K_LIMITS {
            64uz, 0uz, 32uz * 1024uz * 1024uz, 4096uz, 262144uz, 524288uz, 0uz };
        // Native scene validation predates this bounded API and copies some
        // observed metadata while checking it. Bound those strings first.
        require(source.m_chunks.size() <= K_LIMITS.m_max_chunks,
                "Measurement chunk budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        for (const auto& [K_ID, K_CHUNK] : source.m_chunks) {
            check_stop(stop);
            require(K_CHUNK && K_CHUNK->m_mesh && K_CHUNK->m_splats.empty() &&
                        !K_CHUNK->m_mesh->m_baked,
                    "Measurement requires untextured mesh-only chunks");
            if (K_CHUNK->m_mesh->m_observed) {
                const auto& observed = *K_CHUNK->m_mesh->m_observed;
                const std::array<std::string_view, 6uz> K_PINS {
                    observed.m_context.m_manifest_sha256,
                    observed.m_context.m_receipt_sha256,
                    observed.m_context.m_recipe_sha256,
                    observed.m_context.m_pose_sha256,
                    observed.m_asset_sha256,
                    observed.m_cleanup_sha256 };
                for (auto pin : K_PINS) {
                    require(pin.size() <= 64uz,
                            "Measurement geometry pin budget exceeded",
                            ErrorCode::RESOURCE_EXHAUSTED);
                }
                require(observed.m_manifest_json.size() <= 4uz * 1024uz * 1024uz &&
                            observed.m_receipt_json.size() <= 4uz * 1024uz * 1024uz &&
                            observed.m_cleanup_json.size() <= 4uz * 1024uz * 1024uz,
                        "Measurement geometry metadata budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
            }
        }
        const auto K_VALID = validate_scene(source, K_LIMITS);
        if (!K_VALID) {
            return std::unexpected(K_VALID.error());
        }
        require(scene_content_hash(source) == K_SCENE.m_sha256,
                "Measurement scene content differs",
                ErrorCode::RESYNC_REQUIRED);
        std::set<FrameId> frame_ids;
        for (const auto& input : K_INPUTS.frames()) {
            if (input.m_view.m_capture == selected.m_view.m_capture &&
                input.m_view.m_pose_asset == selected.m_view.m_pose_asset) {
                frame_ids.insert(input.m_view.m_frame);
            }
        }
        require(!source.m_chunks.empty(), "Measurement scene has no anchor geometry");
        for (const auto& [K_ID, K_CHUNK] : source.m_chunks) {
            check_stop(stop);
            require(K_CHUNK->m_geometry_valid && K_CHUNK->m_mesh && !K_CHUNK->m_mesh->m_baked &&
                        K_CHUNK->m_pose_revision.m_value == expected.m_pose_revision.m_value &&
                        !K_CHUNK->m_source_frames.empty(),
                    "Measurement requires current untextured pose-bound meshes",
                    ErrorCode::RESYNC_REQUIRED);
            for (auto frame : K_CHUNK->m_source_frames) {
                require(frame_ids.contains(frame),
                        "Measurement geometry uses unselected/evaluation frames");
            }
            if (K_CHUNK->m_mesh->m_observed) {
                const auto& context = K_CHUNK->m_mesh->m_observed->m_context;
                require(context.m_pose_sha256 == selected.m_view.m_pose_asset.m_content_sha256 &&
                            context.m_epoch == expected.m_world_epoch &&
                            context.m_pose_revision == expected.m_pose_revision,
                        "Measurement geometry pose source differs",
                        ErrorCode::RESYNC_REQUIRED);
            }
        }
        const auto K_ALIGNMENT_BYTES = receipt.copy_blob(
            selected.m_alignment.m_content_sha256, K_WORLD_FRAME_DOCUMENT_BYTES, stop);
        if (!K_ALIGNMENT_BYTES) {
            return std::unexpected(K_ALIGNMENT_BYTES.error());
        }
        // Use the strict metadata parser before the established world-frame reader.
        const auto K_ALIGNMENT_DOC = parse(*K_ALIGNMENT_BYTES, K_WORLD_FRAME_DOCUMENT_BYTES, stop);
        const std::string K_TEXT { reinterpret_cast<const char*>(K_ALIGNMENT_BYTES->data()),
                                  K_ALIGNMENT_BYTES->size() };
        auto alignment = admit_world_frame(source, K_TEXT, selected.m_alignment.m_content_sha256);
        if (!alignment) {
            return std::unexpected(alignment.error());
        }
        check_stop(stop);
        return AdmittedCaptureMeasurement(
            std::make_shared<const AdmittedCaptureMeasurement::Implementation>(
                AdmittedCaptureMeasurement::Implementation {
                    selected,
                    selection.m_product,
                    receipt.receipt_sha256(),
                    expected,
                    std::move(*alignment),
                    number(K_ALIGNMENT_DOC.at("distance").at("metres")),
                    number(K_ALIGNMENT_DOC.at("distance").at("uncertainty_metres")),
                    node.m_manual_ancestor,
                    *K_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Measurement allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_capture_measurement(const AdmittedCaptureMeasurement& measurement,
                                                 const AdmittedAssetReceipt& receipt,
                                                 const std::filesystem::path& directory,
                                                 OperationId operation,
                                                 std::uint64_t base_generation,
                                                 const AssetDependencyContext& expected,
                                                 const std::stop_token& stop,
                                                 const std::function<void(std::size_t)>& observed) {
    const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
    if (!K_GRAPH) {
        return std::unexpected(K_GRAPH.error());
    }
    if (measurement.receipt_sha256() != receipt.receipt_sha256() ||
        measurement.context().m_world_epoch != expected.m_world_epoch ||
        measurement.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Measurement publication source/context differs");
    }
    try {
        const auto& product = measurement.selection().m_product;
        const auto K_KEY = measurement_key(product);
        const auto K_TEXT = std::as_bytes(std::span(measurement.alignment().m_document_json));
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         { { K_KEY, measurement.m_state->m_bytes },
                                          { K_KEY + "-frame", { K_TEXT.begin(), K_TEXT.end() } } },
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Measurement publication allocation failed");
    }
}

Result<AdmittedCaptureMeasurement>
read_capture_measurement(const std::filesystem::path& directory,
                         const RecordCommit& commit,
                         const CaptureMeasurementSelection& selected,
                         const AdmittedCaptureSelection& capture,
                         const AdmittedAssetReceipt& receipt,
                         const SceneState& source,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop) {
    try {
        digest_text(commit.m_manifest_sha256);
        const auto K_SNAPSHOT = read_immutable_records(directory, stop);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        require(commit.m_generation != 0 &&
                    K_SNAPSHOT->m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT->m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Measurement registry differs from independent commit pin",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_KEY = measurement_key(selected.m_product);
        const auto K_RECORD = K_SNAPSHOT->m_records.find(K_KEY);
        const auto K_FRAME = K_SNAPSHOT->m_records.find(K_KEY + "-frame");
        require(K_RECORD != K_SNAPSHOT->m_records.end() && K_FRAME != K_SNAPSHOT->m_records.end() &&
                    sha256(K_RECORD->second) == selected.m_product.m_content_sha256 &&
                    sha256(K_FRAME->second) == selected.m_alignment.m_content_sha256,
                "Measurement metadata is not committed with selected bytes",
                ErrorCode::RESYNC_REQUIRED);
        return inspect_capture_measurement(selected, capture, receipt, source, expected, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Measurement read allocation failed");
    }
}

}  // namespace world_explorer
