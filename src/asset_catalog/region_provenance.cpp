#include "world_explorer/asset_catalog/region_provenance.hpp"

#include "recipe_json.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <new>

namespace world_explorer {
namespace {

using namespace asset_recipe_detail;
constexpr std::size_t K_RECORD_LIMIT { 4uz * 1024uz * 1024uz };
constexpr std::uint64_t K_ELEMENT_LIMIT { 16uz * 1024uz * 1024uz };

void selected_digest(const std::string& value) {
    require(value.size() == 64uz && value.find_first_not_of("0123456789abcdef") == value.npos,
            "Invalid selected region metadata digest",
            ErrorCode::INVALID_ARGUMENT);
}

void domain(const AssetRegionDomain& value) {
    require(value.m_elements > 0 && value.m_elements <= K_ELEMENT_LIMIT,
            "Region element budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    if (value.m_kind == AssetRegionKind::GEOMETRY_FACES) {
        require(value.m_width == 0 && value.m_height == 0, "Face domain has texture dimensions");
    } else {
        require(value.m_kind == AssetRegionKind::MATERIAL_TEXELS,
                "Unsupported region domain",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(value.m_width > 0 && value.m_width <= 32768 && value.m_height > 0 &&
                    value.m_height <= 32768,
                "Invalid texel domain dimensions");
        require(value.m_width <= K_ELEMENT_LIMIT / value.m_height,
                "Texel domain exceeds element budget",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(value.m_width * value.m_height == value.m_elements,
                "Texel domain element count differs from grid");
    }
}

AssetSourceViewId view_id(const Json& value) {
    keys(value, { "capture_id", "frame_id" });
    AssetSourceViewId result { { integer(value.at("capture_id")) }, { integer(value.at("frame_id")) } };
    require(result.m_capture.m_value && result.m_frame.m_value, "Invalid source view identity");
    return result;
}

AssetSourceViewRef source(const Json& value) {
    keys(value,
         { "view",
          "calibration_id",
          "image",
          "calibration_sha256",
          "pose_sha256",
          "image_grid_sha256" });
    return { view_id(value.at("view")),
            { integer(value.at("calibration_id")) },
            reference(value.at("image")),
            digest(value.at("calibration_sha256")),
            digest(value.at("pose_sha256")),
            digest(value.at("image_grid_sha256")) };
}

void reference_budget(const Json& values, std::size_t& total) {
    require(values.is_array(), "Region references must be arrays");
    require(values.size() <= 64uz && values.size() <= 65536uz - total,
            "Region reference budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    total += values.size();
}

}  // namespace

Result<AssetRegionInspection>
inspect_asset_region_provenance(const AdmittedAssetReceipt& receipt,
                                AssetProductId provenance_product,
                                const AssetRegionSelection& selection,
                                const AssetDependencyContext& expected,
                                const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        domain(selection.m_domain);
        require(selection.m_sources.size() <= 64uz,
                "Too many selected source views",
                ErrorCode::RESOURCE_EXHAUSTED);
        std::map<AssetProductId, std::reference_wrapper<const AssetDependencyNode>> nodes;
        for (const auto& node : receipt.graph().m_products) {
            nodes.emplace(node.m_product.m_product, node);
        }
        const auto K_NODE = [&](AssetProductId id) -> const AssetDependencyNode& {
            const auto K_FOUND = nodes.find(id);
            require(K_FOUND != nodes.end(), "Region product is not selected", ErrorCode::NOT_FOUND);
            return K_FOUND->second.get();
        };
        const auto& target = K_NODE(selection.m_target.m_product);
        require(target.m_product == selection.m_target,
                "Region target selection is stale",
                ErrorCode::RESYNC_REQUIRED);
        const auto& sidecar = K_NODE(provenance_product);
        require(sidecar.m_dependencies == std::vector { target.m_product } &&
                    !sidecar.m_manual_action_sha256.has_value(),
                "Provenance sidecar must depend only on its target without its own action");
        std::set<AssetProductId> ancestors;
        std::vector<AssetRevisionRef> pending = target.m_dependencies;
        while (!pending.empty()) {
            check_stop(stop);
            const auto K_REF = pending.back();
            pending.pop_back();
            if (ancestors.insert(K_REF.m_product).second) {
                const auto& parents = K_NODE(K_REF.m_product).m_dependencies;
                pending.insert(pending.end(), parents.begin(), parents.end());
            }
        }
        std::map<AssetSourceViewId, bool> source_manual;
        for (const auto& view : selection.m_sources) {
            check_stop(stop);
            require(view.m_view.m_capture.m_value && view.m_view.m_frame.m_value &&
                        view.m_calibration.m_value,
                    "Invalid selected source identifiers",
                    ErrorCode::INVALID_ARGUMENT);
            selected_digest(view.m_calibration_sha256);
            selected_digest(view.m_pose_sha256);
            selected_digest(view.m_image_grid_sha256);
            const auto& image = K_NODE(view.m_image.m_product);
            require(image.m_product == view.m_image,
                    "Selected source image revision is stale",
                    ErrorCode::RESYNC_REQUIRED);
            require(ancestors.contains(view.m_image.m_product),
                    "Source view is not a target ancestor");
            require(source_manual.emplace(view.m_view, image.m_manual_ancestor).second,
                    "Duplicate selected source view",
                    ErrorCode::INVALID_ARGUMENT);
        }
        auto selected_sources = selection.m_sources;
        std::ranges::sort(selected_sources);
        const auto K_BYTES =
            receipt.copy_blob(sidecar.m_product.m_content_sha256, K_RECORD_LIMIT, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_DOCUMENT = parse(*K_BYTES);
        keys(K_DOCUMENT,
             { "schema_version",
              "world_epoch",
              "pose_revision",
              "target",
              "recipe_sha256",
              "domain",
              "sources",
              "spans",
              "manual_ancestor" });
        require(integer(K_DOCUMENT.at("schema_version")) == 1,
                "Unsupported region provenance schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        require(integer(K_DOCUMENT.at("world_epoch")) == expected.m_world_epoch.m_value &&
                    integer(K_DOCUMENT.at("pose_revision")) == expected.m_pose_revision.m_value &&
                    reference(K_DOCUMENT.at("target")) == selection.m_target &&
                    digest(K_DOCUMENT.at("recipe_sha256")) == target.m_recipe_sha256,
                "Region context/target/recipe binding differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto& grid = K_DOCUMENT.at("domain");
        keys(grid, { "kind", "elements", "width", "height" });
        static const std::map<std::string, AssetRegionKind> K_KINDS {
            { "GEOMETRY_FACES", AssetRegionKind::GEOMETRY_FACES },
            { "MATERIAL_TEXELS", AssetRegionKind::MATERIAL_TEXELS } };
        AssetRegionDomain region_domain { named(grid.at("kind"), K_KINDS),
                                         integer(grid.at("elements")),
                                         integer(grid.at("width")),
                                         integer(grid.at("height")) };
        domain(region_domain);
        require(region_domain == selection.m_domain,
                "Region domain differs from independent selection",
                ErrorCode::RESYNC_REQUIRED);
        AssetRegionInspection result { { expected,
                                       selection.m_target,
                                       target.m_recipe_sha256,
                                       region_domain,
                                       {},
                                       {},
                                       K_DOCUMENT.at("manual_ancestor").get<bool>() },
                                      receipt.receipt_sha256(),
                                      K_GRAPH->m_graph_sha256,
                                      sidecar.m_product.m_content_sha256,
                                      {},
                                      0 };
        const auto& sources = K_DOCUMENT.at("sources");
        require(sources.is_array() && sources.size() <= 64uz,
                "Region source table exceeds budget",
                ErrorCode::RESOURCE_EXHAUSTED);
        for (const auto& view : sources) {
            check_stop(stop);
            result.m_record.m_sources.push_back(source(view));
        }
        std::ranges::sort(result.m_record.m_sources);
        require(result.m_record.m_sources == selected_sources,
                "Region source metadata differs from independent selection",
                ErrorCode::RESYNC_REQUIRED);
        const auto& spans = K_DOCUMENT.at("spans");
        require(spans.is_array() && !spans.empty(), "Missing region partition");
        require(
            spans.size() <= 16384uz, "Region span budget exceeded", ErrorCode::RESOURCE_EXHAUSTED);
        static const std::map<std::string, AssetEvidenceClass> K_CLASSES {
            { "UNKNOWN", AssetEvidenceClass::UNKNOWN },
            { "OBSERVED", AssetEvidenceClass::OBSERVED },
            { "INFERRED", AssetEvidenceClass::INFERRED },
            { "GENERATED", AssetEvidenceClass::GENERATED },
            { "MANUALLY_EDITED", AssetEvidenceClass::MANUALLY_EDITED } };
        std::uint64_t offset {};
        std::size_t references {};
        std::set<AssetSourceViewId> used_sources;
        bool manual_edit {};
        for (const auto& span : spans) {
            check_stop(stop);
            keys(
                span,
                { "first", "count", "class", "confidence", "parents", "support", "manual_ancestor" });
            AssetRegionSpan row { integer(span.at("first")),
                                 integer(span.at("count")),
                                 named(span.at("class"), K_CLASSES),
                                 {},
                                 {},
                                 {},
                                 span.at("manual_ancestor").get<bool>() };
            require(row.m_first == offset && row.m_count > 0 &&
                        row.m_count <= region_domain.m_elements - offset,
                    "Region partition has a gap, overlap or out-of-range span");
            offset += row.m_count;
            if (!span.at("confidence").is_null()) {
                require(span.at("confidence").is_number(),
                        "Region confidence must be numeric or null");
                const auto K_CONFIDENCE = span.at("confidence").get<double>();
                require(std::isfinite(K_CONFIDENCE) && K_CONFIDENCE >= 0 && K_CONFIDENCE <= 1,
                        "Invalid region confidence");
                row.m_confidence = K_CONFIDENCE;
            }
            const auto K_EDIT = row.m_class == AssetEvidenceClass::MANUALLY_EDITED;
            require(!K_EDIT || target.m_manual_action_sha256.has_value(),
                    "Manual region lacks a declared target action");
            bool manual = K_EDIT;
            reference_budget(span.at("parents"), references);
            std::set<AssetProductId> parent_ids;
            for (const auto& parent : span.at("parents")) {
                check_stop(stop);
                const auto K_REF = reference(parent);
                require(parent_ids.insert(K_REF.m_product).second, "Duplicate region parent");
                require(std::ranges::find(target.m_dependencies, K_REF) !=
                            target.m_dependencies.end(),
                        "Region parent is not an exact target dependency",
                        ErrorCode::RESYNC_REQUIRED);
                manual = manual || K_NODE(K_REF.m_product).m_manual_ancestor;
                row.m_parents.push_back(K_REF);
            }
            reference_budget(span.at("support"), references);
            std::set<AssetSourceViewId> supported;
            for (const auto& support : span.at("support")) {
                check_stop(stop);
                const auto K_ID = view_id(support);
                const auto K_FOUND = source_manual.find(K_ID);
                require(K_FOUND != source_manual.end(), "Region support is not a selected view");
                require(supported.insert(K_ID).second, "Duplicate region support view");
                manual = manual || K_FOUND->second;
                row.m_support.push_back(K_ID);
                used_sources.insert(K_ID);
            }
            require(row.m_class != AssetEvidenceClass::OBSERVED || !row.m_support.empty(),
                    "Observed region lacks source-view support");
            require(row.m_class != AssetEvidenceClass::UNKNOWN ||
                        (!row.m_confidence.has_value() && row.m_support.empty()),
                    "Unknown region claims confidence or observed support");
            require(row.m_manual_ancestor == manual,
                    "Region manual ancestry differs from declared lineage");
            manual_edit = manual_edit || K_EDIT;
            result.m_manual_ancestor_elements += manual ? row.m_count : 0;
            result.m_coverage.at(static_cast<std::size_t>(row.m_class)) += row.m_count;
            std::ranges::sort(row.m_parents);
            std::ranges::sort(row.m_support);
            result.m_record.m_spans.push_back(std::move(row));
        }
        require(offset == region_domain.m_elements, "Region partition omits trailing elements");
        require(used_sources.size() == source_manual.size(), "Unused region source view");
        require(manual_edit == target.m_manual_action_sha256.has_value() &&
                    result.m_record.m_manual_ancestor == target.m_manual_ancestor &&
                    result.m_record.m_manual_ancestor == (result.m_manual_ancestor_elements > 0),
                "Region/target manual ancestry disagrees");
        check_stop(stop);
        return result;
    } catch (const RecipeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Region provenance allocation failed");
    } catch (const Json::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
