#include "world_explorer/application/corpus_inspection.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr auto K_REQUEST_BYTES = 2uz * 1024uz * 1024uz;
constexpr auto K_RECEIPT_BYTES = 256uz * 1024uz * 1024uz;

struct InspectionBudget {
    std::size_t m_receipt_bytes {}, m_frames {};
    std::size_t m_receipt_limit { K_RECEIPT_BYTES };
    std::uint32_t m_request_version { 1 };
};

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return *result;
}

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    const auto K_FOUND = std::ranges::find(names, K_TEXT);
    require(K_FOUND != names.end(), "Unsupported corpus-inspection selection enum");
    return static_cast<Enum>(std::distance(names.begin(), K_FOUND));
}

std::string text(const Json& value, std::size_t maximum) {
    auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= maximum && result.find('\0') == result.npos,
            "Invalid corpus-inspection selection text");
    return result;
}

std::filesystem::path path(const Json& value, const std::filesystem::path& base) {
    return base / std::filesystem::path(text(value, 4096uz));
}

bool boolean(const Json& value) {
    require(value.is_boolean(), "Expected corpus-inspection selection boolean");
    return value.get<bool>();
}

std::uint32_t integer32(const Json& value) {
    const auto K_VALUE = integer(value);
    require(K_VALUE <= std::numeric_limits<std::uint32_t>::max(), "Selection integer overflow");
    return static_cast<std::uint32_t>(K_VALUE);
}

std::uint32_t schema_version(const Json& value) {
    const auto K_VERSION = integer32(value);
    require(K_VERSION == 1 || K_VERSION == 2,
            "Unsupported inspection selection schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    return K_VERSION;
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

std::optional<AssetRevisionRef> optional_ref(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional { reference(value) };
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

AssetDependencyContext context(const Json& value) {
    keys(value, { "world_epoch", "pose_revision" });
    return { { integer(value.at("world_epoch")) }, { integer(value.at("pose_revision")) } };
}

RecordCommit commit(const Json& value) {
    keys(value, { "generation", "manifest_sha256" });
    const RecordCommit K_COMMIT { integer(value.at("generation")),
                                 digest(value.at("manifest_sha256")) };
    require(K_COMMIT.m_generation != 0, "Unselected corpus-inspection registry generation");
    return K_COMMIT;
}

CaptureFrameRole role(const Json& value) {
    return named<CaptureFrameRole>(value, std::array { "INFERENCE", "HELD_OUT", "EVALUATION_ONLY" });
}

CaptureSourceSelection source(const Json& value) {
    keys(value, { "capture_id", "source_sha256", "rights" });
    return { { integer(value.at("capture_id")) },
            digest(value.at("source_sha256")),
            reference(value.at("rights")) };
}

CaptureSelection capture(const Json& value, InspectionBudget& budget) {
    if (budget.m_request_version == 1) {
        keys(value, { "product", "selection_id", "revision", "sources", "frames", "measurement" });
    } else {
        keys(value,
             { "schema_version",
              "product",
              "selection_id",
              "revision",
              "sources",
              "frames",
              "measurement" });
    }
    CaptureSelection selected {
        reference(value.at("product")),
        { integer(value.at("selection_id")) },
        integer(value.at("revision")),
        {},
        {},
        optional_ref(value.at("measurement")),
        budget.m_request_version == 1 ? 1 : schema_version(value.at("schema_version")) };
    array(value.at("sources"), 32uz);
    array(value.at("frames"), 64uz);
    require(value.at("frames").size() <= 256uz - budget.m_frames,
            "Inspection frame budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    budget.m_frames += value.at("frames").size();
    for (const auto& row : value.at("sources")) {
        selected.m_sources.push_back(source(row));
    }
    std::size_t annotations {};
    for (const auto& row : value.at("frames")) {
        keys(row, { "view", "role", "annotations" });
        array(row.at("annotations"), 16uz);
        require(row.at("annotations").size() <= 128uz - annotations,
                "Capture annotation budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        annotations += row.at("annotations").size();
        CaptureFrameSelection frame { view(row.at("view")), role(row.at("role")), {} };
        for (const auto& pin : row.at("annotations")) {
            frame.m_annotations.push_back(reference(pin));
        }
        selected.m_frames.push_back(std::move(frame));
    }
    return selected;
}

ObjectObservationSelection annotation(const Json& value) {
    keys(value, { "product", "observation_id", "revision", "view", "mask", "grid", "evidence" });
    const auto& grid = value.at("grid");
    keys(grid, { "width", "height", "image_to_mask" });
    array(grid.at("image_to_mask"), 4uz);
    require(grid.at("image_to_mask").size() == 4uz, "Invalid annotation transform selection");
    ObjectObservationSelection selected {
        reference(value.at("product")),
        { integer(value.at("observation_id")) },
        integer(value.at("revision")),
        view(value.at("view")),
        reference(value.at("mask")),
        { integer(grid.at("width")), integer(grid.at("height")), {} },
        {} };
    for (std::size_t index { 0uz }; index < 4uz; ++index) {
        selected.m_grid.m_image_to_mask[index] = number(grid.at("image_to_mask")[index]);
    }
    array(value.at("evidence"), 64uz);
    for (const auto& pin : value.at("evidence")) {
        selected.m_evidence.push_back(reference(pin));
    }
    return selected;
}

EvaluationInventorySelection inventory(const Json& value) {
    keys(value, { "inventory", "visibility", "items", "cells" });
    EvaluationInventorySelection selected {
        reference(value.at("inventory")), reference(value.at("visibility")), {}, {} };
    array(value.at("items"), 64uz);
    array(value.at("cells"), 4096uz);
    for (const auto& row : value.at("items")) {
        keys(row, { "track_id", "kind", "label", "required" });
        selected.m_items.push_back(
            { { integer(row.at("track_id")) },
             named<EvaluationItemKind>(row.at("kind"),
                                       std::array { "PROP", "FLOOR", "WALL", "CEILING", "OPENING" }),
             text(row.at("label"), 128uz),
             boolean(row.at("required")) });
    }
    for (const auto& row : value.at("cells")) {
        keys(row, { "track_id", "capture_id", "frame_id", "state", "annotation" });
        selected.m_cells.push_back(
            { { integer(row.at("track_id")) },
             { integer(row.at("capture_id")) },
             { integer(row.at("frame_id")) },
             named<EvaluationVisibility>(
                 row.at("state"),
                 std::array { "VISIBLE", "FULLY_OCCLUDED", "OUT_OF_VIEW", "UNKNOWN" }),
             optional_ref(row.at("annotation")) });
    }
    return selected;
}

EvaluationPathSelection paths(const Json& value) {
    keys(value, { "temporal", "inspection", "blocks", "path" });
    EvaluationPathSelection selected {
        reference(value.at("temporal")), reference(value.at("inspection")), {}, {} };
    array(value.at("blocks"), 64uz);
    array(value.at("path"), 64uz);
    std::size_t frames {};
    for (const auto& row : value.at("blocks")) {
        keys(row, { "id", "capture_id", "role", "frames" });
        array(row.at("frames"), 64uz);
        require(row.at("frames").size() <= 64uz - frames,
                "Temporal frame budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        frames += row.at("frames").size();
        EvaluationTemporalBlock block {
            integer(row.at("id")), { integer(row.at("capture_id")) }, role(row.at("role")), {} };
        for (const auto& frame : row.at("frames")) {
            block.m_frames.push_back({ integer(frame) });
        }
        selected.m_blocks.push_back(std::move(block));
    }
    for (const auto& row : value.at("path")) {
        keys(row, { "capture_id", "frame_id", "confidence" });
        selected.m_path.push_back(
            { { integer(row.at("capture_id")) },
             { integer(row.at("frame_id")) },
             named<EvaluationPoseConfidence>(
                 row.at("confidence"),
                 std::array { "UNASSESSED", "UNCERTAIN", "DECLARED_RELIABLE" }) });
    }
    return selected;
}

ControlledReferenceSelection controlled(const Json& value) {
    keys(value, { "product", "kind", "pairs" });
    ControlledReferenceSelection selected {
        reference(value.at("product")),
        named<ControlledReferenceKind>(
            value.at("kind"), std::array { "BACKSIDE", "EMPTY_BACKGROUND", "CHANGED_LIGHT" }),
        {} };
    array(value.at("pairs"), 64uz);
    for (const auto& row : value.at("pairs")) {
        keys(row,
             { "id",
              "subject",
              "baseline",
              "reference",
              "baseline_condition",
              "reference_condition",
              "reference_state",
              "baseline_lighting",
              "reference_lighting" });
        selected.m_pairs.push_back(
            { integer(row.at("id")),
             { integer(row.at("subject")) },
             view(row.at("baseline")),
             view(row.at("reference")),
             text(row.at("baseline_condition"), 64uz),
             text(row.at("reference_condition"), 64uz),
             named<ControlledSubjectState>(row.at("reference_state"),
                                           std::array { "PRESENT", "REMOVED" }),
             optional_ref(row.at("baseline_lighting")),
             optional_ref(row.at("reference_lighting")) });
    }
    return selected;
}

CaptureEnvelopeSelection envelope(const Json& value) {
    keys(value, { "product", "sources", "views" });
    CaptureEnvelopeSelection selected { reference(value.at("product")), {}, {} };
    array(value.at("sources"), 32uz);
    array(value.at("views"), 64uz);
    for (const auto& row : value.at("sources")) {
        keys(row,
             { "source", "source_frame_count", "condition", "scene_motion", "exposure_control" });
        selected.m_sources.push_back(
            { source(row.at("source")),
             row.at("source_frame_count").is_null()
                 ? std::nullopt
                 : std::optional { integer(row.at("source_frame_count")) },
             text(row.at("condition"), 64uz),
             named<CaptureSceneMotion>(row.at("scene_motion"),
                                       std::array { "UNASSESSED", "STATIC", "DYNAMIC" }),
             named<CaptureExposureControl>(row.at("exposure_control"),
                                           std::array { "UNASSESSED", "LOCKED", "AUTOMATIC" }) });
    }
    for (const auto& row : value.at("views")) {
        keys(row, { "view", "role" });
        selected.m_views.push_back({ view(row.at("view")), role(row.at("role")) });
    }
    return selected;
}

EvaluationProtocolSelection protocol(const Json& value, std::uint32_t request_version) {
    if (request_version == 1) {
        keys(value,
             { "product",
              "artifacts",
              "masks",
              "lights",
              "geometry_samples",
              "geometry_seed",
              "uv_resolution",
              "reviewers_min" });
    } else {
        keys(value,
             { "schema_version",
              "product",
              "artifacts",
              "masks",
              "lights",
              "geometry_samples",
              "geometry_seed",
              "uv_resolution",
              "reviewers_min" });
    }
    EvaluationProtocolSelection selected {
        reference(value.at("product")),
        {},
        {},
        {},
        integer(value.at("geometry_samples")),
        integer(value.at("geometry_seed")),
        integer32(value.at("uv_resolution")),
        integer32(value.at("reviewers_min")),
        request_version == 1 ? 1 : schema_version(value.at("schema_version")) };
    constexpr std::array K_ARTIFACTS {
        "GEOMETRY_SAMPLER", "UV_SAMPLER", "RENDERER", "TONE_MAPPER", "METRICS", "REVIEW_RUBRIC" };
    keys(value.at("artifacts"),
         { "GEOMETRY_SAMPLER", "UV_SAMPLER", "RENDERER", "TONE_MAPPER", "METRICS", "REVIEW_RUBRIC" });
    for (std::size_t index { 0uz }; index < K_ARTIFACTS.size(); ++index) {
        selected.m_artifacts.emplace(static_cast<EvaluationArtifact>(index),
                                     reference(value.at("artifacts").at(K_ARTIFACTS[index])));
    }
    array(value.at("masks"), 32uz);
    array(value.at("lights"), 8uz);
    for (const auto& row : value.at("masks")) {
        keys(row, { "track", "view", "product" });
        selected.m_masks.push_back(
            { named<EvaluationTrack>(row.at("track"), std::array { "OBSERVED", "COMPLETED" }),
             view(row.at("view")),
             reference(row.at("product")) });
    }
    for (const auto& row : value.at("lights")) {
        keys(row, { "label", "product", "seed", "near_light" });
        selected.m_lights.push_back({ text(row.at("label"), 64uz),
                                     reference(row.at("product")),
                                     integer(row.at("seed")),
                                     boolean(row.at("near_light")) });
    }
    return selected;
}

AdmittedAssetReceipt receipt(const Json& value,
                             const AssetDependencyContext& expected,
                             const std::filesystem::path& base,
                             InspectionBudget& budget,
                             const std::stop_token& stop) {
    if (budget.m_request_version == 1) {
        keys(value, { "directory", "sha256" });
    } else {
        keys(value, { "schema_version", "directory", "sha256" });
    }
    const auto K_VERSION =
        budget.m_request_version == 1 ? 1 : schema_version(value.at("schema_version"));
    require(budget.m_receipt_bytes < budget.m_receipt_limit,
            "Inspection retained receipt budget exhausted",
            ErrorCode::RESOURCE_EXHAUSTED);
    // The native reader checks the complete inventory against this remaining
    // allowance before reading a single payload. Repeated snapshots count again.
    auto result = take(read_asset_receipt({ path(value.at("directory"), base),
                                           digest(value.at("sha256")),
                                           static_cast<AssetReceiptProfile>(K_VERSION),
                                           budget.m_receipt_limit - budget.m_receipt_bytes },
                                          expected,
                                          stop));
    budget.m_receipt_bytes += result.payload_bytes();
    return result;
}

AdmittedCaptureMeasurement measurement(const Json& value,
                                       const AdmittedCaptureSelection& selection,
                                       const AdmittedAssetReceipt& bytes,
                                       const AssetDependencyContext& expected,
                                       const std::filesystem::path& base,
                                       const std::stop_token& stop) {
    keys(value, { "selection", "bundle", "manifest_sha256" });
    const auto& pin = value.at("selection");
    keys(pin, { "product", "view", "scene", "alignment" });
    const auto& scene = pin.at("scene");
    keys(scene, { "scene_id", "world_epoch", "revision", "sha256" });
    const CaptureMeasurementSelection K_SELECTED { reference(pin.at("product")),
                                                  view(pin.at("view")),
                                                  { { integer(scene.at("scene_id")) },
                                                   { integer(scene.at("world_epoch")) },
                                                   { integer(scene.at("revision")) },
                                                   digest(scene.at("sha256")) },
                                                  reference(pin.at("alignment")) };
    BundleLimits limits;
    limits.m_scene.m_max_splats = 0;
    limits.m_scene.m_max_chunks = 64;
    limits.m_scene.m_max_payload_bytes = 32uz * 1024uz * 1024uz;
    limits.m_scene.m_max_texture_texels = 0;
    limits.m_scene.m_max_mesh_vertices = 262144;
    limits.m_scene.m_max_mesh_triangles = 524288;
    const auto K_SCENE = take(
        load_bundle(path(value.at("bundle"), base), limits, digest(value.at("manifest_sha256"))));
    check_stop(stop);
    return take(
        inspect_capture_measurement(K_SELECTED, selection, bytes, *K_SCENE, expected, stop));
}

AssetCorpusCaptureInput capture_input(const Json& value,
                                      const std::filesystem::path& base,
                                      InspectionBudget& budget,
                                      const std::stop_token& stop) {
    keys(value,
         { "label",
          "receipt",
          "context",
          "registry",
          "commit",
          "selection",
          "annotations",
          "measurement",
          "inventory",
          "paths",
          "references",
          "envelope",
          "protocol" });
    const auto K_CONTEXT = context(value.at("context"));
    const auto K_SELECTED = capture(value.at("selection"), budget);
    const auto K_COMMIT = commit(value.at("commit"));
    const auto K_REGISTRY = path(value.at("registry"), base);
    const auto K_LABEL = text(value.at("label"), 64uz);
    array(value.at("annotations"), 128uz);
    array(value.at("references"), 3uz);
    const auto K_BYTES = receipt(value.at("receipt"), K_CONTEXT, base, budget, stop);
    const auto K_SNAPSHOT = take(read_immutable_records(K_REGISTRY, stop));
    require(K_SNAPSHOT.m_commit.m_generation == K_COMMIT.m_generation &&
                K_SNAPSHOT.m_commit.m_manifest_sha256 == K_COMMIT.m_manifest_sha256,
            "Capture registry differs from independent inspection commit",
            ErrorCode::RESYNC_REQUIRED);
    std::vector<AdmittedObjectObservation> annotations;
    for (const auto& row : value.at("annotations")) {
        annotations.push_back(
            take(inspect_object_observation(K_BYTES, annotation(row), K_CONTEXT, stop)));
    }
    const auto K_CAPTURE =
        take(inspect_capture_selection(K_BYTES, K_SELECTED, annotations, K_CONTEXT, stop));
    AssetCorpusCaptureInput result { K_LABEL, K_CAPTURE, K_BYTES, K_REGISTRY };
    if (!value.at("measurement").is_null()) {
        result.m_measurement =
            measurement(value.at("measurement"), K_CAPTURE, K_BYTES, K_CONTEXT, base, stop);
    }
    if (!value.at("inventory").is_null()) {
        result.m_inventory = take(inspect_evaluation_inventory(
            inventory(value.at("inventory")), K_CAPTURE, K_BYTES, K_CONTEXT, stop));
    }
    if (!value.at("paths").is_null()) {
        result.m_paths = take(inspect_evaluation_paths(
            paths(value.at("paths")), K_CAPTURE, K_BYTES, K_CONTEXT, stop));
    }
    for (const auto& row : value.at("references")) {
        if (!result.m_inventory) {
            throw RecordFailure(ErrorCode::INVALID_DATA,
                                "Controlled reference selection requires independent inventory");
        }
        result.m_references.push_back(take(inspect_controlled_reference(
            controlled(row), result.m_inventory.value(), K_CAPTURE, K_BYTES, K_CONTEXT, stop)));
    }
    if (!value.at("envelope").is_null()) {
        result.m_envelope = take(inspect_capture_envelope(
            envelope(value.at("envelope")), K_CAPTURE, K_BYTES, K_CONTEXT, stop));
    }
    if (!value.at("protocol").is_null()) {
        result.m_protocol = take(
            inspect_evaluation_protocol(protocol(value.at("protocol"), budget.m_request_version),
                                        K_CAPTURE,
                                        K_BYTES,
                                        K_CONTEXT,
                                        stop));
    }
    return result;
}

}  // namespace

Result<AdmittedAssetCorpus> inspect_asset_corpus_request(const std::filesystem::path& request,
                                                         const std::string& expected_sha256,
                                                         const std::stop_token& stop) {
    try {
        check_stop(stop);
        digest_text(expected_sha256);
        const auto K_BYTES = take(asset_files::read_bytes(request, K_REQUEST_BYTES, stop));
        require(sha256(K_BYTES) == expected_sha256,
                "Corpus inspection request hash differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_DOC = parse(K_BYTES, K_REQUEST_BYTES, stop);
        const auto K_VERSION = schema_version(K_DOC.at("schema_version"));
        InspectionBudget budget;
        budget.m_request_version = K_VERSION;
        if (K_VERSION == 1) {
            keys(K_DOC, { "schema_version", "corpus", "captures", "recipes" });
        } else {
            keys(K_DOC,
                 { "schema_version", "receipt_budget_bytes", "corpus", "captures", "recipes" });
            const auto K_LIMIT = integer(K_DOC.at("receipt_budget_bytes"));
            require(K_LIMIT > 0 && K_LIMIT <= 1024uz * 1024uz * 1024uz,
                    "Inspection receipt budget must be within 1 byte..1 GiB",
                    ErrorCode::RESOURCE_EXHAUSTED);
            budget.m_receipt_limit = static_cast<std::size_t>(K_LIMIT);
        }
        array(K_DOC.at("captures"), 16uz);
        array(K_DOC.at("recipes"), 16uz);
        require(!K_DOC.at("corpus").is_null() && !K_DOC.at("captures").empty() &&
                    !K_DOC.at("recipes").empty(),
                "Independent corpus/seal pin, capture selections/commits and recipe receipts are "
                "required; reconstruction outputs cannot supply them",
                ErrorCode::INVALID_ARGUMENT);
        const auto& selected = K_DOC.at("corpus");
        keys(selected, { "directory", "commit", "selected" });
        const auto& pin = selected.at("selected");
        keys(pin, { "corpus", "revision", "sha256" });
        const AssetCorpusRef K_REF {
            text(pin.at("corpus"), 64uz), integer(pin.at("revision")), digest(pin.at("sha256")) };
        const auto K_COMMIT = commit(selected.at("commit"));
        const auto K_BASE = std::filesystem::absolute(request).parent_path();
        std::vector<AssetCorpusCaptureInput> captures;
        std::vector<AssetCorpusRecipeInput> recipes;
        std::map<std::string, RecordCommit> capture_commits;
        for (const auto& row : K_DOC.at("captures")) {
            check_stop(stop);
            require(capture_commits.emplace(text(row.at("label"), 64uz), commit(row.at("commit")))
                        .second,
                    "Repeated independent capture label");
            captures.push_back(capture_input(row, K_BASE, budget, stop));
        }
        for (const auto& row : K_DOC.at("recipes")) {
            check_stop(stop);
            keys(row, { "label", "receipt", "context" });
            recipes.push_back(
                { text(row.at("label"), 64uz),
                 receipt(row.at("receipt"), context(row.at("context")), K_BASE, budget, stop) });
        }
        auto corpus = take(read_asset_corpus(
            path(selected.at("directory"), K_BASE), K_COMMIT, K_REF, captures, recipes, stop));
        // Compare both selections even if a registry changed between the two
        // reads. A pin in the inspected corpus cannot replace the request's pin.
        for (const auto& capture_pin : corpus.definition().m_captures) {
            const auto K_FOUND = capture_commits.find(capture_pin.m_label);
            require(K_FOUND != capture_commits.end() &&
                        K_FOUND->second.m_generation == capture_pin.m_commit.m_generation &&
                        K_FOUND->second.m_manifest_sha256 == capture_pin.m_commit.m_manifest_sha256,
                    "Corpus capture commit differs from independent inspection pin",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return corpus;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Corpus inspection allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
