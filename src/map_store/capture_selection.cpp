#include "world_explorer/map_store/capture_selection.hpp"

#include "../core/json_records.hpp"
#include "map_codec.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr std::size_t K_MIB { 1024uz * 1024uz };
using ViewKey = std::pair<CaptureId, FrameId>;

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

CaptureFrameRole role(const Json& value) {
    const auto K_TEXT = value.get<std::string>();
    if (K_TEXT == "INFERENCE") {
        return CaptureFrameRole::INFERENCE;
    }
    if (K_TEXT == "HELD_OUT") {
        return CaptureFrameRole::HELD_OUT;
    }
    require(
        K_TEXT == "EVALUATION_ONLY", "Unsupported capture role", ErrorCode::UNSUPPORTED_OPERATION);
    return CaptureFrameRole::EVALUATION_ONLY;
}

CaptureFrameSelection frame(const Json& value) {
    keys(value, { "view", "role", "annotations" });
    CaptureFrameSelection result { view(value.at("view")), role(value.at("role")), {} };
    array(value.at("annotations"), 16uz);
    for (const auto& item : value.at("annotations")) {
        result.m_annotations.push_back(reference(item));
    }
    std::ranges::sort(result.m_annotations);
    return result;
}

ViewKey key(const ObjectViewSelection& value) {
    return { value.m_capture, value.m_frame };
}

}  // namespace

struct AdmittedCaptureSelection::Implementation {
    CaptureSelectionRecord m_record;
    std::shared_ptr<const std::vector<CaptureFrameInputs>> m_inference;
    std::vector<CaptureEvaluationFrame> m_evaluation;
    std::string m_receipt_sha256;
    std::vector<std::byte> m_bytes;
};

InferenceCaptureInputs::InferenceCaptureInputs(
    std::shared_ptr<const std::vector<CaptureFrameInputs>> frames)
    : m_frames(std::move(frames)) { }

const std::vector<CaptureFrameInputs>& InferenceCaptureInputs::frames() const {
    return *m_frames;
}

AdmittedCaptureSelection::AdmittedCaptureSelection(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const CaptureSelectionRecord& AdmittedCaptureSelection::record() const {
    return m_state->m_record;
}

InferenceCaptureInputs AdmittedCaptureSelection::inference() const {
    return InferenceCaptureInputs(m_state->m_inference);
}

const std::vector<CaptureEvaluationFrame>& AdmittedCaptureSelection::evaluation() const {
    return m_state->m_evaluation;
}

const std::string& AdmittedCaptureSelection::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const std::vector<std::byte>& AdmittedCaptureSelection::encoded_bytes() const {
    return m_state->m_bytes;
}

Result<AdmittedCaptureSelection>
inspect_capture_selection(const AdmittedAssetReceipt& receipt,
                          const CaptureSelection& selection,
                          const std::vector<AdmittedObjectObservation>& annotations,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        require(selection.m_selection.m_value && selection.m_revision,
                "Missing capture-selection identity",
                ErrorCode::INVALID_ARGUMENT);
        require(!selection.m_frames.empty() && selection.m_frames.size() <= 64uz &&
                    !selection.m_sources.empty() && selection.m_sources.size() <= 32uz &&
                    annotations.size() <= 128uz,
                "Capture selection table budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::map<AssetProductId, std::reference_wrapper<const AssetDependencyNode>> nodes;
        for (const auto& node : receipt.graph().m_products) {
            nodes.emplace(node.m_product.m_product, node);
        }
        const auto K_BIND = [&](const AssetRevisionRef& ref) -> const AssetDependencyNode& {
            check_stop(stop);
            const auto K_FOUND = nodes.find(ref.m_product);
            require(K_FOUND != nodes.end() && K_FOUND->second.get().m_product == ref,
                    "Stale capture-selection product",
                    ErrorCode::RESYNC_REQUIRED);
            return K_FOUND->second.get();
        };
        const auto& target = K_BIND(selection.m_product);
        std::map<AssetProductId, AssetRevisionRef> parents;
        const auto K_PARENT = [&](const AssetRevisionRef& ref) {
            K_BIND(ref);
            require(ref.m_product != selection.m_product.m_product,
                    "Capture selection references itself");
            parents.emplace(ref.m_product, ref);
        };
        std::map<CaptureId, CaptureSourceSelection> sources;
        for (const auto& source : selection.m_sources) {
            digest_text(source.m_source_sha256);
            K_PARENT(source.m_rights);
            require(source.m_capture.m_value && sources.emplace(source.m_capture, source).second,
                    "Duplicate/invalid selected capture source",
                    ErrorCode::INVALID_ARGUMENT);
        }
        if (selection.m_measurement.has_value()) {
            K_PARENT(selection.m_measurement.value());
        }
        std::map<ViewKey, CaptureFrameSelection> frames;
        std::map<std::string, CaptureFrameRole> image_roles;
        std::set<std::string> forbidden;
        std::set<ViewKey> evaluation_views;
        std::set<CaptureId> used_sources;
        std::set<AssetProductId> annotation_products;
        std::set<AssetProductId> view_products;
        std::size_t annotation_count {}, inference_count {};
        for (const auto& selected : selection.m_frames) {
            const auto& selected_view = selected.m_view;
            require(selected_view.m_capture.m_value && selected_view.m_frame.m_value &&
                        selected_view.m_calibration.m_value &&
                        selected_view.m_frame_pose_revision.m_value &&
                        sources.contains(selected_view.m_capture),
                    "Missing capture frame/source identity",
                    ErrorCode::INVALID_ARGUMENT);
            require(selected.m_role == CaptureFrameRole::INFERENCE ||
                        selected.m_role == CaptureFrameRole::HELD_OUT ||
                        selected.m_role == CaptureFrameRole::EVALUATION_ONLY,
                    "Unsupported selected capture role",
                    ErrorCode::UNSUPPORTED_OPERATION);
            require(selected.m_annotations.size() <= 16uz &&
                        selected.m_annotations.size() <= 128uz - annotation_count,
                    "Capture annotation budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            annotation_count += selected.m_annotations.size();
            view_products.insert(selected_view.m_image.m_product);
            view_products.insert(selected_view.m_pose_asset.m_product);
            K_PARENT(selected_view.m_image);
            K_PARENT(selected_view.m_pose_asset);
            require(selected_view.m_image.m_product != selected_view.m_pose_asset.m_product,
                    "Aliased capture image/pose roles");
            const auto K_PRIOR = image_roles.find(selected_view.m_image.m_content_sha256);
            require(K_PRIOR == image_roles.end() || K_PRIOR->second == selected.m_role,
                    "Image bytes overlap capture roles");
            image_roles[selected_view.m_image.m_content_sha256] = selected.m_role;
            if (selected.m_role == CaptureFrameRole::INFERENCE) {
                ++inference_count;
                require(selected.m_annotations.empty(),
                        "Inference frames cannot carry evaluation annotations");
            } else {
                evaluation_views.insert(key(selected_view));
                forbidden.insert(selected_view.m_image.m_content_sha256);
                forbidden.insert(selected_view.m_pose_asset.m_content_sha256);
            }
            for (const auto& ref : selected.m_annotations) {
                K_PARENT(ref);
                require(annotation_products.insert(ref.m_product).second,
                        "Duplicate selected annotation");
            }
            auto owned = selected;
            std::ranges::sort(owned.m_annotations);
            require(frames.emplace(key(selected_view), std::move(owned)).second,
                    "Frame occurs more than once in capture selection");
            used_sources.insert(selected_view.m_capture);
        }
        require(inference_count && inference_count < frames.size(),
                "Capture selection requires inference and evaluation frames");
        require(used_sources.size() == sources.size(), "Unused capture source selection");
        require(annotation_count == annotations.size(),
                "Annotation handle selection incomplete",
                ErrorCode::RESYNC_REQUIRED);
        std::map<AssetProductId, std::reference_wrapper<const AdmittedObjectObservation>>
            selected_annotations;
        std::set<ObjectObservationId> annotation_ids;
        std::size_t spans {}, pixels {};
        for (const auto& annotation : annotations) {
            const auto& record = annotation.record();
            const auto& ref = record.m_selection.m_product;
            require(annotation.receipt_sha256() == receipt.receipt_sha256() &&
                        annotation_products.contains(ref.m_product) &&
                        record.m_context.m_world_epoch == expected.m_world_epoch &&
                        record.m_context.m_pose_revision == expected.m_pose_revision,
                    "Annotation uses a foreign source/context",
                    ErrorCode::RESYNC_REQUIRED);
            K_BIND(ref);
            require(selected_annotations.emplace(ref.m_product, annotation).second &&
                        annotation_ids.insert(record.m_selection.m_observation).second,
                    "Duplicate annotation product/semantic ID");
            require(annotation.mask().size() <= 32768uz - spans,
                    "Capture annotation span budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            spans += annotation.mask().size();
            for (auto count : annotation.coverage()) {
                require(count <= 64uz * K_MIB - pixels,
                        "Capture annotation pixel budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                pixels += static_cast<std::size_t>(count);
            }
            // Stop at selected source views: inference inputs may feed evaluation,
            // while label/mask/evidence bytes must never flow back into inference.
            std::set<AssetProductId> label_roots { ref.m_product,
                                                  record.m_selection.m_mask.m_product };
            for (const auto& item : record.m_selection.m_evidence) {
                label_roots.insert(item.m_product);
            }
            std::vector<AssetProductId> pending { ref.m_product };
            std::set<AssetProductId> visited;
            while (!pending.empty()) {
                check_stop(stop);
                const auto K_ID = pending.back();
                pending.pop_back();
                if (!visited.insert(K_ID).second) {
                    continue;
                }
                if (view_products.contains(K_ID) && !label_roots.contains(K_ID)) {
                    continue;
                }
                const auto& node = nodes.at(K_ID).get();
                forbidden.insert(node.m_product.m_content_sha256);
                forbidden.insert(node.m_recipe_sha256);
                if (node.m_manual_action_sha256.has_value()) {
                    forbidden.insert(node.m_manual_action_sha256.value());
                }
                for (const auto& parent : node.m_dependencies) {
                    pending.push_back(parent.m_product);
                }
            }
        }
        std::vector<AssetRevisionRef> expected_parents;
        expected_parents.reserve(parents.size());
        for (const auto& [K_ID, K_REF] : parents) {
            expected_parents.push_back(K_REF);
        }
        auto actual_parents = target.m_dependencies;
        std::ranges::sort(expected_parents);
        std::ranges::sort(actual_parents);
        require(expected_parents == actual_parents,
                "Capture selection dependency closure differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_BYTES =
            receipt.copy_blob(selection.m_product.m_content_sha256, 256uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_DOC = parse(*K_BYTES, 256uz * 1024uz, stop);
        keys(K_DOC,
             { "schema_version",
              "selection_id",
              "revision",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "sources",
              "frames",
              "measurement",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1,
                "Unsupported capture selection schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_DOC.at("selection_id")) == selection.m_selection.m_value &&
                    integer(K_DOC.at("revision")) == selection.m_revision &&
                    integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    digest(K_DOC.at("recipe_sha256")) == target.m_recipe_sha256,
                "Capture selection identity/context differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_MEASUREMENT = K_DOC.at("measurement").is_null()
                                       ? std::nullopt
                                       : std::optional { reference(K_DOC.at("measurement")) };
        require(K_MEASUREMENT == selection.m_measurement,
                "Capture measurement selection differs",
                ErrorCode::RESYNC_REQUIRED);
        require(K_DOC.at("manual_ancestor").get<bool>() == target.m_manual_ancestor,
                "Capture selection clears/invents manual ancestry");
        array(K_DOC.at("sources"), 32uz);
        auto remaining_sources = sources;
        for (const auto& row : K_DOC.at("sources")) {
            keys(row, { "capture_id", "source_sha256", "rights" });
            const CaptureSourceSelection K_SOURCE { { integer(row.at("capture_id")) },
                                                   digest(row.at("source_sha256")),
                                                   reference(row.at("rights")) };
            const auto K_FOUND = remaining_sources.find(K_SOURCE.m_capture);
            require(K_FOUND != remaining_sources.end() && K_FOUND->second == K_SOURCE,
                    "Capture source differs from independent selection",
                    ErrorCode::RESYNC_REQUIRED);
            remaining_sources.erase(K_SOURCE.m_capture);
        }
        require(remaining_sources.empty(), "Missing capture sources");
        array(K_DOC.at("frames"), 64uz);
        auto remaining_frames = frames;
        for (const auto& row : K_DOC.at("frames")) {
            const auto K_FRAME = frame(row);
            const auto K_FOUND = remaining_frames.find(key(K_FRAME.m_view));
            require(K_FOUND != remaining_frames.end() && K_FOUND->second == K_FRAME,
                    "Capture frame/role differs from independent selection",
                    ErrorCode::RESYNC_REQUIRED);
            remaining_frames.erase(key(K_FRAME.m_view));
        }
        require(remaining_frames.empty(), "Missing capture frames");
        // Follow all declared upstream bytes; hashes detect aliases with new IDs.
        std::set<AssetProductId> visited_inference;
        for (const auto& [K_KEY, K_FRAME] : frames) {
            if (K_FRAME.m_role != CaptureFrameRole::INFERENCE) {
                continue;
            }
            std::vector<AssetProductId> pending { K_FRAME.m_view.m_image.m_product,
                                                 K_FRAME.m_view.m_pose_asset.m_product };
            while (!pending.empty()) {
                check_stop(stop);
                const auto K_ID = pending.back();
                pending.pop_back();
                if (!visited_inference.insert(K_ID).second) {
                    continue;
                }
                const auto& node = nodes.at(K_ID).get();
                require(!forbidden.contains(node.m_product.m_content_sha256) &&
                            !forbidden.contains(node.m_recipe_sha256) &&
                            (!node.m_manual_action_sha256.has_value() ||
                             !forbidden.contains(node.m_manual_action_sha256.value())),
                        "Evaluation bytes enter inference dependency closure");
                for (const auto& parent : node.m_dependencies) {
                    pending.push_back(parent.m_product);
                }
            }
        }
        std::map<AssetProductId, CanonicalPoseAsset> decoded;
        std::size_t pose_bytes {};
        auto inference = std::make_shared<std::vector<CaptureFrameInputs>>();
        std::vector<CaptureEvaluationFrame> evaluation;
        std::size_t inference_bytes {}, evaluation_bytes {};
        for (const auto& [K_KEY, K_FRAME] : frames) {
            check_stop(stop);
            const auto& selected_view = K_FRAME.m_view;
            const auto K_POSE_ID = selected_view.m_pose_asset.m_product;
            if (!decoded.contains(K_POSE_ID)) {
                require(pose_bytes < 32uz * K_MIB,
                        "Capture pose byte budget exhausted",
                        ErrorCode::RESOURCE_EXHAUSTED);
                const auto K_POSE_BYTES =
                    receipt.copy_blob(selected_view.m_pose_asset.m_content_sha256,
                                      std::min(16uz * K_MIB, 32uz * K_MIB - pose_bytes),
                                      stop);
                if (!K_POSE_BYTES) {
                    return std::unexpected(K_POSE_BYTES.error());
                }
                pose_bytes += K_POSE_BYTES->size();
                auto asset = decode_pose_asset(*K_POSE_BYTES,
                                               selected_view.m_pose_asset.m_content_sha256,
                                               { { 16uz * K_MIB }, { 4096uz, 64uz, 64uz, 1024uz } },
                                               stop);
                if (!asset) {
                    return std::unexpected(asset.error());
                }
                decoded.emplace(K_POSE_ID, std::move(*asset));
            }
            const auto& asset = decoded.at(K_POSE_ID);
            require(static_cast<bool>(asset.m_snapshot), "Missing capture pose snapshot");
            const auto& map = *asset.m_snapshot;
            require(map.m_epoch == expected.m_world_epoch &&
                        map.m_revision == expected.m_pose_revision &&
                        asset.m_provenance.m_source_sha256 ==
                            sources.at(selected_view.m_capture).m_source_sha256 &&
                        !map.m_invalidated_frames.contains(selected_view.m_frame),
                    "Stale capture pose/source",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_OBSERVATION = map.m_observations.find(selected_view.m_frame);
            const auto K_CAMERA = map.m_calibrations.find(selected_view.m_calibration);
            const auto K_POSE = map.m_poses.find(selected_view.m_frame);
            require(K_OBSERVATION != map.m_observations.end() &&
                        K_CAMERA != map.m_calibrations.end() && K_POSE != map.m_poses.end(),
                    "Capture frame/calibration/pose absent",
                    ErrorCode::RESYNC_REQUIRED);
            const auto& observation = K_OBSERVATION->second;
            const auto& pose = K_POSE->second;
            require(observation.m_capture == selected_view.m_capture &&
                        observation.m_calibration == selected_view.m_calibration &&
                        observation.m_image_sha256 == selected_view.m_image.m_content_sha256 &&
                        pose.m_revision == selected_view.m_frame_pose_revision &&
                        pose.m_tracking == TrackingState::TRACKING,
                    "Capture view differs from decoded pose",
                    ErrorCode::RESYNC_REQUIRED);
            if (K_FRAME.m_role == CaptureFrameRole::INFERENCE) {
                for (const auto& [K_ID, K_OBS] : map.m_observations) {
                    check_stop(stop);
                    const auto K_SELECTED = frames.find({ K_OBS.m_capture, K_ID });
                    require(K_SELECTED != frames.end() &&
                                K_SELECTED->second.m_role == CaptureFrameRole::INFERENCE,
                            "Inference pose snapshot contains unselected/evaluation observations");
                    require(!evaluation_views.contains({ K_OBS.m_capture, K_ID }) &&
                                !forbidden.contains(K_OBS.m_image_sha256),
                            "Inference pose snapshot contains evaluation observations");
                }
            }
            auto& used_bytes =
                K_FRAME.m_role == CaptureFrameRole::INFERENCE ? inference_bytes : evaluation_bytes;
            require(used_bytes < 32uz * K_MIB,
                    "Capture image byte budget exhausted",
                    ErrorCode::RESOURCE_EXHAUSTED);
            auto image = receipt.copy_blob(selected_view.m_image.m_content_sha256,
                                           std::min(16uz * K_MIB, 32uz * K_MIB - used_bytes),
                                           stop);
            if (!image) {
                return std::unexpected(image.error());
            }
            used_bytes += image->size();
            CaptureFrameInputs inputs { selected_view,
                                       expected,
                                       observation,
                                       std::move(*image),
                                       K_CAMERA->second,
                                       pose,
                                       map_codec::compose(map.m_world_from_submap.at(pose.m_submap),
                                                          pose.m_submap_from_camera),
                                       K_BIND(selected_view.m_image).m_manual_ancestor ||
                                           K_BIND(selected_view.m_pose_asset).m_manual_ancestor,
                                       map.m_scale };
            if (K_FRAME.m_role == CaptureFrameRole::INFERENCE) {
                inference->push_back(std::move(inputs));
            } else {
                std::vector<AdmittedObjectObservation> owned_annotations;
                for (const auto& ref : K_FRAME.m_annotations) {
                    const auto K_FOUND = selected_annotations.find(ref.m_product);
                    require(K_FOUND != selected_annotations.end(),
                            "Missing selected annotation handle",
                            ErrorCode::RESYNC_REQUIRED);
                    const auto& annotation = K_FOUND->second.get();
                    require(annotation.record().m_selection.m_product == ref &&
                                annotation.record().m_selection.m_view == selected_view,
                            "Annotation belongs to another view/revision",
                            ErrorCode::RESYNC_REQUIRED);
                    owned_annotations.push_back(annotation);
                }
                evaluation.push_back(
                    { K_FRAME.m_role, std::move(inputs), std::move(owned_annotations) });
            }
        }
        CaptureSelection owned { selection.m_product,
                                selection.m_selection,
                                selection.m_revision,
                                {},
                                {},
                                selection.m_measurement };
        for (const auto& [K_ID, K_SOURCE] : sources) {
            owned.m_sources.push_back(K_SOURCE);
        }
        for (const auto& [K_KEY, K_FRAME] : frames) {
            owned.m_frames.push_back(K_FRAME);
        }
        check_stop(stop);
        return AdmittedCaptureSelection(
            std::make_shared<const AdmittedCaptureSelection::Implementation>(
                AdmittedCaptureSelection::Implementation {
                    { std::move(owned), expected, target.m_manual_ancestor },
                    std::move(inference),
                    std::move(evaluation),
                    receipt.receipt_sha256(),
                    *K_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Capture selection allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_capture_selection(const AdmittedCaptureSelection& selection,
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
    const auto& record = selection.record();
    if (selection.receipt_sha256() != receipt.receipt_sha256() ||
        record.m_context.m_world_epoch != expected.m_world_epoch ||
        record.m_context.m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Capture selection source/context differs");
    }
    try {
        const auto& selected = record.m_selection;
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { "capture-" + std::to_string(selected.m_selection.m_value) + "-" +
                  std::to_string(selected.m_revision),
              selection.encoded_bytes() } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Capture selection publication allocation failed");
    }
}

}  // namespace world_explorer
