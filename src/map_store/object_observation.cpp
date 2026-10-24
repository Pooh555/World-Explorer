#include "world_explorer/map_store/object_observation.hpp"

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

ObjectImageGrid grid(const Json& value) {
    keys(value, { "width", "height", "image_to_mask" });
    const auto& transform = value.at("image_to_mask");
    array(transform, 4uz);
    require(transform.size() == 4uz, "Invalid object image-grid transform");
    return { integer(value.at("width")),
            integer(value.at("height")),
            { number(transform[0uz]),
             number(transform[1uz]),
             number(transform[2uz]),
             number(transform[3uz]) } };
}

void context(const Json& value, const AssetDependencyContext& expected) {
    require(integer(value.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(value.at("pose_revision")) == expected.m_pose_revision.m_value,
            "Stale object observation context",
            ErrorCode::RESYNC_REQUIRED);
}

std::optional<double> confidence(const Json& value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    const auto K_VALUE = number(value);
    require(K_VALUE >= 0 && K_VALUE <= 1, "Invalid object confidence");
    return K_VALUE;
}

std::string label(const Json& value, const std::string& alphabet) {
    auto text = value.get<std::string>();
    require(!text.empty() && text.size() <= 64uz && text.find_first_not_of(alphabet) == text.npos,
            "Invalid object label/reason token");
    return text;
}

template <class Enum> Enum named(const Json& value, const std::map<std::string, Enum>& names) {
    const auto K_FOUND = names.find(value.get<std::string>());
    require(K_FOUND != names.end(),
            "Unsupported object observation enum",
            ErrorCode::UNSUPPORTED_OPERATION);
    return K_FOUND->second;
}

std::uint64_t grid_pixels(const ObjectImageGrid& value) {
    require(value.m_width && value.m_width <= 32768 && value.m_height && value.m_height <= 32768,
            "Invalid mask dimensions");
    require(value.m_width <= 16uz * K_MIB / value.m_height,
            "Object mask pixel budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    for (auto coefficient : value.m_image_to_mask) {
        require(std::isfinite(coefficient), "Nonfinite mask grid");
    }
    require(value.m_image_to_mask[0uz] > 0 && value.m_image_to_mask[1uz] > 0,
            "Mask grid must have positive scales");
    return value.m_width * value.m_height;
}

void contained_grid(const ObjectImageGrid& value, const CalibrationRecord& camera) {
    for (std::size_t axis { 0uz }; axis < 2uz; ++axis) {
        const auto K_SCALE = value.m_image_to_mask[axis];
        const auto K_OFFSET = value.m_image_to_mask[axis + 2uz];
        const auto K_EXTENT = static_cast<double>(axis ? value.m_height : value.m_width);
        const auto K_IMAGE = static_cast<double>(axis ? camera.m_height : camera.m_width);
        const auto K_FIRST = (-0.5 - K_OFFSET) / K_SCALE;
        const auto K_LAST = (K_EXTENT - 0.5 - K_OFFSET) / K_SCALE;
        require(std::isfinite(K_FIRST) && std::isfinite(K_LAST) && K_FIRST >= -0.5 &&
                    K_LAST <= K_IMAGE - 0.5,
                "Mask grid escapes calibrated image support");
    }
}

}  // namespace

struct AdmittedObjectObservation::Implementation {
    ObjectObservationRecord m_record;
    std::vector<ObjectMaskSpan> m_mask;
    std::array<std::uint64_t, 4> m_coverage {};
    CalibrationRecord m_calibration;
    PoseEstimate m_pose;
    SimilarityTransform m_world_from_camera;
    std::string m_receipt_sha256;
};

AdmittedObjectObservation::AdmittedObjectObservation(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const ObjectObservationRecord& AdmittedObjectObservation::record() const {
    return m_state->m_record;
}

const std::vector<ObjectMaskSpan>& AdmittedObjectObservation::mask() const {
    return m_state->m_mask;
}

const std::array<std::uint64_t, 4>& AdmittedObjectObservation::coverage() const {
    return m_state->m_coverage;
}

const CalibrationRecord& AdmittedObjectObservation::calibration() const {
    return m_state->m_calibration;
}

const PoseEstimate& AdmittedObjectObservation::pose() const {
    return m_state->m_pose;
}

const SimilarityTransform& AdmittedObjectObservation::world_from_camera() const {
    return m_state->m_world_from_camera;
}

const std::string& AdmittedObjectObservation::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

Result<AdmittedObjectObservation>
inspect_object_observation(const AdmittedAssetReceipt& receipt,
                           const ObjectObservationSelection& selection,
                           const AssetDependencyContext& expected,
                           const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        require(selection.m_observation.m_value && selection.m_revision &&
                    selection.m_view.m_capture.m_value && selection.m_view.m_frame.m_value &&
                    selection.m_view.m_calibration.m_value &&
                    selection.m_view.m_frame_pose_revision.m_value,
                "Missing selected object identity",
                ErrorCode::INVALID_ARGUMENT);
        require(selection.m_evidence.size() <= 48uz,
                "Object evidence budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        const auto K_PIXELS = grid_pixels(selection.m_grid);
        std::map<AssetProductId, std::reference_wrapper<const AssetDependencyNode>> nodes;
        for (const auto& node : receipt.graph().m_products) {
            nodes.emplace(node.m_product.m_product, node);
        }
        const auto K_BIND = [&](const AssetRevisionRef& ref) -> const AssetDependencyNode& {
            check_stop(stop);
            const auto K_FOUND = nodes.find(ref.m_product);
            require(K_FOUND != nodes.end() && K_FOUND->second.get().m_product == ref,
                    "Stale object dependency selection",
                    ErrorCode::RESYNC_REQUIRED);
            return K_FOUND->second.get();
        };
        const auto& target = K_BIND(selection.m_product);
        const auto& mask_node = K_BIND(selection.m_mask);
        K_BIND(selection.m_view.m_image);
        K_BIND(selection.m_view.m_pose_asset);
        std::set<AssetProductId> roles { selection.m_product.m_product,
                                        selection.m_mask.m_product,
                                        selection.m_view.m_image.m_product,
                                        selection.m_view.m_pose_asset.m_product };
        require(roles.size() == 4uz, "Aliased object dependency roles");
        std::map<AssetProductId, AssetRevisionRef> evidence;
        for (const auto& ref : selection.m_evidence) {
            K_BIND(ref);
            require(roles.insert(ref.m_product).second &&
                        evidence.emplace(ref.m_product, ref).second,
                    "Duplicate/aliased selected object evidence",
                    ErrorCode::INVALID_ARGUMENT);
        }
        auto expected_parents = selection.m_evidence;
        expected_parents.insert(
            expected_parents.end(),
            { selection.m_view.m_image, selection.m_view.m_pose_asset, selection.m_mask });
        const auto K_SAME_PARENTS = [&](const AssetDependencyNode& node,
                                        std::vector<AssetRevisionRef> parents) {
            auto actual = node.m_dependencies;
            std::ranges::sort(actual);
            std::ranges::sort(parents);
            require(actual == parents,
                    "Object dependency closure differs from selection",
                    ErrorCode::RESYNC_REQUIRED);
        };
        K_SAME_PARENTS(target, expected_parents);
        K_SAME_PARENTS(mask_node, { selection.m_view.m_image, selection.m_view.m_pose_asset });
        const auto K_POSE_BYTES =
            receipt.copy_blob(selection.m_view.m_pose_asset.m_content_sha256, 16uz * K_MIB, stop);
        if (!K_POSE_BYTES) {
            return std::unexpected(K_POSE_BYTES.error());
        }
        const auto K_POSES = decode_pose_asset(*K_POSE_BYTES,
                                               selection.m_view.m_pose_asset.m_content_sha256,
                                               { { 16uz * K_MIB }, { 4096uz, 64uz, 64uz, 1024uz } },
                                               stop);
        if (!K_POSES) {
            return std::unexpected(K_POSES.error());
        }
        require(static_cast<bool>(K_POSES->m_snapshot), "Missing decoded pose snapshot");
        const auto& map = *K_POSES->m_snapshot;
        require(map.m_epoch == expected.m_world_epoch &&
                    map.m_revision == expected.m_pose_revision &&
                    !map.m_invalidated_frames.contains(selection.m_view.m_frame),
                "Stale/invalidated object view",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FRAME = map.m_observations.find(selection.m_view.m_frame);
        const auto K_CAMERA = map.m_calibrations.find(selection.m_view.m_calibration);
        const auto K_POSE = map.m_poses.find(selection.m_view.m_frame);
        require(K_FRAME != map.m_observations.end() && K_CAMERA != map.m_calibrations.end() &&
                    K_POSE != map.m_poses.end(),
                "Object frame/calibration/pose absent",
                ErrorCode::RESYNC_REQUIRED);
        const auto& frame = K_FRAME->second;
        const auto& pose = K_POSE->second;
        require(frame.m_capture == selection.m_view.m_capture &&
                    frame.m_calibration == selection.m_view.m_calibration &&
                    frame.m_image_sha256 == selection.m_view.m_image.m_content_sha256 &&
                    pose.m_revision == selection.m_view.m_frame_pose_revision &&
                    pose.m_tracking == TrackingState::TRACKING,
                "Object view differs from decoded map",
                ErrorCode::RESYNC_REQUIRED);
        contained_grid(selection.m_grid, K_CAMERA->second);
        const auto K_RECORD_BYTES =
            receipt.copy_blob(selection.m_product.m_content_sha256, 256uz * 1024uz, stop);
        if (!K_RECORD_BYTES) {
            return std::unexpected(K_RECORD_BYTES.error());
        }
        const auto K_DOC = parse(*K_RECORD_BYTES, 256uz * 1024uz, stop);
        keys(K_DOC,
             { "schema_version",
              "observation_id",
              "revision",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "view",
              "mask",
              "grid",
              "classes",
              "associations",
              "evidence",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1,
                "Unsupported object observation schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        context(K_DOC, expected);
        require(integer(K_DOC.at("observation_id")) == selection.m_observation.m_value &&
                    integer(K_DOC.at("revision")) == selection.m_revision &&
                    view(K_DOC.at("view")) == selection.m_view &&
                    reference(K_DOC.at("mask")) == selection.m_mask &&
                    grid(K_DOC.at("grid")) == selection.m_grid &&
                    digest(K_DOC.at("recipe_sha256")) == target.m_recipe_sha256,
                "Object record differs from independent selection",
                ErrorCode::RESYNC_REQUIRED);
        ObjectObservationRecord record { selection,
                                        expected,
                                        target.m_recipe_sha256,
                                        {},
                                        {},
                                        {},
                                        K_DOC.at("manual_ancestor").get<bool>() };
        require(record.m_manual_ancestor == target.m_manual_ancestor,
                "Object record clears/invents declared manual ancestry");
        const auto& classes = K_DOC.at("classes");
        array(classes, 16uz);
        std::set<std::string> labels;
        for (const auto& row : classes) {
            keys(row, { "label", "confidence" });
            auto name = label(row.at("label"),
                              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-");
            require(labels.insert(name).second, "Duplicate object class hypothesis");
            record.m_classes.push_back({ std::move(name), confidence(row.at("confidence")) });
        }
        const auto& associations = K_DOC.at("associations");
        array(associations, 16uz);
        std::set<ObjectTrackId> tracks;
        std::size_t accepted {};
        for (const auto& row : associations) {
            keys(row, { "track_id", "decision", "confidence" });
            ObjectTrackAssociation item { { integer(row.at("track_id")) },
                                         named<ObjectAssociationDecision>(
                                             row.at("decision"),
                                             { { "HYPOTHESIS", ObjectAssociationDecision::HYPOTHESIS },
                                              { "ACCEPTED", ObjectAssociationDecision::ACCEPTED },
                                              { "REJECTED", ObjectAssociationDecision::REJECTED } }),
                                         confidence(row.at("confidence")) };
            require(item.m_track.m_value && tracks.insert(item.m_track).second,
                    "Invalid/duplicate object track");
            if (item.m_decision == ObjectAssociationDecision::ACCEPTED) {
                ++accepted;
            }
            require(accepted <= 1uz, "Object observation accepts multiple tracks");
            record.m_associations.push_back(item);
        }
        const auto& decisions = K_DOC.at("evidence");
        array(decisions, 48uz);
        require(decisions.size() == evidence.size(),
                "Object evidence selection incomplete",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& row : decisions) {
            keys(row, { "product", "accepted", "reason" });
            const auto K_REF = reference(row.at("product"));
            const auto K_FOUND = evidence.find(K_REF.m_product);
            require(K_FOUND != evidence.end() && K_FOUND->second == K_REF,
                    "Foreign/duplicate object evidence",
                    ErrorCode::RESYNC_REQUIRED);
            evidence.erase(K_REF.m_product);
            record.m_evidence.push_back(
                { K_REF,
                 row.at("accepted").get<bool>(),
                 label(row.at("reason"), "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") });
        }
        const auto K_MASK_BYTES =
            receipt.copy_blob(selection.m_mask.m_content_sha256, 4uz * K_MIB, stop);
        if (!K_MASK_BYTES) {
            return std::unexpected(K_MASK_BYTES.error());
        }
        const auto K_MASK = parse(*K_MASK_BYTES, 4uz * K_MIB, stop);
        keys(K_MASK,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "view",
              "grid",
              "runs",
              "manual_ancestor" });
        require(integer(K_MASK.at("schema_version")) == 1,
                "Unsupported object mask schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        context(K_MASK, expected);
        require(view(K_MASK.at("view")) == selection.m_view &&
                    grid(K_MASK.at("grid")) == selection.m_grid,
                "Object mask view/grid differs from selection",
                ErrorCode::RESYNC_REQUIRED);
        require(K_MASK.at("manual_ancestor").get<bool>() == mask_node.m_manual_ancestor,
                "Object mask clears/invents declared manual ancestry");
        const auto& runs = K_MASK.at("runs");
        array(runs, 16384uz);
        std::vector<ObjectMaskSpan> spans;
        std::array<std::uint64_t, 4> coverage {};
        std::uint64_t next {};
        for (const auto& row : runs) {
            check_stop(stop);
            keys(row, { "first", "count", "class" });
            ObjectMaskSpan span {
                integer(row.at("first")),
                integer(row.at("count")),
                named<ObjectMaskClass>(row.at("class"),
                                       { { "VISIBLE", ObjectMaskClass::VISIBLE },
                                        { "OCCLUDED", ObjectMaskClass::OCCLUDED },
                                        { "UNKNOWN", ObjectMaskClass::UNKNOWN },
                                        { "BACKGROUND", ObjectMaskClass::BACKGROUND } }) };
            require(span.m_first == next && span.m_count > 0 && span.m_count <= K_PIXELS - next,
                    "Incomplete/overlapping object mask partition");
            next += span.m_count;
            coverage[static_cast<std::size_t>(span.m_class)] += span.m_count;
            spans.push_back(span);
        }
        require(next == K_PIXELS, "Incomplete object mask coverage");
        const auto K_WORLD = map_codec::compose(map.m_world_from_submap.at(pose.m_submap),
                                                pose.m_submap_from_camera);
        require(valid(K_WORLD), "Object view world transform overflows");
        check_stop(stop);
        return AdmittedObjectObservation(
            std::make_shared<const AdmittedObjectObservation::Implementation>(
                AdmittedObjectObservation::Implementation { std::move(record),
                                                           std::move(spans),
                                                           coverage,
                                                           K_CAMERA->second,
                                                           pose,
                                                           K_WORLD,
                                                           receipt.receipt_sha256() }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Object observation allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
