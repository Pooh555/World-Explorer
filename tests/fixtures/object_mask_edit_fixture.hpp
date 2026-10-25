#pragma once

#include "capture_selection_fixture.hpp"
#include "world_explorer/application/object_mask_edit.hpp"

namespace world_explorer::object_mask_edit_test {

using namespace capture_selection_test;

struct MaskEditFixture {
    SelectionFixture m_before, m_after;
    RecipeFixture m_workspace;
    ReplaceObjectMaskCommand m_command;
    Json m_old_mask, m_old_record, m_new_mask, m_new_record;
    std::size_t m_mask_index {}, m_record_index {};
    CaptureSelection m_alternate_capture;

  private:
    std::optional<AdmittedAssetReceipt> m_input, m_output;
    std::optional<AdmittedManualActionJournal> m_journal;

  public:
    const AdmittedAssetReceipt& input() const {
        if (!m_input) {
            throw std::logic_error("Missing mask fixture input");
        }
        return *m_input;
    }

    const AdmittedAssetReceipt& output() const {
        if (!m_output) {
            throw std::logic_error("Missing mask fixture output");
        }
        return *m_output;
    }

    const AdmittedManualActionJournal& journal() const {
        if (!m_journal) {
            throw std::logic_error("Missing mask fixture journal");
        }
        return *m_journal;
    }

    explicit MaskEditFixture(bool evaluation_view = false,
                             CaptureFrameRole evaluation_role = CaptureFrameRole::HELD_OUT) {
        m_before.m_selection.m_frames[1uz].m_role = evaluation_role;
        m_after.m_selection.m_frames[1uz].m_role = evaluation_role;
        const auto K_INITIAL_BEFORE = m_before.freeze();
        const auto K_INITIAL_AFTER = m_after.freeze();
        (void)K_INITIAL_BEFORE;
        (void)K_INITIAL_AFTER;
        auto& region = m_before.m_object.m_region;
        m_mask_index = region.m_graph.m_products.size();
        m_record_index = m_mask_index + 1uz;
        auto mask = region.node(7001, "synthetic inference mask placeholder");
        auto record = region.node(7002, "synthetic inference observation placeholder");
        const std::string K_RECIPE { "synthetic inference mask recipe; no evaluation labels" };
        region.m_data[hash(K_RECIPE)] = K_RECIPE;
        mask.m_recipe_sha256 = hash(K_RECIPE);
        record.m_recipe_sha256 = hash(K_RECIPE);
        region.m_graph.m_products.insert(region.m_graph.m_products.end(), { mask, record });
        auto alternate = region.m_graph.m_products[m_before.m_manifest_index];
        alternate.m_product.m_product = { 9001 };
        region.m_graph.m_products.push_back(alternate);
        m_alternate_capture = m_before.m_selection;
        m_alternate_capture.m_product = alternate.m_product;
        m_command.m_before = { record.m_product,
                              { 191 },
                              1,
                              m_before.m_selection.m_frames[evaluation_view ? 1uz : 0uz].m_view,
                              mask.m_product,
                              m_before.m_object.m_selection.m_grid,
                              {} };
        m_old_mask = m_before.m_object.m_mask;
        m_old_mask["view"] = object_view(m_command.m_before.m_view);
        m_old_record = m_before.m_object.m_record;
        m_old_record["observation_id"] = 191;
        m_old_record["view"] = object_view(m_command.m_before.m_view);
        m_old_record["recipe_sha256"] = record.m_recipe_sha256;
        m_old_record["evidence"] = Json::array();
        m_input = stage(m_before, m_command.m_before, m_old_mask, m_old_record);
        m_command.m_draft = {
            { { 1 }, ManualActionKind::EDIT_MASK, { 1 }, 0, { m_command.m_before.m_mask } },
            m_workspace.m_root / "draft" };
        const ManualActionRequest K_ACTION { { 1 },
                                            ManualActionKind::EDIT_MASK,
                                            "synthetic-mask-editor",
                                            1000,
                                            500,
                                            200,
                                            { m_command.m_before.m_mask },
                                            "synthetic typed mask replacement" };
        const auto K_COMMIT = append_manual_action(
            K_ACTION, input(), m_workspace.m_root / "journal", { 1 }, 0, region.m_graph.m_context);
        REQUIRE(K_COMMIT);
        const auto K_JOURNAL =
            read_manual_action_journal(m_workspace.m_root / "journal", *K_COMMIT);
        REQUIRE(K_JOURNAL);
        m_journal = *K_JOURNAL;
        auto& revised = m_after.m_object.m_region;
        revised.m_graph = input().graph();
        revised.m_data = region.m_data;
        const auto K_STORE = read_immutable_records(journal().source_directory());
        REQUIRE(K_STORE);
        const auto& bytes = K_STORE->m_records.at("manual-1");
        const std::string K_TEXT(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        revised.m_data[hash(K_TEXT)] = K_TEXT;
        revised.m_graph.m_products[m_mask_index].m_manual_action_sha256 = hash(K_TEXT);
        for (auto index : { m_mask_index, m_record_index }) {
            ++revised.m_graph.m_products[index].m_product.m_revision;
            revised.m_graph.m_products[index].m_manual_ancestor = true;
        }
        m_command.m_after = m_command.m_before;
        m_command.m_after.m_revision = 2;
        m_new_mask = m_old_mask;
        m_new_record = m_old_record;
        m_new_mask["manual_ancestor"] = true;
        m_new_mask["runs"][0uz]["class"] = "UNKNOWN";
        m_new_record["manual_ancestor"] = true;
        m_new_record["revision"] = 2;
        save_after();
    }

    AdmittedAssetReceipt stage(SelectionFixture& side,
                               ObjectObservationSelection& selection,
                               const Json& mask,
                               Json& record) const {
        auto& region = side.m_object.m_region;
        const auto K_MASK = mask.dump();
        region.m_data[hash(K_MASK)] = K_MASK;
        auto& mask_node = region.m_graph.m_products[m_mask_index];
        mask_node.m_product.m_content_sha256 = hash(K_MASK);
        mask_node.m_dependencies = { selection.m_view.m_image, selection.m_view.m_pose_asset };
        selection.m_mask = mask_node.m_product;
        record["mask"] = region_ref(selection.m_mask);
        const auto K_RECORD = record.dump();
        region.m_data[hash(K_RECORD)] = K_RECORD;
        auto& target = region.m_graph.m_products[m_record_index];
        target.m_product.m_content_sha256 = hash(K_RECORD);
        target.m_dependencies = {
            selection.m_view.m_image, selection.m_view.m_pose_asset, selection.m_mask };
        target.m_dependencies.insert(
            target.m_dependencies.end(), selection.m_evidence.begin(), selection.m_evidence.end());
        selection.m_product = target.m_product;
        // Remove only this fixture's earlier staging inventory before refreezing.
        std::filesystem::remove_all(region.m_directory.m_root / "blobs");
        std::filesystem::create_directory(region.m_directory.m_root / "blobs");
        return region.freeze();
    }

    void save_after() {
        m_output = stage(m_after, m_command.m_after, m_new_mask, m_new_record);
    }

    Result<RecordCommit> publish(const std::stop_token& stop = {},
                                 const DraftMutationObserver& observed = {}) const {
        const auto K_BEFORE = m_before.inspect(input());
        const auto K_AFTER = m_after.inspect(output());
        REQUIRE(K_BEFORE);
        REQUIRE(K_AFTER);
        return replace_object_mask(m_command,
                                   input(),
                                   output(),
                                   journal(),
                                   *K_BEFORE,
                                   *K_AFTER,
                                   input().graph().m_context,
                                   stop,
                                   observed);
    }

    Result<InferenceObjectMask> read(const RecordCommit& selected,
                                     const std::stop_token& stop = {}) const {
        const auto K_BEFORE = m_before.inspect(input());
        const auto K_AFTER = m_after.inspect(output());
        REQUIRE(K_BEFORE);
        REQUIRE(K_AFTER);
        return read_object_mask_edit(m_command,
                                     input(),
                                     output(),
                                     journal(),
                                     *K_BEFORE,
                                     *K_AFTER,
                                     selected,
                                     input().graph().m_context,
                                     stop);
    }
};

}  // namespace world_explorer::object_mask_edit_test
