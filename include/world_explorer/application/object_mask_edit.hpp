#pragma once

#include "world_explorer/application/draft_mutations.hpp"
#include "world_explorer/map_store/capture_selection.hpp"

#include <array>
#include <memory>

namespace world_explorer {

struct ReplaceObjectMaskCommand {
    ReplaceDraftMetadataCommand m_draft;
    ObjectObservationSelection m_before, m_after;
};

// Owned inference view and edited mask only; no evaluation data, complete pose
// asset, observation labels/evidence bytes or source receipt. Constructed only
// after typed admission and independently pinned COMMITTED draft publication.
class InferenceObjectMask {
  public:
    InferenceObjectMask(const InferenceObjectMask&) = default;
    InferenceObjectMask& operator=(const InferenceObjectMask&) = default;
    const ObjectObservationSelection& selection() const;
    const std::vector<ObjectMaskSpan>& mask() const;
    const std::array<std::uint64_t, 4>& coverage() const;
    const CaptureFrameInputs& frame() const;
    const RecordCommit& commit() const;
    const std::string& result_sha256() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit InferenceObjectMask(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<InferenceObjectMask> read_object_mask_edit(const ReplaceObjectMaskCommand&,
                                                             const AdmittedAssetReceipt&,
                                                             const AdmittedAssetReceipt&,
                                                             const AdmittedManualActionJournal&,
                                                             const AdmittedCaptureSelection&,
                                                             const AdmittedCaptureSelection&,
                                                             const RecordCommit&,
                                                             const AssetDependencyContext&,
                                                             const std::stop_token&);
};

// EDIT_MASK, exactly the old mask as action target. Both observations undergo
// native admission before any intent. View, semantic identity, class/association
// hypotheses and evidence remain fixed. Grid/runs may change. Generic draft
// bounds still apply; other descendants are byte/lineage checked, not qualified.
Result<RecordCommit> replace_object_mask(const ReplaceObjectMaskCommand& command,
                                         const AdmittedAssetReceipt& before,
                                         const AdmittedAssetReceipt& after,
                                         const AdmittedManualActionJournal& journal,
                                         const AdmittedCaptureSelection& before_capture,
                                         const AdmittedCaptureSelection& after_capture,
                                         const AssetDependencyContext& expected,
                                         const std::stop_token& stop = {},
                                         const DraftMutationObserver& observed = {});

Result<InferenceObjectMask> read_object_mask_edit(const ReplaceObjectMaskCommand& command,
                                                  const AdmittedAssetReceipt& before,
                                                  const AdmittedAssetReceipt& after,
                                                  const AdmittedManualActionJournal& journal,
                                                  const AdmittedCaptureSelection& before_capture,
                                                  const AdmittedCaptureSelection& after_capture,
                                                  const RecordCommit& selected,
                                                  const AssetDependencyContext& expected,
                                                  const std::stop_token& stop = {});

}  // namespace world_explorer
