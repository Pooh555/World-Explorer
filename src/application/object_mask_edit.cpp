#include "world_explorer/application/object_mask_edit.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <span>
#include <tuple>

namespace world_explorer {
namespace {

using namespace json_record_detail;

struct Validated {
    AdmittedObjectObservation m_observation;
    ReplaceDraftMetadataCommand m_draft;
};

Json reference(const AssetRevisionRef& ref) {
    return { { "product_id", ref.m_product.m_value },
            { "revision", ref.m_revision },
            { "content_sha256", ref.m_content_sha256 } };
}

Result<Validated> validate(const ReplaceObjectMaskCommand& command,
                           const AdmittedAssetReceipt& before,
                           const AdmittedAssetReceipt& after,
                           const AdmittedCaptureSelection& before_capture,
                           const AdmittedCaptureSelection& after_capture,
                           const AssetDependencyContext& expected,
                           const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto& mutation = command.m_draft.m_mutation;
        require(!mutation.m_admission_sha256.has_value(),
                "Typed mask admission binding must be derived by the adapter",
                ErrorCode::INVALID_ARGUMENT);
        require(mutation.m_kind == ManualActionKind::EDIT_MASK &&
                    mutation.m_targets == std::vector { command.m_before.m_mask },
                "Mask edit must target exactly the selected original mask",
                ErrorCode::INVALID_ARGUMENT);
        require(before_capture.record().m_selection.m_product ==
                        after_capture.record().m_selection.m_product &&
                    before_capture.encoded_bytes() == after_capture.encoded_bytes(),
                "Mask edit changes the selected capture roles",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& [K_RECEIPT, K_CAPTURE, K_SELECTION] :
             { std::tuple {
                  std::cref(before), std::cref(before_capture), std::cref(command.m_before) },
              std::tuple {
                  std::cref(after), std::cref(after_capture), std::cref(command.m_after) } }) {
            const auto K_DEPENDENCY = K_CAPTURE.get().validate_inference_dependency(
                K_RECEIPT.get(), K_SELECTION.get().m_product, expected, stop);
            if (!K_DEPENDENCY) {
                return std::unexpected(K_DEPENDENCY.error());
            }
            const auto K_INPUTS = K_CAPTURE.get().inference();
            require(std::ranges::any_of(K_INPUTS.frames(),
                                        [&](const auto& frame) {
                                            return frame.m_view == K_SELECTION.get().m_view;
                                        }),
                    "Edited mask view is not an independently selected inference frame");
        }
        const auto K_BEFORE = inspect_object_observation(before, command.m_before, expected, stop);
        if (!K_BEFORE) {
            return std::unexpected(K_BEFORE.error());
        }
        const auto K_AFTER = inspect_object_observation(after, command.m_after, expected, stop);
        if (!K_AFTER) {
            return std::unexpected(K_AFTER.error());
        }
        const auto& old_record = K_BEFORE->record();
        const auto& new_record = K_AFTER->record();
        require(command.m_before.m_observation == command.m_after.m_observation &&
                    command.m_before.m_product.m_product == command.m_after.m_product.m_product &&
                    command.m_before.m_mask.m_product == command.m_after.m_mask.m_product &&
                    command.m_before.m_view == command.m_after.m_view &&
                    command.m_before.m_revision < std::numeric_limits<std::uint64_t>::max() &&
                    command.m_after.m_revision == command.m_before.m_revision + 1 &&
                    command.m_before.m_evidence == command.m_after.m_evidence &&
                    old_record.m_classes == new_record.m_classes &&
                    old_record.m_associations == new_record.m_associations &&
                    old_record.m_evidence == new_record.m_evidence,
                "Mask edit changes observation identity, source or semantic evidence",
                ErrorCode::RESYNC_REQUIRED);
        require(new_record.m_manual_ancestor, "Edited observation must retain manual ancestry");
        const Json K_BINDING { { "schema_version", 1 },
                              { "adapter", "OBJECT_MASK_EDIT" },
                              { "capture", reference(before_capture.record().m_selection.m_product) },
                              { "before_observation", reference(command.m_before.m_product) },
                              { "after_observation", reference(command.m_after.m_product) } };
        const auto K_TEXT = K_BINDING.dump();
        auto bound = command.m_draft;
        bound.m_mutation.m_admission_sha256 = sha256(std::as_bytes(std::span(K_TEXT)));
        return Validated { *K_AFTER, std::move(bound) };
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Mask edit allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace

struct InferenceObjectMask::Implementation {
    ObjectObservationSelection m_selection;
    std::vector<ObjectMaskSpan> m_mask;
    std::array<std::uint64_t, 4> m_coverage;
    CaptureFrameInputs m_frame;
    RecordCommit m_commit;
    std::string m_result_sha256;
    bool m_manual_ancestor;
};

InferenceObjectMask::InferenceObjectMask(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const ObjectObservationSelection& InferenceObjectMask::selection() const {
    return m_state->m_selection;
}

const std::vector<ObjectMaskSpan>& InferenceObjectMask::mask() const {
    return m_state->m_mask;
}

const std::array<std::uint64_t, 4>& InferenceObjectMask::coverage() const {
    return m_state->m_coverage;
}

const CaptureFrameInputs& InferenceObjectMask::frame() const {
    return m_state->m_frame;
}

const RecordCommit& InferenceObjectMask::commit() const {
    return m_state->m_commit;
}

const std::string& InferenceObjectMask::result_sha256() const {
    return m_state->m_result_sha256;
}

bool InferenceObjectMask::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

Result<DraftAttemptSnapshot>
attempt_object_mask_edit(const ReplaceObjectMaskCommand& command,
                         const DraftMetadataAttempt& attempt,
                         const AdmittedAssetReceipt& before,
                         const AdmittedAssetReceipt& after,
                         const AdmittedManualActionJournal& journal,
                         const AdmittedCaptureSelection& before_capture,
                         const AdmittedCaptureSelection& after_capture,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop,
                         const DraftMutationObserver& mutation_observed,
                         const DraftAttemptObserver& observed) {
    const auto K_TYPED =
        validate(command, before, after, before_capture, after_capture, expected, stop);
    if (!K_TYPED) {
        return std::unexpected(K_TYPED.error());
    }
    return attempt_draft_metadata(K_TYPED->m_draft,
                                  attempt,
                                  before,
                                  after,
                                  journal,
                                  expected,
                                  stop,
                                  mutation_observed,
                                  observed);
}

Result<DraftAttemptSnapshot>
read_object_mask_attempt(const ReplaceObjectMaskCommand& command,
                         const DraftMetadataAttempt& attempt,
                         const AdmittedAssetReceipt& before,
                         const AdmittedAssetReceipt& after,
                         const AdmittedManualActionJournal& journal,
                         const AdmittedCaptureSelection& before_capture,
                         const AdmittedCaptureSelection& after_capture,
                         const RecordCommit& selected,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop) {
    const auto K_TYPED =
        validate(command, before, after, before_capture, after_capture, expected, stop);
    if (!K_TYPED) {
        return std::unexpected(K_TYPED.error());
    }
    return read_draft_metadata_attempt(attempt,
                                       K_TYPED->m_draft.m_mutation,
                                       before,
                                       after,
                                       journal,
                                       command.m_draft.m_directory,
                                       selected,
                                       expected,
                                       stop);
}

Result<RecordCommit> replace_object_mask(const ReplaceObjectMaskCommand& command,
                                         const AdmittedAssetReceipt& before,
                                         const AdmittedAssetReceipt& after,
                                         const AdmittedManualActionJournal& journal,
                                         const AdmittedCaptureSelection& before_capture,
                                         const AdmittedCaptureSelection& after_capture,
                                         const AssetDependencyContext& expected,
                                         const std::stop_token& stop,
                                         const DraftMutationObserver& observed) {
    const auto K_TYPED =
        validate(command, before, after, before_capture, after_capture, expected, stop);
    if (!K_TYPED) {
        return std::unexpected(K_TYPED.error());
    }
    return replace_draft_metadata(
        K_TYPED->m_draft, before, after, journal, expected, stop, observed);
}

Result<InferenceObjectMask> read_object_mask_edit(const ReplaceObjectMaskCommand& command,
                                                  const AdmittedAssetReceipt& before,
                                                  const AdmittedAssetReceipt& after,
                                                  const AdmittedManualActionJournal& journal,
                                                  const AdmittedCaptureSelection& before_capture,
                                                  const AdmittedCaptureSelection& after_capture,
                                                  const RecordCommit& selected,
                                                  const AssetDependencyContext& expected,
                                                  const std::stop_token& stop) {
    const auto K_TYPED =
        validate(command, before, after, before_capture, after_capture, expected, stop);
    if (!K_TYPED) {
        return std::unexpected(K_TYPED.error());
    }
    const auto K_DRAFT = read_draft_metadata_mutation(K_TYPED->m_draft.m_mutation,
                                                      before,
                                                      after,
                                                      journal,
                                                      command.m_draft.m_directory,
                                                      selected,
                                                      expected,
                                                      stop);
    if (!K_DRAFT) {
        return std::unexpected(K_DRAFT.error());
    }
    if (K_DRAFT->m_phase != DraftMutationPhase::COMMITTED) {
        return fail(ErrorCode::WOULD_BLOCK,
                    "Mask edit has a durable intent but no committed result");
    }
    try {
        const auto K_INPUTS = after_capture.inference();
        const auto K_FRAME = std::ranges::find_if(K_INPUTS.frames(), [&](const auto& frame) {
            return frame.m_view == command.m_after.m_view;
        });
        require(K_FRAME != K_INPUTS.frames().end(), "Selected inference frame disappeared");
        check_stop(stop);
        return InferenceObjectMask(std::make_shared<const InferenceObjectMask::Implementation>(
            InferenceObjectMask::Implementation {
                K_TYPED->m_observation.record().m_selection,
                K_TYPED->m_observation.mask(),
                K_TYPED->m_observation.coverage(),
                *K_FRAME,
                K_DRAFT->m_commit,
                K_DRAFT->m_result_sha256,
                K_TYPED->m_observation.record().m_manual_ancestor }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Inference mask allocation failed");
    }
}

}  // namespace world_explorer
