#include "world_explorer/map_store/evaluation_inventory.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
using ViewKey = std::pair<CaptureId, FrameId>;
using CellKey = std::tuple<ObjectTrackId, CaptureId, FrameId>;
constexpr std::array K_KINDS { std::pair { "PROP", EvaluationItemKind::PROP },
                              std::pair { "FLOOR", EvaluationItemKind::FLOOR },
                              std::pair { "WALL", EvaluationItemKind::WALL },
                              std::pair { "CEILING", EvaluationItemKind::CEILING },
                              std::pair { "OPENING", EvaluationItemKind::OPENING } };
constexpr std::array K_STATES { std::pair { "VISIBLE", EvaluationVisibility::VISIBLE },
                               std::pair { "FULLY_OCCLUDED", EvaluationVisibility::FULLY_OCCLUDED },
                               std::pair { "OUT_OF_VIEW", EvaluationVisibility::OUT_OF_VIEW },
                               std::pair { "UNKNOWN", EvaluationVisibility::UNKNOWN } };

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<std::pair<const char*, Enum>, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    for (const auto& entry : names) {
        if (K_TEXT == entry.first) {
            return entry.second;
        }
    }
    throw RecordFailure(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported inventory enum");
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

auto items(const std::vector<EvaluationInventoryItem>& selected) {
    require(!selected.empty() && selected.size() <= 64uz,
            "Inventory item budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    std::map<ObjectTrackId, EvaluationInventoryItem> result;
    bool required {};
    for (const auto& item : selected) {
        require(item.m_track.m_value && !item.m_label.empty() && item.m_label.size() <= 64uz &&
                    item.m_label.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") ==
                        item.m_label.npos,
                "Invalid inventory track/label");
        require(result.emplace(item.m_track, item).second, "Duplicate inventory track");
        required = required || item.m_required;
    }
    require(required, "Inventory requires at least one required item");
    return result;
}

auto cells(const std::vector<EvaluationVisibilityCell>& selected) {
    require(selected.size() <= 4096uz,
            "Visibility cell budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    std::map<CellKey, EvaluationVisibilityCell> result;
    for (const auto& cell : selected) {
        if (cell.m_annotation) {
            digest_text(cell.m_annotation->m_content_sha256);
        }
        require(result.emplace(CellKey { cell.m_track, cell.m_capture, cell.m_frame }, cell).second,
                "Duplicate inventory/view visibility cell");
    }
    return result;
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

}  // namespace

struct AdmittedEvaluationInventory::Implementation {
    EvaluationInventorySelection m_selection;
    AssetRevisionRef m_capture_product;
    std::string m_receipt_sha256;
    AssetDependencyContext m_context;
    bool m_manual_ancestor {};
    std::vector<std::byte> m_inventory_bytes, m_visibility_bytes;
};

AdmittedEvaluationInventory::AdmittedEvaluationInventory(
    std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const EvaluationInventorySelection& AdmittedEvaluationInventory::selection() const {
    return m_state->m_selection;
}

const AssetRevisionRef& AdmittedEvaluationInventory::capture_product() const {
    return m_state->m_capture_product;
}

const std::string& AdmittedEvaluationInventory::receipt_sha256() const {
    return m_state->m_receipt_sha256;
}

const AssetDependencyContext& AdmittedEvaluationInventory::context() const {
    return m_state->m_context;
}

bool AdmittedEvaluationInventory::manual_ancestor() const {
    return m_state->m_manual_ancestor;
}

Result<AdmittedEvaluationInventory>
inspect_evaluation_inventory(const EvaluationInventorySelection& selected,
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
                "Inventory uses a foreign capture/context",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_ITEMS = items(selected.m_items);
        const auto K_CELLS = cells(selected.m_cells);
        const auto& inventory_node = bind(selected.m_inventory, receipt);
        const auto& visibility_node = bind(selected.m_visibility, receipt);
        const auto K_SEPARATION = capture.validate_evaluation_separation(
            receipt, { selected.m_inventory, selected.m_visibility }, expected, stop);
        if (!K_SEPARATION) {
            return std::unexpected(K_SEPARATION.error());
        }
        require(inventory_node.m_dependencies ==
                    std::vector { capture.record().m_selection.m_product },
                "Inventory dependency differs");
        std::map<ViewKey, ObjectViewSelection> expected_views;
        std::map<AssetProductId, std::reference_wrapper<const AdmittedObjectObservation>>
            annotations;
        std::map<AssetProductId, AssetRevisionRef> parents {
            { selected.m_inventory.m_product, selected.m_inventory } };
        for (const auto& frame : capture.evaluation()) {
            check_stop(stop);
            const auto& selected_view = frame.m_inputs.m_view;
            expected_views.emplace(ViewKey { selected_view.m_capture, selected_view.m_frame },
                                   selected_view);
            parents.emplace(selected_view.m_image.m_product, selected_view.m_image);
            parents.emplace(selected_view.m_pose_asset.m_product, selected_view.m_pose_asset);
            for (const auto& annotation : frame.m_annotations) {
                const auto& ref = annotation.record().m_selection.m_product;
                annotations.emplace(ref.m_product, annotation);
                parents.emplace(ref.m_product, ref);
            }
        }
        require(!expected_views.empty() && expected_views.size() <= 64uz,
                "Evaluation view budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::vector<AssetRevisionRef> expected_parents;
        expected_parents.reserve(parents.size());
        for (const auto& [K_ID, K_REF] : parents) {
            expected_parents.push_back(K_REF);
        }
        auto actual_parents = visibility_node.m_dependencies;
        std::ranges::sort(actual_parents);
        std::ranges::sort(expected_parents);
        require(actual_parents == expected_parents, "Visibility dependency closure differs");
        const auto K_INVENTORY_BYTES =
            receipt.copy_blob(selected.m_inventory.m_content_sha256, 64uz * 1024uz, stop);
        const auto K_VISIBILITY_BYTES =
            receipt.copy_blob(selected.m_visibility.m_content_sha256, 256uz * 1024uz, stop);
        if (!K_INVENTORY_BYTES) {
            return std::unexpected(K_INVENTORY_BYTES.error());
        }
        if (!K_VISIBILITY_BYTES) {
            return std::unexpected(K_VISIBILITY_BYTES.error());
        }
        const auto K_INVENTORY = parse(*K_INVENTORY_BYTES, 64uz * 1024uz, stop);
        const auto K_VISIBILITY = parse(*K_VISIBILITY_BYTES, 256uz * 1024uz, stop);
        keys(K_INVENTORY,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "capture",
              "items",
              "manual_ancestor" });
        keys(K_VISIBILITY,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "capture",
              "inventory",
              "views",
              "cells",
              "manual_ancestor" });
        header(K_INVENTORY, inventory_node, capture, expected);
        header(K_VISIBILITY, visibility_node, capture, expected);
        require(reference(K_VISIBILITY.at("inventory")) == selected.m_inventory,
                "Visibility inventory differs",
                ErrorCode::RESYNC_REQUIRED);
        array(K_INVENTORY.at("items"), 64uz);
        std::vector<EvaluationInventoryItem> parsed_items;
        for (const auto& row : K_INVENTORY.at("items")) {
            check_stop(stop);
            keys(row, { "track_id", "kind", "label", "required" });
            require(row.at("required").is_boolean(), "Invalid inventory required flag");
            parsed_items.push_back({ { integer(row.at("track_id")) },
                                    named(row.at("kind"), K_KINDS),
                                    row.at("label").get<std::string>(),
                                    row.at("required").get<bool>() });
        }
        require(items(parsed_items) == K_ITEMS,
                "Inventory differs from independent selection",
                ErrorCode::RESYNC_REQUIRED);
        array(K_VISIBILITY.at("views"), 64uz);
        std::map<ViewKey, ObjectViewSelection> parsed_views;
        for (const auto& row : K_VISIBILITY.at("views")) {
            check_stop(stop);
            auto parsed = view(row);
            require(parsed_views.emplace(ViewKey { parsed.m_capture, parsed.m_frame }, parsed).second,
                    "Duplicate visibility view");
        }
        require(parsed_views == expected_views,
                "Visibility includes omitted/stale/inference views",
                ErrorCode::RESYNC_REQUIRED);
        array(K_VISIBILITY.at("cells"), 4096uz);
        std::vector<EvaluationVisibilityCell> parsed_cells;
        for (const auto& row : K_VISIBILITY.at("cells")) {
            check_stop(stop);
            keys(row, { "track_id", "capture_id", "frame_id", "state", "annotation" });
            parsed_cells.push_back({ { integer(row.at("track_id")) },
                                    { integer(row.at("capture_id")) },
                                    { integer(row.at("frame_id")) },
                                    named(row.at("state"), K_STATES),
                                    row.at("annotation").is_null()
                                        ? std::nullopt
                                        : std::optional { reference(row.at("annotation")) } });
        }
        require(cells(parsed_cells) == K_CELLS &&
                    K_CELLS.size() == K_ITEMS.size() * expected_views.size(),
                "Visibility differs from complete independently selected table",
                ErrorCode::RESYNC_REQUIRED);
        std::set<AssetProductId> consumed;
        for (const auto& [K_KEY, K_CELL] : K_CELLS) {
            check_stop(stop);
            const auto K_ITEM = K_ITEMS.find(K_CELL.m_track);
            const auto K_VIEW = expected_views.find({ K_CELL.m_capture, K_CELL.m_frame });
            require(K_ITEM != K_ITEMS.end() && K_VIEW != expected_views.end(),
                    "Visibility contains foreign inventory/view");
            const bool K_ANNOTATED { K_CELL.m_state == EvaluationVisibility::VISIBLE ||
                                    K_CELL.m_state == EvaluationVisibility::FULLY_OCCLUDED };
            require(K_CELL.m_annotation.has_value() == K_ANNOTATED,
                    "Visibility state/annotation presence differs");
            if (!K_CELL.m_annotation) {
                continue;
            }
            const auto& ref = K_CELL.m_annotation.value();
            const auto K_FOUND = annotations.find(ref.m_product);
            require(K_FOUND != annotations.end(),
                    "Visibility annotation is not selected for evaluation",
                    ErrorCode::RESYNC_REQUIRED);
            const auto& annotation = K_FOUND->second.get();
            const auto& record = annotation.record();
            require(record.m_selection.m_product == ref &&
                        record.m_selection.m_view == K_VIEW->second &&
                        consumed.insert(ref.m_product).second,
                    "Visibility annotation view/ref reused or stale",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_ACCEPTED = std::ranges::count(record.m_associations,
                                                       ObjectAssociationDecision::ACCEPTED,
                                                       &ObjectTrackAssociation::m_decision);
            require(K_ACCEPTED == 1 &&
                        std::ranges::any_of(record.m_associations,
                                            [&](const auto& association) {
                                                return association.m_track == K_CELL.m_track &&
                                                       association.m_decision ==
                                                           ObjectAssociationDecision::ACCEPTED;
                                            }),
                    "Visibility annotation track is ambiguous or differs");
            require(std::ranges::any_of(record.m_classes,
                                        [&](const auto& hypothesis) {
                                            return hypothesis.m_label == K_ITEM->second.m_label;
                                        }),
                    "Visibility annotation label differs");
            require(K_CELL.m_state == EvaluationVisibility::VISIBLE
                        ? annotation.coverage()[0uz] > 0
                        : annotation.coverage()[0uz] == 0 && annotation.coverage()[1uz] > 0,
                    "Visibility state disagrees with selected mask counts");
        }
        require(consumed.size() == annotations.size(),
                "Visibility omits selected evaluation annotations");
        EvaluationInventorySelection owned { selected.m_inventory, selected.m_visibility, {}, {} };
        for (const auto& [K_ID, K_ITEM] : K_ITEMS) {
            owned.m_items.push_back(K_ITEM);
        }
        for (const auto& [K_KEY, K_CELL] : K_CELLS) {
            owned.m_cells.push_back(K_CELL);
        }
        check_stop(stop);
        return AdmittedEvaluationInventory(
            std::make_shared<const AdmittedEvaluationInventory::Implementation>(
                AdmittedEvaluationInventory::Implementation { std::move(owned),
                                                             capture.record().m_selection.m_product,
                                                             receipt.receipt_sha256(),
                                                             expected,
                                                             inventory_node.m_manual_ancestor ||
                                                                 visibility_node.m_manual_ancestor,
                                                             *K_INVENTORY_BYTES,
                                                             *K_VISIBILITY_BYTES }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Evaluation inventory allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit>
publish_evaluation_inventory(const AdmittedEvaluationInventory& inventory,
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
    if (inventory.receipt_sha256() != receipt.receipt_sha256() ||
        inventory.context().m_world_epoch != expected.m_world_epoch ||
        inventory.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Evaluation inventory publication context differs");
    }
    try {
        return publish_immutable_records(
            directory,
            operation,
            base_generation,
            { { record_key(inventory.selection().m_inventory, "inventory"),
              inventory.m_state->m_inventory_bytes },
             { record_key(inventory.selection().m_visibility, "visibility"),
              inventory.m_state->m_visibility_bytes } },
            { receipt.source_directory() },
            stop,
            observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Evaluation inventory publication allocation failed");
    }
}

Result<AdmittedEvaluationInventory>
read_evaluation_inventory(const std::filesystem::path& directory,
                          const RecordCommit& commit,
                          const EvaluationInventorySelection& selected,
                          const AdmittedCaptureSelection& capture,
                          const AdmittedAssetReceipt& receipt,
                          const AssetDependencyContext& expected,
                          const std::stop_token& stop) {
    try {
        digest_text(commit.m_manifest_sha256);
        digest_text(selected.m_inventory.m_content_sha256);
        digest_text(selected.m_visibility.m_content_sha256);
        const auto K_SNAPSHOT = read_immutable_records(directory, stop);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        require(commit.m_generation && K_SNAPSHOT->m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT->m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Evaluation inventory registry commit differs",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& entry : { std::pair { selected.m_inventory, "inventory" },
                                  std::pair { selected.m_visibility, "visibility" } }) {
            const auto K_FOUND = K_SNAPSHOT->m_records.find(record_key(entry.first, entry.second));
            require(K_FOUND != K_SNAPSHOT->m_records.end() &&
                        sha256(K_FOUND->second) == entry.first.m_content_sha256,
                    "Selected evaluation record is not committed",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return inspect_evaluation_inventory(selected, capture, receipt, expected, stop);
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Evaluation inventory read allocation failed");
    }
}

}  // namespace world_explorer
