#include "world_explorer/map_store/controlled_references.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr std::size_t K_MAX_BYTES { 256uz * 1024uz };
constexpr std::array K_KINDS { "BACKSIDE", "EMPTY_BACKGROUND", "CHANGED_LIGHT" };
constexpr std::array K_STATES { "PRESENT", "REMOVED" };

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    const auto K_FOUND = std::ranges::find(names, K_TEXT);
    require(K_FOUND != names.end(), "Unsupported controlled-reference enum");
    return static_cast<Enum>(std::distance(names.begin(), K_FOUND));
}

void token(const std::string& value) {
    require(!value.empty() && value.size() <= 64uz &&
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == value.npos,
            "Invalid controlled-reference condition");
}

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

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    require(ref.m_product.m_value && ref.m_revision, "Unselected evaluation record");
    const auto K_FOUND = std::ranges::find(receipt.graph().m_products, ref, [](const auto& node) {
        return node.m_product;
    });
    require(K_FOUND != receipt.graph().m_products.end(),
            "Evaluation product/revision differs",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

void header(const Json& value,
            const AssetDependencyNode& node,
            const AdmittedCaptureSelection& capture,
            const AssetDependencyContext& expected) {
    require(integer(value.at("schema_version")) == 1,
            "Unsupported evaluation record schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(value.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(value.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(value.at("recipe_sha256")) == node.m_recipe_sha256 &&
                reference(value.at("capture")) == capture.record().m_selection.m_product,
            "Evaluation record capture/context/recipe differs",
            ErrorCode::RESYNC_REQUIRED);
    require(value.at("manual_ancestor").is_boolean() &&
                value.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Evaluation manual ancestry differs");
}

std::string record_key(const AssetRevisionRef& ref, const std::string& kind) {
    return kind + "-" + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

std::optional<AssetRevisionRef> optional_ref(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional { reference(value) };
}

void check_view(const ObjectViewSelection& selected) {
    digest_text(selected.m_image.m_content_sha256);
    digest_text(selected.m_pose_asset.m_content_sha256);
}

std::optional<double> quantity(const Json& value, const char* unit, bool positive) {
    if (value.is_null()) {
        return std::nullopt;
    }
    keys(value, { "value", "unit" });
    require(value.at("unit").get<std::string>() == unit, "Unsupported lighting measurement unit");
    const auto K_VALUE = number(value.at("value"));
    require(positive ? K_VALUE > 0 : K_VALUE >= 0, "Invalid lighting measurement range");
    return K_VALUE;
}

ControlledLightingObservation lighting(const AssetRevisionRef& selected,
                                       const ObjectViewSelection& expected_view,
                                       const AdmittedAssetReceipt& receipt,
                                       const AssetDependencyContext& expected,
                                       std::size_t& total_bytes,
                                       const std::stop_token& stop) {
    const auto& node = bind(selected, receipt);
    const auto K_BYTES = receipt.copy_blob(selected.m_content_sha256, 64uz * 1024uz, stop);
    if (!K_BYTES) {
        throw RecordFailure(K_BYTES.error().m_code, K_BYTES.error().m_message);
    }
    total_bytes += K_BYTES->size();
    require(total_bytes <= 1024uz * 1024uz,
            "Lighting aggregate byte budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_DOC = parse(*K_BYTES, 64uz * 1024uz, stop);
    keys(K_DOC,
         { "schema_version",
          "world_epoch",
          "pose_revision",
          "recipe_sha256",
          "view",
          "illuminance",
          "color_temperature",
          "manual_ancestor" });
    require(integer(K_DOC.at("schema_version")) == 1,
            "Unsupported lighting schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(K_DOC.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(K_DOC.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(K_DOC.at("recipe_sha256")) == node.m_recipe_sha256 &&
                view(K_DOC.at("view")) == expected_view,
            "Lighting context/recipe/view differs",
            ErrorCode::RESYNC_REQUIRED);
    require(K_DOC.at("manual_ancestor").is_boolean() &&
                K_DOC.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Lighting manual ancestry differs");
    auto dependencies = node.m_dependencies;
    std::vector<AssetRevisionRef> parents { expected_view.m_image, expected_view.m_pose_asset };
    std::ranges::sort(dependencies);
    std::ranges::sort(parents);
    require(dependencies == parents, "Lighting view dependencies differ");
    const auto K_LUX = quantity(K_DOC.at("illuminance"), "lux", false);
    const auto K_KELVIN = quantity(K_DOC.at("color_temperature"), "kelvin", true);
    require(K_LUX.has_value() || K_KELVIN.has_value(), "Lighting observation has no measurement");
    return { selected, expected_view, K_LUX, K_KELVIN };
}

std::string controlled_key(const ControlledReferenceSelection& selected) {
    return record_key(selected.m_product, "controlled");
}

}  // namespace

struct AdmittedControlledReference::Implementation {
    ControlledReferenceSelection m_selection;
    AssetRevisionRef m_capture, m_inventory, m_visibility;
    std::string m_receipt_sha256;
    AssetDependencyContext m_context;
    std::vector<ControlledLightingObservation> m_lighting;
    bool m_manual_ancestor {}, m_missing_lighting {};
    std::vector<std::byte> m_bytes;
};

AdmittedControlledReference::AdmittedControlledReference(
    std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const ControlledReferenceSelection& AdmittedControlledReference::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedControlledReference::capture_product() const {
    return m_state->m_capture;
}

const AssetRevisionRef& AdmittedControlledReference::inventory_product() const {
    return m_state->m_inventory;
}

const AssetRevisionRef& AdmittedControlledReference::visibility_product() const {
    return m_state->m_visibility;
}

const std::string& AdmittedControlledReference::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const AssetDependencyContext& AdmittedControlledReference::context() const {
    return m_state->m_context;
}

const std::vector<ControlledLightingObservation>& AdmittedControlledReference::lighting() const {
    return m_state->m_lighting;
}

bool AdmittedControlledReference::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

bool AdmittedControlledReference::has_missing_lighting() const {
    return m_state->m_missing_lighting;
}

Result<AdmittedControlledReference>
inspect_controlled_reference(const ControlledReferenceSelection& selected,
                             const AdmittedEvaluationInventory& inventory,
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
                    inventory.receipt_sha256() == receipt.receipt_sha256() &&
                    inventory.capture_product() == capture.record().m_selection.m_product &&
                    capture.record().m_context.m_world_epoch == expected.m_world_epoch &&
                    capture.record().m_context.m_pose_revision == expected.m_pose_revision,
                "Controlled reference capture/inventory/context differs",
                ErrorCode::RESYNC_REQUIRED);
        require(!selected.m_pairs.empty() && selected.m_pairs.size() <= 32uz,
                "Controlled reference pair budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        for (const auto& pair : selected.m_pairs) {
            token(pair.m_baseline_condition);
            token(pair.m_reference_condition);
            check_view(pair.m_baseline);
            check_view(pair.m_reference);
            if (pair.m_baseline_lighting) {
                digest_text(pair.m_baseline_lighting->m_content_sha256);
            }
            if (pair.m_reference_lighting) {
                digest_text(pair.m_reference_lighting->m_content_sha256);
            }
        }
        const auto& node = bind(selected.m_product, receipt);
        const auto K_SEPARATION =
            capture.validate_evaluation_separation(receipt, { selected.m_product }, expected, stop);
        if (!K_SEPARATION) {
            return std::unexpected(K_SEPARATION.error());
        }
        const auto K_BYTES =
            receipt.copy_blob(selected.m_product.m_content_sha256, K_MAX_BYTES, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_DOC = parse(*K_BYTES, K_MAX_BYTES, stop);
        keys(K_DOC,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "capture",
              "inventory",
              "visibility",
              "kind",
              "pairs",
              "manual_ancestor" });
        header(K_DOC, node, capture, expected);
        require(reference(K_DOC.at("inventory")) == inventory.selection().m_inventory &&
                    reference(K_DOC.at("visibility")) == inventory.selection().m_visibility &&
                    named<ControlledReferenceKind>(K_DOC.at("kind"), K_KINDS) == selected.m_kind,
                "Controlled reference inventory/kind differs",
                ErrorCode::RESYNC_REQUIRED);
        array(K_DOC.at("pairs"), 32uz);
        require(K_DOC.at("pairs").size() == selected.m_pairs.size(),
                "Controlled reference selected pair count differs");
        std::map<std::uint64_t, ControlledReferencePair> pairs;
        std::set<std::tuple<ObjectTrackId, ObjectViewSelection, ObjectViewSelection>> unique;
        std::map<AssetProductId, AssetRevisionRef> parents {
            { capture.record().m_selection.m_product.m_product,
             capture.record().m_selection.m_product },
            { inventory.selection().m_inventory.m_product, inventory.selection().m_inventory },
            { inventory.selection().m_visibility.m_product, inventory.selection().m_visibility } };
        std::map<AssetProductId, ControlledLightingObservation> lights;
        std::size_t light_bytes {};
        bool missing_lighting {};
        for (const auto& pair : selected.m_pairs) {
            check_stop(stop);
            require(pair.m_id && pairs.emplace(pair.m_id, pair).second &&
                        unique.emplace(pair.m_subject, pair.m_baseline, pair.m_reference).second,
                    "Duplicate/invalid controlled reference pair");
            const auto K_SUBJECT = std::ranges::find(
                inventory.selection().m_items, pair.m_subject, &EvaluationInventoryItem::m_track);
            require(K_SUBJECT != inventory.selection().m_items.end() &&
                        K_SUBJECT->m_kind == EvaluationItemKind::PROP,
                    "Controlled reference subject is not an inventoried prop");
            require(std::ranges::any_of(capture.inference().frames(),
                                        [&](const auto& frame) {
                                            return frame.m_view == pair.m_baseline;
                                        }),
                    "Controlled reference baseline is not selected inference",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_REFERENCE =
                std::ranges::find(capture.evaluation(), pair.m_reference, [](const auto& frame) {
                    return frame.m_inputs.m_view;
                });
            require(K_REFERENCE != capture.evaluation().end() &&
                        pair.m_baseline.m_image.m_content_sha256 !=
                            pair.m_reference.m_image.m_content_sha256,
                    "Controlled reference view is not distinct evaluation",
                    ErrorCode::RESYNC_REQUIRED);
            const bool K_EMPTY { selected.m_kind == ControlledReferenceKind::EMPTY_BACKGROUND };
            require(pair.m_reference_state == (K_EMPTY ? ControlledSubjectState::REMOVED
                                                       : ControlledSubjectState::PRESENT),
                    "Controlled reference subject state differs");
            const auto K_CELL =
                std::ranges::find_if(inventory.selection().m_cells, [&](const auto& cell) {
                    return cell.m_track == pair.m_subject &&
                           cell.m_capture == pair.m_reference.m_capture &&
                           cell.m_frame == pair.m_reference.m_frame;
                });
            require(K_CELL != inventory.selection().m_cells.end() &&
                        K_CELL->m_state == (K_EMPTY ? EvaluationVisibility::OUT_OF_VIEW
                                                    : EvaluationVisibility::VISIBLE),
                    "Controlled reference visibility declaration differs");
            require(selected.m_kind != ControlledReferenceKind::CHANGED_LIGHT ||
                        pair.m_baseline_condition != pair.m_reference_condition,
                    "Changed-light condition labels must differ");
            for (const auto& ref : { pair.m_baseline.m_image,
                                    pair.m_baseline.m_pose_asset,
                                    pair.m_reference.m_image,
                                    pair.m_reference.m_pose_asset }) {
                parents.emplace(ref.m_product, ref);
            }
            for (const auto& entry : { std::pair { pair.m_baseline_lighting, pair.m_baseline },
                                      std::pair { pair.m_reference_lighting, pair.m_reference } }) {
                if (!entry.first) {
                    missing_lighting = true;
                    continue;
                }
                const auto& ref = *entry.first;
                parents.emplace(ref.m_product, ref);
                const auto K_LIGHT = lights.find(ref.m_product);
                if (K_LIGHT == lights.end()) {
                    lights.emplace(
                        ref.m_product,
                        lighting(ref, entry.second, receipt, expected, light_bytes, stop));
                } else {
                    require(K_LIGHT->second.m_product == ref &&
                                K_LIGHT->second.m_view == entry.second,
                            "Lighting product reused for another view/revision");
                }
            }
        }
        std::set<std::uint64_t> parsed;
        for (const auto& row : K_DOC.at("pairs")) {
            check_stop(stop);
            keys(row,
                 { "pair_id",
                  "subject_track_id",
                  "baseline",
                  "reference",
                  "baseline_condition",
                  "reference_condition",
                  "reference_state",
                  "baseline_lighting",
                  "reference_lighting" });
            const ControlledReferencePair K_PAIR {
                integer(row.at("pair_id")),
                { integer(row.at("subject_track_id")) },
                view(row.at("baseline")),
                view(row.at("reference")),
                row.at("baseline_condition").get<std::string>(),
                row.at("reference_condition").get<std::string>(),
                named<ControlledSubjectState>(row.at("reference_state"), K_STATES),
                optional_ref(row.at("baseline_lighting")),
                optional_ref(row.at("reference_lighting")) };
            const auto K_FOUND = pairs.find(K_PAIR.m_id);
            require(K_FOUND != pairs.end() && parsed.insert(K_PAIR.m_id).second &&
                        K_FOUND->second == K_PAIR,
                    "Controlled reference differs from independent pair selection",
                    ErrorCode::RESYNC_REQUIRED);
        }
        std::vector<AssetRevisionRef> expected_parents;
        expected_parents.reserve(parents.size());
        for (const auto& [K_ID, K_REF] : parents) {
            expected_parents.push_back(K_REF);
        }
        auto actual_parents = node.m_dependencies;
        std::ranges::sort(actual_parents);
        std::ranges::sort(expected_parents);
        require(actual_parents == expected_parents, "Controlled reference dependencies differ");
        ControlledReferenceSelection owned { selected.m_product, selected.m_kind, {} };
        for (const auto& [K_ID, K_PAIR] : pairs) {
            owned.m_pairs.push_back(K_PAIR);
        }
        std::vector<ControlledLightingObservation> observations;
        observations.reserve(lights.size());
        for (const auto& [K_ID, K_LIGHT] : lights) {
            observations.push_back(K_LIGHT);
        }
        check_stop(stop);
        return AdmittedControlledReference(
            std::make_shared<const AdmittedControlledReference::Implementation>(
                AdmittedControlledReference::Implementation { std::move(owned),
                                                             capture.record().m_selection.m_product,
                                                             inventory.selection().m_inventory,
                                                             inventory.selection().m_visibility,
                                                             receipt.receipt_sha256(),
                                                             expected,
                                                             std::move(observations),
                                                             node.m_manual_ancestor,
                                                             missing_lighting,
                                                             *K_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Controlled reference allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit>
publish_controlled_reference(const AdmittedControlledReference& reference,
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
    if (reference.receipt_sha256() != receipt.receipt_sha256() ||
        reference.context().m_world_epoch != expected.m_world_epoch ||
        reference.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Controlled reference publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { controlled_key(reference.selection()), reference.m_state->m_bytes } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Controlled reference publication allocation failed");
    }
}

Result<AdmittedControlledReference>
read_controlled_reference(const std::filesystem::path& directory,
                          const RecordCommit& commit,
                          const ControlledReferenceSelection& selected,
                          const AdmittedEvaluationInventory& inventory,
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
                "Controlled reference registry commit differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_FOUND = K_SNAPSHOT->m_records.find(controlled_key(selected));
        require(K_FOUND != K_SNAPSHOT->m_records.end() &&
                    sha256(K_FOUND->second) == selected.m_product.m_content_sha256,
                "Selected controlled reference is not committed",
                ErrorCode::RESYNC_REQUIRED);
        return inspect_controlled_reference(selected, inventory, capture, receipt, expected, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Controlled reference read allocation failed");
    }
}

}  // namespace world_explorer
