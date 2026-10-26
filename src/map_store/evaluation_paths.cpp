#include "world_explorer/map_store/evaluation_paths.hpp"

#include "capture_metadata.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
using namespace capture_metadata_detail;
using ViewKey = std::pair<CaptureId, FrameId>;
constexpr std::size_t K_MAX_BYTES { 256uz * 1024uz };
constexpr std::array K_ROLES { "INFERENCE", "HELD_OUT", "EVALUATION_ONLY" };
constexpr std::array K_CONFIDENCE { "UNASSESSED", "UNCERTAIN", "DECLARED_RELIABLE" };

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    const auto K_FOUND = std::ranges::find(names, K_TEXT);
    require(K_FOUND != names.end(), "Unsupported evaluation-path enum");
    return static_cast<Enum>(std::distance(names.begin(), K_FOUND));
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    require(ref.m_product.m_value && ref.m_revision, "Unselected evaluation-path product");
    const auto K_FOUND = std::ranges::find(receipt.graph().m_products, ref, [](const auto& node) {
        return node.m_product;
    });
    require(K_FOUND != receipt.graph().m_products.end(),
            "Evaluation-path product differs",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

void header(const Json& value,
            const AssetDependencyNode& node,
            const AdmittedCaptureSelection& capture,
            const AssetDependencyContext& expected,
            const char* rows) {
    keys(value,
         { "schema_version",
          "world_epoch",
          "pose_revision",
          "recipe_sha256",
          "capture",
          "manual_ancestor",
          rows });
    require(integer(value.at("schema_version")) == 1,
            "Unsupported evaluation-path schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(value.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(value.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(value.at("recipe_sha256")) == node.m_recipe_sha256 &&
                reference(value.at("capture")) == capture.record().m_selection.m_product,
            "Evaluation-path capture/context/recipe differs",
            ErrorCode::RESYNC_REQUIRED);
    require(value.at("manual_ancestor").is_boolean() &&
                value.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Evaluation-path manual ancestry differs");
}

std::string record_key(const AssetRevisionRef& ref, const std::string& kind) {
    return kind + "-" + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

}  // namespace

struct AdmittedEvaluationPaths::Implementation {
    EvaluationPathSelection m_selection;
    AssetRevisionRef m_capture_product;
    std::string m_receipt_sha256;
    AssetDependencyContext m_context;
    bool m_manual_ancestor {}, m_uncertain {};
    std::vector<std::byte> m_temporal_bytes, m_inspection_bytes;
};

AdmittedEvaluationPaths::AdmittedEvaluationPaths(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const EvaluationPathSelection& AdmittedEvaluationPaths::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedEvaluationPaths::capture_product() const {
    return m_state->m_capture_product;
}

const std::string& AdmittedEvaluationPaths::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const AssetDependencyContext& AdmittedEvaluationPaths::context() const {
    return m_state->m_context;
}

bool AdmittedEvaluationPaths::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

bool AdmittedEvaluationPaths::has_unassessed_or_uncertain_poses() const {
    return m_state->m_uncertain;
}

Result<AdmittedEvaluationPaths> inspect_evaluation_paths(const EvaluationPathSelection& selected,
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
                "Evaluation paths use a foreign capture/context",
                ErrorCode::RESYNC_REQUIRED);
        require(!selected.m_blocks.empty() && selected.m_blocks.size() <= 64uz &&
                    !selected.m_path.empty() && selected.m_path.size() <= 64uz,
                "Evaluation-path table budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::size_t frame_count {};
        for (const auto& block : selected.m_blocks) {
            require(!block.m_frames.empty() && block.m_frames.size() <= 64uz,
                    "Temporal block frame budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            frame_count += block.m_frames.size();
            require(frame_count <= 64uz,
                    "Temporal aggregate frame budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
        }
        const auto& temporal_node = bind(selected.m_temporal, receipt);
        const auto& inspection_node = bind(selected.m_inspection, receipt);
        const auto K_SEPARATION = capture.validate_evaluation_separation(
            receipt, { selected.m_temporal, selected.m_inspection }, expected, stop);
        if (!K_SEPARATION) {
            return std::unexpected(K_SEPARATION.error());
        }
        require(temporal_node.m_dependencies ==
                    std::vector { capture.record().m_selection.m_product },
                "Temporal dependency differs");
        const auto K_TEMPORAL_BYTES =
            receipt.copy_blob(selected.m_temporal.m_content_sha256, K_MAX_BYTES, stop);
        const auto K_INSPECTION_BYTES =
            receipt.copy_blob(selected.m_inspection.m_content_sha256, K_MAX_BYTES, stop);
        if (!K_TEMPORAL_BYTES) {
            return std::unexpected(K_TEMPORAL_BYTES.error());
        }
        if (!K_INSPECTION_BYTES) {
            return std::unexpected(K_INSPECTION_BYTES.error());
        }
        const auto K_TEMPORAL = parse(*K_TEMPORAL_BYTES, K_MAX_BYTES, stop);
        const auto K_INSPECTION = parse(*K_INSPECTION_BYTES, K_MAX_BYTES, stop);
        header(K_TEMPORAL, temporal_node, capture, expected, "blocks");
        // The inspection body binds the selected temporal product as well as its ordered entries.
        header(K_INSPECTION, inspection_node, capture, expected, "path");
        const auto& path = K_INSPECTION.at("path");
        keys(path, { "temporal", "entries" });
        require(reference(path.at("temporal")) == selected.m_temporal,
                "Inspection temporal pin differs",
                ErrorCode::RESYNC_REQUIRED);
        array(K_TEMPORAL.at("blocks"), 64uz);
        array(path.at("entries"), 64uz);
        require(K_TEMPORAL.at("blocks").size() == selected.m_blocks.size() &&
                    path.at("entries").size() == selected.m_path.size(),
                "Evaluation-path independent table size differs",
                ErrorCode::RESYNC_REQUIRED);
        std::map<ViewKey, std::reference_wrapper<const CaptureFrameInputs>> frames;
        std::map<ViewKey, CaptureFrameRole> roles;
        std::map<CaptureId, std::string> sources;
        std::set<std::string> inference_sources;
        for (const auto& source : capture.record().m_selection.m_sources) {
            sources.emplace(source.m_capture, source.m_source_sha256);
        }
        const auto K_INFERENCE = capture.inference();
        for (const auto& frame : K_INFERENCE.frames()) {
            frames.emplace(ViewKey { frame.m_view.m_capture, frame.m_view.m_frame }, frame);
            roles.emplace(ViewKey { frame.m_view.m_capture, frame.m_view.m_frame },
                          CaptureFrameRole::INFERENCE);
            inference_sources.insert(sources.at(frame.m_view.m_capture));
        }
        for (const auto& frame : capture.evaluation()) {
            frames.emplace(ViewKey { frame.m_inputs.m_view.m_capture, frame.m_inputs.m_view.m_frame },
                           frame.m_inputs);
            roles.emplace(ViewKey { frame.m_inputs.m_view.m_capture, frame.m_inputs.m_view.m_frame },
                          frame.m_role);
        }
        std::set<ViewKey> consumed, expected_path;
        std::set<std::uint64_t> ids;
        std::map<CaptureId,
                 std::pair<std::reference_wrapper<const CaptureFrameInputs>, CaptureFrameRole>>
            previous;
        std::set<CaptureId> training, held_out;
        for (std::size_t index { 0uz }; index < selected.m_blocks.size(); ++index) {
            check_stop(stop);
            const auto& block = selected.m_blocks[index];
            const auto& row = K_TEMPORAL.at("blocks")[index];
            keys(row, { "block_id", "capture_id", "source_sha256", "role", "frames" });
            require(block.m_id && ids.insert(block.m_id).second &&
                        integer(row.at("block_id")) == block.m_id &&
                        integer(row.at("capture_id")) == block.m_capture.m_value &&
                        sources.contains(block.m_capture) &&
                        digest(row.at("source_sha256")) == sources.at(block.m_capture) &&
                        named<CaptureFrameRole>(row.at("role"), K_ROLES) == block.m_role,
                    "Temporal block identity/role/source differs",
                    ErrorCode::RESYNC_REQUIRED);
            array(row.at("frames"), 64uz);
            require(row.at("frames").size() == block.m_frames.size(),
                    "Temporal block frame count differs");
            auto last = previous.find(block.m_capture);
            require(last == previous.end() || last->second.second != block.m_role,
                    "Adjacent same-role temporal blocks must be merged");
            for (std::size_t frame_index { 0uz }; frame_index < block.m_frames.size();
                 ++frame_index) {
                check_stop(stop);
                const ViewKey K_KEY { block.m_capture, block.m_frames[frame_index] };
                const auto K_FOUND = frames.find(K_KEY);
                require(K_FOUND != frames.end() && consumed.insert(K_KEY).second &&
                            roles.at(K_KEY) == block.m_role,
                        "Temporal overlap/foreign frame/role differs");
                const auto& inputs = K_FOUND->second.get();
                camera(row.at("frames")[frame_index], inputs);
                if (last != previous.end()) {
                    const auto& prior = last->second.first.get();
                    const auto& a = prior.m_observation.m_timestamp;
                    const auto& b = inputs.m_observation.m_timestamp;
                    require(a.m_time_base_numerator == b.m_time_base_numerator &&
                                a.m_time_base_denominator == b.m_time_base_denominator &&
                                a.m_pts < b.m_pts &&
                                prior.m_observation.m_capture_time <
                                    inputs.m_observation.m_capture_time,
                            "Temporal order/constant source time base differs");
                }
                previous.insert_or_assign(block.m_capture,
                                          std::pair { std::cref(inputs), block.m_role });
                last = previous.find(block.m_capture);
                if (block.m_role == CaptureFrameRole::INFERENCE) {
                    training.insert(block.m_capture);
                }
                if (block.m_role == CaptureFrameRole::HELD_OUT) {
                    held_out.insert(block.m_capture);
                    if (!inference_sources.contains(sources.at(block.m_capture))) {
                        expected_path.insert(K_KEY);
                    }
                }
            }
        }
        require(consumed.size() == frames.size(), "Temporal blocks omit selected frames");
        require(std::ranges::any_of(training,
                                    [&](CaptureId id) {
                                        return held_out.contains(id);
                                    }),
                "Training-source temporal holdout missing");
        require(!expected_path.empty(), "Independent held-out inspection source missing");
        std::set<ViewKey> path_frames;
        std::map<AssetProductId, AssetRevisionRef> parents {
            { selected.m_temporal.m_product, selected.m_temporal } };
        std::optional<CaptureId> path_source;
        std::optional<std::chrono::nanoseconds> path_time;
        bool uncertain {};
        for (std::size_t index { 0uz }; index < selected.m_path.size(); ++index) {
            check_stop(stop);
            const auto& entry = selected.m_path[index];
            const ViewKey K_KEY { entry.m_capture, entry.m_frame };
            const auto& row = path.at("entries")[index];
            keys(row, { "camera", "confidence" });
            require(expected_path.contains(K_KEY) && path_frames.insert(K_KEY).second &&
                        named<EvaluationPoseConfidence>(row.at("confidence"), K_CONFIDENCE) ==
                            entry.m_confidence,
                    "Inspection overlap/inference/source/confidence differs");
            const auto& inputs = frames.at(K_KEY).get();
            camera(row.at("camera"), inputs);
            require((!path_source || *path_source == entry.m_capture) &&
                        (!path_time || *path_time < inputs.m_observation.m_capture_time),
                    "Inspection path source/order differs");
            path_source = entry.m_capture;
            path_time = inputs.m_observation.m_capture_time;
            uncertain =
                uncertain || entry.m_confidence != EvaluationPoseConfidence::DECLARED_RELIABLE;
            parents.emplace(inputs.m_view.m_image.m_product, inputs.m_view.m_image);
            parents.emplace(inputs.m_view.m_pose_asset.m_product, inputs.m_view.m_pose_asset);
        }
        require(path_frames == expected_path,
                "Inspection path omits selected independent held-out views");
        std::vector<AssetRevisionRef> expected_parents;
        expected_parents.reserve(parents.size());
        for (const auto& [K_ID, K_REF] : parents) {
            expected_parents.push_back(K_REF);
        }
        auto actual = inspection_node.m_dependencies;
        std::ranges::sort(actual);
        std::ranges::sort(expected_parents);
        require(actual == expected_parents, "Inspection dependency differs");
        check_stop(stop);
        return AdmittedEvaluationPaths(
            std::make_shared<const AdmittedEvaluationPaths::Implementation>(
                AdmittedEvaluationPaths::Implementation { selected,
                                                         capture.record().m_selection.m_product,
                                                         receipt.receipt_sha256(),
                                                         expected,
                                                         temporal_node.m_manual_ancestor ||
                                                             inspection_node.m_manual_ancestor,
                                                         uncertain,
                                                         *K_TEMPORAL_BYTES,
                                                         *K_INSPECTION_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Evaluation-path allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_evaluation_paths(const AdmittedEvaluationPaths& paths,
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
    if (paths.receipt_sha256() != receipt.receipt_sha256() ||
        paths.context().m_world_epoch != expected.m_world_epoch ||
        paths.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Evaluation paths publication context differs");
    }
    try {
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         { { record_key(paths.selection().m_temporal, "temporal"),
                                           paths.m_state->m_temporal_bytes },
                                          { record_key(paths.selection().m_inspection, "inspection"),
                                           paths.m_state->m_inspection_bytes } },
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Evaluation paths publication allocation failed");
    }
}

Result<AdmittedEvaluationPaths> read_evaluation_paths(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const EvaluationPathSelection& selected,
                                                      const AdmittedCaptureSelection& capture,
                                                      const AdmittedAssetReceipt& receipt,
                                                      const AssetDependencyContext& expected,
                                                      const std::stop_token& stop) {
    try {
        digest_text(commit.m_manifest_sha256);
        digest_text(selected.m_temporal.m_content_sha256);
        digest_text(selected.m_inspection.m_content_sha256);
        const auto K_SNAPSHOT = read_immutable_records(directory, stop);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        require(commit.m_generation && K_SNAPSHOT->m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT->m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Evaluation paths registry commit differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& entry : { std::pair { selected.m_temporal, "temporal" },
                                  std::pair { selected.m_inspection, "inspection" } }) {
            const auto K_FOUND = K_SNAPSHOT->m_records.find(record_key(entry.first, entry.second));
            require(K_FOUND != K_SNAPSHOT->m_records.end() &&
                        sha256(K_FOUND->second) == entry.first.m_content_sha256,
                    "Selected evaluation record is not committed",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return inspect_evaluation_paths(selected, capture, receipt, expected, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Evaluation paths read allocation failed");
    }
}

}  // namespace world_explorer
