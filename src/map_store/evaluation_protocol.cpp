#include "world_explorer/map_store/evaluation_protocol.hpp"

#include "capture_metadata.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
using namespace capture_metadata_detail;
using MaskKey = std::tuple<EvaluationTrack, CaptureId, FrameId>;
constexpr std::array K_TRACKS { "OBSERVED", "COMPLETED" };
constexpr std::array K_ARTIFACTS {
    "GEOMETRY_SAMPLER", "UV_SAMPLER", "RENDERER", "TONE_MAPPER", "METRICS", "REVIEW_RUBRIC" };
constexpr std::array K_SURFACES { "UNKNOWN", "OBSERVED", "COMPLETED" };

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    const auto K_FOUND = std::ranges::find(names, K_TEXT);
    require(K_FOUND != names.end(), "Unsupported evaluation-protocol enum");
    return static_cast<Enum>(std::distance(names.begin(), K_FOUND));
}

void token(const std::string& value) {
    require(!value.empty() && value.size() <= 64uz &&
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == value.npos,
            "Invalid evaluation light label");
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    const auto K_FOUND = std::ranges::find(receipt.graph().m_products, ref, [](const auto& node) {
        return node.m_product;
    });
    require(K_FOUND != receipt.graph().m_products.end(),
            "Evaluation-protocol product differs",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

void header(const Json& value,
            const AssetDependencyNode& node,
            const AdmittedCaptureSelection& capture,
            const AssetDependencyContext& expected) {
    require(integer(value.at("schema_version")) == 1,
            "Unsupported evaluation-protocol schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(value.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(value.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(value.at("recipe_sha256")) == node.m_recipe_sha256 &&
                reference(value.at("capture")) == capture.record().m_selection.m_product,
            "Evaluation-protocol header differs",
            ErrorCode::RESYNC_REQUIRED);
    require(value.at("manual_ancestor").is_boolean() &&
                value.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Evaluation-protocol ancestry differs");
}

void fixed_metrics(const Json& value) {
    keys(value,
         { "color_space",
          "psnr_reduction",
          "ssim_window",
          "ssim_sigma",
          "ssim_k1",
          "ssim_k2",
          "data_range",
          "lpips_backbone",
          "lpips_normalize",
          "fit_test_exposure",
          "fit_test_color" });
    require(value.at("color_space").get<std::string>() == "DISPLAY_RGB_0_1" &&
                value.at("psnr_reduction").get<std::string>() == "PER_IMAGE_EQUAL_SCENE" &&
                integer(value.at("ssim_window")) == 11 && number(value.at("ssim_sigma")) == 1.5 &&
                number(value.at("ssim_k1")) == 0.01 && number(value.at("ssim_k2")) == 0.03 &&
                number(value.at("data_range")) == 1 &&
                value.at("lpips_backbone").get<std::string>() == "ALEXNET" &&
                value.at("lpips_normalize").is_boolean() &&
                value.at("lpips_normalize").get<bool>() &&
                value.at("fit_test_exposure").is_boolean() &&
                !value.at("fit_test_exposure").get<bool>() &&
                value.at("fit_test_color").is_boolean() && !value.at("fit_test_color").get<bool>(),
            "Evaluation metrics differ from frozen plan profile");
}

std::string record_key(const AssetRevisionRef& ref) {
    return "evaluation-protocol-" + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

}  // namespace

struct AdmittedEvaluationProtocol::Implementation {
    EvaluationProtocolSelection m_selection;
    AssetRevisionRef m_capture;
    std::string m_receipt_sha256;
    AssetDependencyContext m_context;
    std::vector<EvaluationMaskSummary> m_masks;
    bool m_manual_ancestor {}, m_empty_masks {};
    std::vector<std::byte> m_bytes;
};

AdmittedEvaluationProtocol::AdmittedEvaluationProtocol(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const EvaluationProtocolSelection& AdmittedEvaluationProtocol::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedEvaluationProtocol::capture_product() const {
    return m_state->m_capture;
}

const std::string& AdmittedEvaluationProtocol::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const AssetDependencyContext& AdmittedEvaluationProtocol::context() const {
    return m_state->m_context;
}

const std::vector<EvaluationMaskSummary>& AdmittedEvaluationProtocol::masks() const {
    return m_state->m_masks;
}

bool AdmittedEvaluationProtocol::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

bool AdmittedEvaluationProtocol::has_empty_masks() const {
    return m_state->m_empty_masks;
}

Result<AdmittedEvaluationProtocol>
inspect_evaluation_protocol(const EvaluationProtocolSelection& selected,
                            const AdmittedCaptureSelection& capture,
                            const AdmittedAssetReceipt& receipt,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        require(capture.receipt_sha256() == receipt.receipt_sha256() &&
                    capture.record().m_context.m_world_epoch == expected.m_world_epoch &&
                    capture.record().m_context.m_pose_revision == expected.m_pose_revision,
                "Evaluation-protocol capture/context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(!capture.evaluation().empty() && capture.evaluation().size() <= 16uz &&
                    selected.m_masks.size() == 2uz * capture.evaluation().size() &&
                    selected.m_artifacts.size() == 6uz && selected.m_lights.size() >= 3uz &&
                    selected.m_lights.size() <= 8uz,
                "Evaluation-protocol table budget/completeness differs",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(selected.m_geometry_samples && selected.m_geometry_samples <= 1000000 &&
                    selected.m_uv_resolution && selected.m_uv_resolution <= 8192 &&
                    selected.m_reviewers_min >= 3 && selected.m_reviewers_min <= 32,
                "Evaluation-protocol sampling/reviewer bounds exceeded");
        // Validate caller strings before owning selections.
        for (const auto& pin : selected.m_masks) {
            digest_text(pin.m_product.m_content_sha256);
            digest_text(pin.m_view.m_image.m_content_sha256);
            digest_text(pin.m_view.m_pose_asset.m_content_sha256);
        }
        for (const auto& light : selected.m_lights) {
            token(light.m_label);
            digest_text(light.m_product.m_content_sha256);
        }
        const auto& node = bind(selected.m_product, receipt);
        const auto K_SEPARATION =
            capture.validate_evaluation_separation(receipt, { selected.m_product }, expected, stop);
        if (!K_SEPARATION) {
            return std::unexpected(K_SEPARATION.error());
        }
        const auto K_BYTES =
            receipt.copy_blob(selected.m_product.m_content_sha256, 256uz * 1024uz, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_DOC = parse(*K_BYTES, 256uz * 1024uz, stop);
        keys(K_DOC,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "capture",
              "manual_ancestor",
              "sampling",
              "metrics",
              "render_columns",
              "review",
              "artifacts",
              "masks",
              "lights" });
        header(K_DOC, node, capture, expected);
        fixed_metrics(K_DOC.at("metrics"));
        const auto& sampling = K_DOC.at("sampling");
        keys(
            sampling,
            { "geometry_method", "geometry_samples", "geometry_seed", "uv_method", "uv_resolution" });
        require(sampling.at("geometry_method").get<std::string>() == "AREA_WEIGHTED_SURFACE" &&
                    sampling.at("uv_method").get<std::string>() == "TEXEL_CENTERS" &&
                    integer(sampling.at("geometry_samples")) == selected.m_geometry_samples &&
                    integer(sampling.at("geometry_seed")) == selected.m_geometry_seed &&
                    integer(sampling.at("uv_resolution")) == selected.m_uv_resolution,
                "Evaluation sampling differs from independent selection");
        require(K_DOC.at("render_columns") == Json::array({ "CAPTURED_APPEARANCE", "PBR" }),
                "Evaluation render columns differ");
        const auto& review = K_DOC.at("review");
        keys(review, { "reviewers_min", "blinded", "rating_min", "rating_max", "dimensions" });
        require(integer(review.at("reviewers_min")) == selected.m_reviewers_min &&
                    review.at("blinded").is_boolean() && review.at("blinded").get<bool>() &&
                    integer(review.at("rating_min")) == 1 &&
                    integer(review.at("rating_max")) == 5 &&
                    review.at("dimensions") == Json::array({ "GEOMETRY", "MATERIAL", "SCENE" }),
                "Evaluation blinded review profile differs");
        keys(K_DOC.at("artifacts"),
             { "GEOMETRY_SAMPLER",
              "UV_SAMPLER",
              "RENDERER",
              "TONE_MAPPER",
              "METRICS",
              "REVIEW_RUBRIC" });
        std::map<AssetProductId, AssetRevisionRef> parents {
            { capture.record().m_selection.m_product.m_product,
             capture.record().m_selection.m_product } };
        for (std::size_t index { 0uz }; index < K_ARTIFACTS.size(); ++index) {
            check_stop(stop);
            const auto K_FOUND = selected.m_artifacts.find(static_cast<EvaluationArtifact>(index));
            require(K_FOUND != selected.m_artifacts.end() &&
                        reference(K_DOC.at("artifacts").at(K_ARTIFACTS[index])) == K_FOUND->second,
                    "Evaluation config artifact selection differs",
                    ErrorCode::RESYNC_REQUIRED);
            (void)bind(K_FOUND->second, receipt);
            const auto K_CONFIG =
                receipt.copy_blob(K_FOUND->second.m_content_sha256, 64uz * 1024uz, stop);
            if (!K_CONFIG) {
                return std::unexpected(K_CONFIG.error());
            }
            require(!K_CONFIG->empty(), "Empty evaluation config artifact");
            parents.emplace(K_FOUND->second.m_product, K_FOUND->second);
        }
        array(K_DOC.at("lights"), 8uz);
        array(K_DOC.at("masks"), 32uz);
        require(K_DOC.at("lights").size() == selected.m_lights.size() &&
                    K_DOC.at("masks").size() == selected.m_masks.size(),
                "Evaluation protocol independently selected table size differs");
        std::map<std::string, EvaluationLightPin> lights;
        std::set<std::string> light_hashes;
        bool near_light {};
        for (const auto& light : selected.m_lights) {
            check_stop(stop);
            require(lights.emplace(light.m_label, light).second &&
                        light_hashes.insert(light.m_product.m_content_sha256).second,
                    "Repeated evaluation light label/content");
            (void)bind(light.m_product, receipt);
            const auto K_LIGHT =
                receipt.copy_blob(light.m_product.m_content_sha256, 64uz * 1024uz, stop);
            if (!K_LIGHT) {
                return std::unexpected(K_LIGHT.error());
            }
            require(!K_LIGHT->empty(), "Empty evaluation light config");
            parents.emplace(light.m_product.m_product, light.m_product);
            near_light = near_light || light.m_near_light;
        }
        require(near_light, "No declared near evaluation light");
        std::set<std::string> parsed_lights;
        for (const auto& row : K_DOC.at("lights")) {
            check_stop(stop);
            keys(row, { "label", "product", "seed", "near_light" });
            require(row.at("near_light").is_boolean(), "Invalid near-light declaration");
            const EvaluationLightPin K_LIGHT { row.at("label").get<std::string>(),
                                              reference(row.at("product")),
                                              integer(row.at("seed")),
                                              row.at("near_light").get<bool>() };
            const auto K_FOUND = lights.find(K_LIGHT.m_label);
            require(K_FOUND != lights.end() && parsed_lights.insert(K_LIGHT.m_label).second &&
                        K_FOUND->second == K_LIGHT,
                    "Evaluation light differs from independent selection",
                    ErrorCode::RESYNC_REQUIRED);
        }
        std::map<MaskKey, EvaluationMaskPin> masks;
        for (const auto& pin : selected.m_masks) {
            require(
                (pin.m_track == EvaluationTrack::OBSERVED ||
                 pin.m_track == EvaluationTrack::COMPLETED) &&
                    masks
                        .emplace(MaskKey { pin.m_track, pin.m_view.m_capture, pin.m_view.m_frame },
                                 pin)
                        .second,
                "Invalid/repeated evaluation mask track/view");
        }
        std::set<MaskKey> parsed_masks;
        for (const auto& row : K_DOC.at("masks")) {
            check_stop(stop);
            keys(row, { "track", "view", "product" });
            const EvaluationMaskPin K_PIN { named<EvaluationTrack>(row.at("track"), K_TRACKS),
                                           view(row.at("view")),
                                           reference(row.at("product")) };
            const MaskKey K_KEY { K_PIN.m_track, K_PIN.m_view.m_capture, K_PIN.m_view.m_frame };
            const auto K_FOUND = masks.find(K_KEY);
            require(K_FOUND != masks.end() && parsed_masks.insert(K_KEY).second &&
                        K_FOUND->second == K_PIN,
                    "Evaluation mask pin differs from independent selection",
                    ErrorCode::RESYNC_REQUIRED);
        }
        std::vector<EvaluationMaskSummary> summaries;
        summaries.reserve(masks.size());
        std::size_t mask_bytes {}, total_runs {};
        std::uint64_t total_pixels {};
        bool empty_mask {};
        for (const auto& frame : capture.evaluation()) {
            for (auto track : { EvaluationTrack::OBSERVED, EvaluationTrack::COMPLETED }) {
                check_stop(stop);
                const auto K_FOUND = masks.find(
                    { track, frame.m_inputs.m_view.m_capture, frame.m_inputs.m_view.m_frame });
                require(K_FOUND != masks.end() && K_FOUND->second.m_view == frame.m_inputs.m_view,
                        "Evaluation masks omit/invent evaluation views",
                        ErrorCode::RESYNC_REQUIRED);
                const auto& pin = K_FOUND->second;
                const auto& mask_node = bind(pin.m_product, receipt);
                std::vector<AssetRevisionRef> expected_parents {
                    capture.record().m_selection.m_product,
                    pin.m_view.m_image,
                    pin.m_view.m_pose_asset };
                auto actual = mask_node.m_dependencies;
                std::ranges::sort(actual);
                std::ranges::sort(expected_parents);
                require(actual == expected_parents, "Evaluation mask dependency differs");
                parents.emplace(pin.m_product.m_product, pin.m_product);
                const auto K_MASK_BYTES =
                    receipt.copy_blob(pin.m_product.m_content_sha256, 256uz * 1024uz, stop);
                if (!K_MASK_BYTES) {
                    return std::unexpected(K_MASK_BYTES.error());
                }
                mask_bytes += K_MASK_BYTES->size();
                require(mask_bytes <= 4uz * 1024uz * 1024uz,
                        "Evaluation mask byte budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                const auto K_MASK = parse(*K_MASK_BYTES, 256uz * 1024uz, stop);
                keys(K_MASK,
                     { "schema_version",
                      "world_epoch",
                      "pose_revision",
                      "recipe_sha256",
                      "capture",
                      "manual_ancestor",
                      "view",
                      "track",
                      "width",
                      "height",
                      "runs" });
                header(K_MASK, mask_node, capture, expected);
                require(view(K_MASK.at("view")) == pin.m_view &&
                            named<EvaluationTrack>(K_MASK.at("track"), K_TRACKS) == track &&
                            integer(K_MASK.at("width")) == frame.m_inputs.m_calibration.m_width &&
                            integer(K_MASK.at("height")) == frame.m_inputs.m_calibration.m_height,
                        "Evaluation mask camera/grid/track differs",
                        ErrorCode::RESYNC_REQUIRED);
                const auto K_PIXELS =
                    static_cast<std::uint64_t>(frame.m_inputs.m_calibration.m_width) *
                    frame.m_inputs.m_calibration.m_height;
                require(K_PIXELS && K_PIXELS <= 16uz * 1024uz * 1024uz &&
                            total_pixels <= 64uz * 1024uz * 1024uz - K_PIXELS,
                        "Evaluation mask pixel budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                total_pixels += K_PIXELS;
                const auto& runs = K_MASK.at("runs");
                array(runs, 16384uz);
                require(runs.size() <= 65536uz - total_runs,
                        "Evaluation aggregate run budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                total_runs += runs.size();
                EvaluationMaskSummary summary { pin, K_PIXELS, {} };
                std::uint64_t cursor {};
                for (const auto& run : runs) {
                    check_stop(stop);
                    keys(run, { "first", "count", "eligible", "surface" });
                    const auto K_COUNT = integer(run.at("count"));
                    const auto K_SURFACE = named<std::size_t>(run.at("surface"), K_SURFACES);
                    require(integer(run.at("first")) == cursor && K_COUNT &&
                                K_COUNT <= K_PIXELS - cursor && run.at("eligible").is_boolean(),
                            "Evaluation mask partition is invalid");
                    cursor += K_COUNT;
                    if (run.at("eligible").get<bool>()) {
                        require(K_SURFACE != 0uz &&
                                    (track != EvaluationTrack::OBSERVED || K_SURFACE == 1uz),
                                "Completed/unknown surface cannot enter observed evaluation");
                        summary.m_eligible[K_SURFACE] += K_COUNT;
                    }
                }
                require(cursor == K_PIXELS, "Evaluation mask partition is incomplete");
                empty_mask =
                    empty_mask || (summary.m_eligible[1uz] == 0 && summary.m_eligible[2uz] == 0);
                summaries.push_back(std::move(summary));
            }
        }
        std::vector<AssetRevisionRef> expected_parents;
        expected_parents.reserve(parents.size());
        for (const auto& [K_ID, K_REF] : parents) {
            expected_parents.push_back(K_REF);
        }
        auto actual_parents = node.m_dependencies;
        std::ranges::sort(actual_parents);
        std::ranges::sort(expected_parents);
        require(actual_parents == expected_parents, "Evaluation protocol dependency set differs");
        auto owned = selected;
        std::ranges::sort(owned.m_masks);
        std::ranges::sort(owned.m_lights);
        check_stop(stop);
        return AdmittedEvaluationProtocol(
            std::make_shared<const AdmittedEvaluationProtocol::Implementation>(
                AdmittedEvaluationProtocol::Implementation { std::move(owned),
                                                            capture.record().m_selection.m_product,
                                                            receipt.receipt_sha256(),
                                                            expected,
                                                            std::move(summaries),
                                                            node.m_manual_ancestor,
                                                            empty_mask,
                                                            *K_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Evaluation-protocol allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_evaluation_protocol(const AdmittedEvaluationProtocol& protocol,
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
    if (protocol.receipt_sha256() != receipt.receipt_sha256() ||
        protocol.context().m_world_epoch != expected.m_world_epoch ||
        protocol.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Evaluation-protocol publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { record_key(protocol.selection().m_product), protocol.m_state->m_bytes } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Evaluation-protocol publication allocation failed");
    }
}

Result<AdmittedEvaluationProtocol>
read_evaluation_protocol(const std::filesystem::path& directory,
                         const RecordCommit& commit,
                         const EvaluationProtocolSelection& selected,
                         const AdmittedCaptureSelection& capture,
                         const AdmittedAssetReceipt& receipt,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop) {
    try {
        digest_text(commit.m_manifest_sha256);
        digest_text(selected.m_product.m_content_sha256);
        const auto K_SNAPSHOT = read_immutable_records(directory, stop);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        require(commit.m_generation && K_SNAPSHOT->m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT->m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Evaluation-protocol commit differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FOUND = K_SNAPSHOT->m_records.find(record_key(selected.m_product));
        require(K_FOUND != K_SNAPSHOT->m_records.end() &&
                    sha256(K_FOUND->second) == selected.m_product.m_content_sha256,
                "Selected evaluation protocol is not committed",
                ErrorCode::RESYNC_REQUIRED);
        return inspect_evaluation_protocol(selected, capture, receipt, expected, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Evaluation-protocol read allocation failed");
    }
}

}  // namespace world_explorer
