#include "world_explorer/map_store/capture_envelope.hpp"

#include "capture_metadata.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
using namespace capture_metadata_detail;
using ViewKey = std::pair<CaptureId, FrameId>;
constexpr std::array K_MOTION { "UNASSESSED", "STATIC", "DYNAMIC" };
constexpr std::array K_EXPOSURE { "UNASSESSED", "LOCKED", "AUTOMATIC" };
constexpr std::array K_ROLES { "INFERENCE", "HELD_OUT", "EVALUATION_ONLY" };

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    const auto K_FOUND = std::ranges::find(names, K_TEXT);
    require(K_FOUND != names.end(), "Unsupported capture-envelope enum");
    return static_cast<Enum>(std::distance(names.begin(), K_FOUND));
}

void token(const std::string& value) {
    require(!value.empty() && value.size() <= 64uz &&
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == value.npos,
            "Invalid capture condition label");
}

std::string record_key(const AssetRevisionRef& ref) {
    return "envelope-" + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

}  // namespace

struct AdmittedCaptureEnvelope::Implementation {
    CaptureEnvelopeSelection m_selection;
    AssetRevisionRef m_capture;
    std::string m_receipt_sha256;
    AssetDependencyContext m_context;
    bool m_manual_ancestor {}, m_unknown_counts {}, m_unassessed {}, m_dynamic {},
        m_automatic_exposure {};
    std::vector<std::byte> m_bytes;
};

AdmittedCaptureEnvelope::AdmittedCaptureEnvelope(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const CaptureEnvelopeSelection& AdmittedCaptureEnvelope::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedCaptureEnvelope::capture_product() const {
    return m_state->m_capture;
}

const std::string& AdmittedCaptureEnvelope::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const AssetDependencyContext& AdmittedCaptureEnvelope::context() const {
    return m_state->m_context;
}

bool AdmittedCaptureEnvelope::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

bool AdmittedCaptureEnvelope::has_unknown_source_counts() const {
    return m_state->m_unknown_counts;
}

bool AdmittedCaptureEnvelope::has_unassessed_conditions() const {
    return m_state->m_unassessed;
}

bool AdmittedCaptureEnvelope::has_dynamic_sources() const {
    return m_state->m_dynamic;
}

bool AdmittedCaptureEnvelope::has_automatic_exposure() const {
    return m_state->m_automatic_exposure;
}

Result<AdmittedCaptureEnvelope> inspect_capture_envelope(const CaptureEnvelopeSelection& selected,
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
                "Capture-envelope receipt/context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(!selected.m_sources.empty() && selected.m_sources.size() <= 32uz &&
                    !selected.m_views.empty() && selected.m_views.size() <= 64uz,
                "Capture-envelope source/view budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::map<CaptureId, CaptureSourceEnvelope> sources;
        std::map<ViewKey, CaptureEnvelopeView> views;
        for (const auto& source : selected.m_sources) {
            check_stop(stop);
            token(source.m_condition);
            digest_text(source.m_source.m_source_sha256);
            digest_text(source.m_source.m_rights.m_content_sha256);
            require(sources.emplace(source.m_source.m_capture, source).second,
                    "Duplicate selected capture-envelope source");
        }
        for (const auto& frame : selected.m_views) {
            check_stop(stop);
            digest_text(frame.m_view.m_image.m_content_sha256);
            digest_text(frame.m_view.m_pose_asset.m_content_sha256);
            require(
                views.emplace(ViewKey { frame.m_view.m_capture, frame.m_view.m_frame }, frame).second,
                "Duplicate selected capture-envelope view");
        }
        require(sources.size() == capture.record().m_selection.m_sources.size() &&
                    views.size() == capture.record().m_selection.m_frames.size(),
                "Capture envelope omits/invents source/view rows",
                ErrorCode::RESYNC_REQUIRED);
        std::map<CaptureId, std::uint64_t> counts;
        for (const auto& frame : capture.record().m_selection.m_frames) {
            const auto K_FOUND = views.find({ frame.m_view.m_capture, frame.m_view.m_frame });
            require(K_FOUND != views.end() && K_FOUND->second.m_view == frame.m_view &&
                        K_FOUND->second.m_role == frame.m_role,
                    "Capture-envelope selected view/role differs",
                    ErrorCode::RESYNC_REQUIRED);
            ++counts[frame.m_view.m_capture];
        }
        bool unknown_counts {}, unassessed {}, dynamic {}, automatic_exposure {};
        for (const auto& source : capture.record().m_selection.m_sources) {
            const auto K_FOUND = sources.find(source.m_capture);
            require(K_FOUND != sources.end() && K_FOUND->second.m_source == source,
                    "Capture-envelope selected source differs",
                    ErrorCode::RESYNC_REQUIRED);
            const auto& envelope = K_FOUND->second;
            require(!envelope.m_source_frame_count ||
                        *envelope.m_source_frame_count >= counts.at(source.m_capture),
                    "Declared source frame count is smaller than selected coverage");
            unknown_counts = unknown_counts || !envelope.m_source_frame_count;
            unassessed = unassessed || envelope.m_scene_motion == CaptureSceneMotion::UNASSESSED ||
                         envelope.m_exposure_control == CaptureExposureControl::UNASSESSED;
            dynamic = dynamic || envelope.m_scene_motion == CaptureSceneMotion::DYNAMIC;
            automatic_exposure = automatic_exposure ||
                                 envelope.m_exposure_control == CaptureExposureControl::AUTOMATIC;
        }
        digest_text(selected.m_product.m_content_sha256);
        const auto K_NODE =
            std::ranges::find(receipt.graph().m_products, selected.m_product, [](const auto& node) {
                return node.m_product;
            });
        require(K_NODE != receipt.graph().m_products.end(),
                "Capture-envelope product differs",
                ErrorCode::RESYNC_REQUIRED);
        require(K_NODE->m_dependencies == std::vector { capture.record().m_selection.m_product },
                "Capture-envelope dependency differs");
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
              "sources",
              "views",
              "manual_ancestor" });
        require(integer(K_DOC.at("schema_version")) == 1,
                "Unsupported capture-envelope schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    digest(K_DOC.at("recipe_sha256")) == K_NODE->m_recipe_sha256 &&
                    reference(K_DOC.at("capture")) == capture.record().m_selection.m_product,
                "Capture-envelope header differs",
                ErrorCode::RESYNC_REQUIRED);
        require(K_DOC.at("manual_ancestor").is_boolean() &&
                    K_DOC.at("manual_ancestor").get<bool>() == K_NODE->m_manual_ancestor,
                "Capture-envelope ancestry differs");
        array(K_DOC.at("sources"), 32uz);
        array(K_DOC.at("views"), 64uz);
        require(K_DOC.at("sources").size() == sources.size() &&
                    K_DOC.at("views").size() == views.size(),
                "Capture-envelope serialized source/view count differs");
        std::set<CaptureId> source_ids;
        for (const auto& row : K_DOC.at("sources")) {
            check_stop(stop);
            keys(row,
                 { "capture_id",
                  "source_sha256",
                  "rights",
                  "source_frame_count",
                  "condition",
                  "scene_motion",
                  "exposure_control" });
            const CaptureSourceEnvelope K_SOURCE {
                { { integer(row.at("capture_id")) },
                 digest(row.at("source_sha256")),
                 reference(row.at("rights")) },
                row.at("source_frame_count").is_null()
                    ? std::nullopt
                    : std::optional { integer(row.at("source_frame_count")) },
                row.at("condition").get<std::string>(),
                named<CaptureSceneMotion>(row.at("scene_motion"), K_MOTION),
                named<CaptureExposureControl>(row.at("exposure_control"), K_EXPOSURE) };
            const auto K_FOUND = sources.find(K_SOURCE.m_source.m_capture);
            require(K_FOUND != sources.end() &&
                        source_ids.insert(K_SOURCE.m_source.m_capture).second &&
                        K_FOUND->second == K_SOURCE,
                    "Capture envelope differs from independent source selection",
                    ErrorCode::RESYNC_REQUIRED);
        }
        std::map<ViewKey, std::reference_wrapper<const CaptureFrameInputs>> native;
        for (const auto& frame : capture.inference().frames()) {
            native.emplace(ViewKey { frame.m_view.m_capture, frame.m_view.m_frame }, frame);
        }
        for (const auto& frame : capture.evaluation()) {
            native.emplace(ViewKey { frame.m_inputs.m_view.m_capture, frame.m_inputs.m_view.m_frame },
                           frame.m_inputs);
        }
        std::set<ViewKey> parsed_views;
        for (const auto& row : K_DOC.at("views")) {
            check_stop(stop);
            keys(row, { "camera", "calibration", "role" });
            const CaptureEnvelopeView K_VIEW { view(row.at("camera").at("view")),
                                              named<CaptureFrameRole>(row.at("role"), K_ROLES) };
            const ViewKey K_KEY { K_VIEW.m_view.m_capture, K_VIEW.m_view.m_frame };
            const auto K_FOUND = views.find(K_KEY);
            require(K_FOUND != views.end() && parsed_views.insert(K_KEY).second &&
                        K_FOUND->second == K_VIEW,
                    "Capture envelope differs from independent view selection",
                    ErrorCode::RESYNC_REQUIRED);
            camera(row.at("camera"), native.at(K_KEY).get());
            calibration(row.at("calibration"), native.at(K_KEY).get().m_calibration);
        }
        CaptureEnvelopeSelection owned { selected.m_product, {}, {} };
        owned.m_sources.reserve(sources.size());
        owned.m_views.reserve(views.size());
        for (const auto& [K_ID, K_SOURCE] : sources) {
            owned.m_sources.push_back(K_SOURCE);
        }
        for (const auto& [K_KEY, K_VIEW] : views) {
            owned.m_views.push_back(K_VIEW);
        }
        check_stop(stop);
        return AdmittedCaptureEnvelope(
            std::make_shared<const AdmittedCaptureEnvelope::Implementation>(
                AdmittedCaptureEnvelope::Implementation { std::move(owned),
                                                         capture.record().m_selection.m_product,
                                                         receipt.receipt_sha256(),
                                                         expected,
                                                         K_NODE->m_manual_ancestor,
                                                         unknown_counts,
                                                         unassessed,
                                                         dynamic,
                                                         automatic_exposure,
                                                         *K_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Capture-envelope allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_capture_envelope(const AdmittedCaptureEnvelope& envelope,
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
    if (envelope.receipt_sha256() != receipt.receipt_sha256() ||
        envelope.context().m_world_epoch != expected.m_world_epoch ||
        envelope.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Capture-envelope publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { record_key(envelope.selection().m_product), envelope.m_state->m_bytes } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Capture-envelope publication allocation failed");
    }
}

Result<AdmittedCaptureEnvelope> read_capture_envelope(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const CaptureEnvelopeSelection& selected,
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
                "Capture-envelope commit differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FOUND = K_SNAPSHOT->m_records.find(record_key(selected.m_product));
        require(K_FOUND != K_SNAPSHOT->m_records.end() &&
                    sha256(K_FOUND->second) == selected.m_product.m_content_sha256,
                "Selected capture envelope is not committed",
                ErrorCode::RESYNC_REQUIRED);
        return inspect_capture_envelope(selected, capture, receipt, expected, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Capture-envelope read allocation failed");
    }
}

}  // namespace world_explorer
