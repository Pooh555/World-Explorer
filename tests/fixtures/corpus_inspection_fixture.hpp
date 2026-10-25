#pragma once

#include "capture_envelope_fixture.hpp"
#include "capture_measurement_fixture.hpp"
#include "controlled_references_fixture.hpp"
#include "evaluation_protocol_fixture.hpp"
#include "world_explorer/application/corpus_inspection.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/scene_store/bundle.hpp"

namespace world_explorer::corpus_inspection_test {

using namespace asset_corpus_test;

inline Json inspection_commit(const RecordCommit& value) {
    return { { "generation", value.m_generation }, { "manifest_sha256", value.m_manifest_sha256 } };
}

inline Json inspection_receipt(const AdmittedAssetReceipt& value) {
    return { { "directory", value.source_directory().generic_string() },
            { "sha256", value.receipt_sha256() } };
}

inline Json inspection_source(const CaptureSourceSelection& value) {
    return { { "capture_id", value.m_capture.m_value },
            { "source_sha256", value.m_source_sha256 },
            { "rights", region_ref(value.m_rights) } };
}

inline Json inspection_capture(const CaptureSelection& value) {
    Json result {
        { "product", region_ref(value.m_product) },
        { "selection_id", value.m_selection.m_value },
        { "revision", value.m_revision },
        { "sources", Json::array() },
        { "frames", Json::array() },
        { "measurement", value.m_measurement ? region_ref(*value.m_measurement) : Json(nullptr) } };
    for (const auto& source : value.m_sources) {
        result["sources"].push_back(inspection_source(source));
    }
    for (const auto& frame : value.m_frames) {
        auto annotations = Json::array();
        for (const auto& pin : frame.m_annotations) {
            annotations.push_back(region_ref(pin));
        }
        result["frames"].push_back({ { "view", object_view(frame.m_view) },
                                    { "role", role_name(frame.m_role) },
                                    { "annotations", annotations } });
    }
    return result;
}

inline Json inspection_annotation(const ObjectObservationSelection& value) {
    Json result { { "product", region_ref(value.m_product) },
                 { "observation_id", value.m_observation.m_value },
                 { "revision", value.m_revision },
                 { "view", object_view(value.m_view) },
                 { "mask", region_ref(value.m_mask) },
                 { "grid",
                  { { "width", value.m_grid.m_width },
                   { "height", value.m_grid.m_height },
                   { "image_to_mask", value.m_grid.m_image_to_mask } } },
                 { "evidence", Json::array() } };
    for (const auto& pin : value.m_evidence) {
        result["evidence"].push_back(region_ref(pin));
    }
    return result;
}

inline Json inspection_inventory(const EvaluationInventorySelection& value) {
    Json result { { "inventory", region_ref(value.m_inventory) },
                 { "visibility", region_ref(value.m_visibility) },
                 { "items", Json::array() },
                 { "cells", Json::array() } };
    constexpr std::array K_KINDS { "PROP", "FLOOR", "WALL", "CEILING", "OPENING" };
    constexpr std::array K_STATES { "VISIBLE", "FULLY_OCCLUDED", "OUT_OF_VIEW", "UNKNOWN" };
    for (const auto& item : value.m_items) {
        result["items"].push_back({ { "track_id", item.m_track.m_value },
                                   { "kind", K_KINDS.at(static_cast<std::size_t>(item.m_kind)) },
                                   { "label", item.m_label },
                                   { "required", item.m_required } });
    }
    for (const auto& cell : value.m_cells) {
        result["cells"].push_back(
            { { "track_id", cell.m_track.m_value },
             { "capture_id", cell.m_capture.m_value },
             { "frame_id", cell.m_frame.m_value },
             { "state", K_STATES.at(static_cast<std::size_t>(cell.m_state)) },
             { "annotation", cell.m_annotation ? region_ref(*cell.m_annotation) : Json(nullptr) } });
    }
    return result;
}

inline Json inspection_paths(const EvaluationPathSelection& value) {
    Json result { { "temporal", region_ref(value.m_temporal) },
                 { "inspection", region_ref(value.m_inspection) },
                 { "blocks", Json::array() },
                 { "path", Json::array() } };
    for (const auto& block : value.m_blocks) {
        auto frames = Json::array();
        for (auto frame : block.m_frames) {
            frames.push_back(frame.m_value);
        }
        result["blocks"].push_back({ { "id", block.m_id },
                                    { "capture_id", block.m_capture.m_value },
                                    { "role", role_name(block.m_role) },
                                    { "frames", frames } });
    }
    constexpr std::array K_CONFIDENCE { "UNASSESSED", "UNCERTAIN", "DECLARED_RELIABLE" };
    for (const auto& entry : value.m_path) {
        result["path"].push_back(
            { { "capture_id", entry.m_capture.m_value },
             { "frame_id", entry.m_frame.m_value },
             { "confidence", K_CONFIDENCE.at(static_cast<std::size_t>(entry.m_confidence)) } });
    }
    return result;
}

inline Json inspection_protocol(const EvaluationProtocolSelection& value) {
    using namespace evaluation_protocol_test;
    Json result { { "product", region_ref(value.m_product) },
                 { "artifacts", Json::object() },
                 { "masks", Json::array() },
                 { "lights", Json::array() },
                 { "geometry_samples", value.m_geometry_samples },
                 { "geometry_seed", value.m_geometry_seed },
                 { "uv_resolution", value.m_uv_resolution },
                 { "reviewers_min", value.m_reviewers_min } };
    for (const auto& [K_KIND, K_REF] : value.m_artifacts) {
        result["artifacts"][K_ARTIFACTS.at(static_cast<std::size_t>(K_KIND))] = region_ref(K_REF);
    }
    for (const auto& mask : value.m_masks) {
        result["masks"].push_back(protocol_mask_pin(mask));
    }
    for (const auto& light : value.m_lights) {
        result["lights"].push_back(protocol_light(light));
    }
    return result;
}

inline Json inspection_envelope(const CaptureEnvelopeSelection& value) {
    Json result { { "product", region_ref(value.m_product) },
                 { "sources", Json::array() },
                 { "views", Json::array() } };
    for (const auto& source : value.m_sources) {
        auto row = capture_envelope_test::envelope_source(source);
        row.erase("capture_id");
        row.erase("source_sha256");
        row.erase("rights");
        row["source"] = inspection_source(source.m_source);
        result["sources"].push_back(row);
    }
    for (const auto& row : value.m_views) {
        result["views"].push_back(
            { { "view", object_view(row.m_view) }, { "role", role_name(row.m_role) } });
    }
    return result;
}

inline Json inspection_reference(const ControlledReferenceSelection& value) {
    Json result {
        { "product", region_ref(value.m_product) },
        { "kind", controlled_references_test::K_KINDS.at(static_cast<std::size_t>(value.m_kind)) },
        { "pairs", Json::array() } };
    for (const auto& pair : value.m_pairs) {
        auto row = controlled_references_test::controlled_pair(pair);
        row.erase("pair_id");
        row.erase("subject_track_id");
        row["id"] = pair.m_id;
        row["subject"] = pair.m_subject.m_value;
        result["pairs"].push_back(row);
    }
    return result;
}

struct InspectionFixture {
    RecipeFixture m_recipe, m_output;
    AdmittedAssetReceipt m_recipe_receipt;
    AssetCorpusCaptureInput m_input;
    AssetCorpusDefinition m_definition;
    Json m_request;
    AdmittedAssetCorpus m_corpus;

    explicit InspectionFixture(AssetCorpusCaptureInput input)
        : m_recipe_receipt(m_recipe.save()), m_input(std::move(input)), m_corpus(stage()) { }

    AdmittedAssetCorpus stage() {
        const auto& bytes = m_input.m_receipt;
        m_input.m_registry = m_output.m_root / "capture";
        const auto K_COMMIT = checked(publish_capture_selection(
            m_input.m_selection, bytes, m_input.m_registry, { 191 }, 0, bytes.graph().m_context));
        const auto& selection = m_input.m_selection.record().m_selection;
        const auto& recipe = m_recipe_receipt.graph().m_products.back();
        m_definition = { "synthetic-inspection",
                        1,
                        AssetCorpusTrack::SYNTHETIC,
                        { { m_input.m_label,
                          selection.m_product,
                          selection.m_selection,
                          selection.m_revision,
                          bytes.receipt_sha256(),
                          K_COMMIT,
                          {} } },
                        { { "import",
                          recipe.m_product,
                          m_recipe_receipt.receipt_sha256(),
                          recipe.m_recipe_sha256,
                          m_recipe.m_context,
                          m_recipe.m_host } } };
        auto& evidence = m_definition.m_captures[0uz].m_evidence;
        Json row { { "label", m_input.m_label },
                  { "receipt", inspection_receipt(bytes) },
                  { "context", { { "world_epoch", 3 }, { "pose_revision", 5 } } },
                  { "registry", m_input.m_registry.generic_string() },
                  { "commit", inspection_commit(K_COMMIT) },
                  { "selection", inspection_capture(selection) },
                  { "annotations", Json::array() },
                  { "measurement", nullptr },
                  { "inventory", nullptr },
                  { "paths", nullptr },
                  { "references", Json::array() },
                  { "envelope", nullptr },
                  { "protocol", nullptr } };
        for (const auto& frame : m_input.m_selection.evaluation()) {
            for (const auto& pin : frame.m_annotations) {
                row["annotations"].push_back(inspection_annotation(pin.record().m_selection));
            }
        }
        if (m_input.m_measurement) {
            m_definition.m_require_typed_scale = true;
            const auto& pin = m_input.m_measurement->selection();
            evidence[AssetCorpusEvidence::INDEPENDENT_SCALE] = pin.m_product;
            row["measurement"] = { { "selection",
                                   { { "product", region_ref(pin.m_product) },
                                    { "view", object_view(pin.m_view) },
                                    { "scene",
                                     { { "scene_id", pin.m_scene.m_scene.m_value },
                                      { "world_epoch", pin.m_scene.m_epoch.m_value },
                                      { "revision", pin.m_scene.m_revision.m_value },
                                      { "sha256", pin.m_scene.m_sha256 } } },
                                    { "alignment", region_ref(pin.m_alignment) } } },
                                  { "bundle", "source" },
                                  { "manifest_sha256", hash("replace with actual bundle manifest") } };
        }
        if (m_input.m_inventory) {
            m_definition.m_require_typed_inventory = true;
            const auto& pin = m_input.m_inventory->selection();
            evidence[AssetCorpusEvidence::INVENTORY] = pin.m_inventory;
            evidence[AssetCorpusEvidence::VISIBILITY] = pin.m_visibility;
            row["inventory"] = inspection_inventory(pin);
        }
        if (m_input.m_paths) {
            m_definition.m_require_typed_paths = true;
            const auto& pin = m_input.m_paths->selection();
            evidence[AssetCorpusEvidence::TEMPORAL_BLOCKS] = pin.m_temporal;
            evidence[AssetCorpusEvidence::INSPECTION_PATH] = pin.m_inspection;
            row["paths"] = inspection_paths(pin);
        }
        for (const auto& reference : m_input.m_references) {
            m_definition.m_require_typed_references = true;
            const auto& pin = reference.selection();
            evidence[static_cast<AssetCorpusEvidence>(static_cast<std::size_t>(pin.m_kind) + 6uz)] =
                pin.m_product;
            row["references"].push_back(inspection_reference(pin));
        }
        if (m_input.m_envelope) {
            m_definition.m_require_typed_envelope = true;
            evidence[AssetCorpusEvidence::CAPTURE_ENVELOPE] =
                m_input.m_envelope->selection().m_product;
            row["envelope"] = inspection_envelope(m_input.m_envelope->selection());
        }
        if (m_input.m_protocol) {
            m_definition.m_require_typed_protocol = true;
            evidence[AssetCorpusEvidence::EVALUATION_PROTOCOL] =
                m_input.m_protocol->selection().m_product;
            row["protocol"] = inspection_protocol(m_input.m_protocol->selection());
        }
        const auto K_CORPUS =
            checked(admit_asset_corpus(m_definition, { m_input }, { { "import", m_recipe_receipt } }));
        const auto K_SEAL =
            checked(publish_asset_corpus(K_CORPUS, m_output.m_root / "seal", { 192 }));
        m_request = { { "schema_version", 1 },
                     { "corpus",
                      { { "directory", "seal" },
                       { "commit", inspection_commit(K_SEAL) },
                       { "selected",
                        { { "corpus", K_CORPUS.reference().m_corpus },
                         { "revision", K_CORPUS.reference().m_revision },
                         { "sha256", K_CORPUS.reference().m_sha256 } } } } },
                     { "captures", Json::array({ row }) },
                     { "recipes",
                      Json::array({ { { "label", "import" },
                                    { "receipt", inspection_receipt(m_recipe_receipt) },
                                    { "context", { { "world_epoch", 3 }, { "pose_revision", 5 } } } } }) } };
        return K_CORPUS;
    }

    std::filesystem::path request() const {
        return m_output.m_root / "request.json";
    }

    std::string save() const {
        const auto K_TEXT = m_request.dump();
        write(request(), K_TEXT);
        return hash(K_TEXT);
    }

    Result<AdmittedAssetCorpus> inspect(const std::stop_token& stop = {}) const {
        return inspect_asset_corpus_request(request(), save(), stop);
    }
};

inline std::map<std::string, std::string> inspection_tree(const std::filesystem::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            if (std::filesystem::file_size(entry.path()) == 0) {
                result.emplace(std::filesystem::relative(entry.path(), root).generic_string(),
                               hash(""));
                continue;
            }
            const auto K_BYTES =
                checked(asset_files::read_bytes(entry.path(), 32uz * 1024uz * 1024uz, {}));
            result.emplace(std::filesystem::relative(entry.path(), root).generic_string(),
                           sha256(K_BYTES));
        }
    }
    return result;
}

}  // namespace world_explorer::corpus_inspection_test
