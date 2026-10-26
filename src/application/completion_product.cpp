#include "world_explorer/application/completion_product.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <map>
#include <set>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;
constexpr auto K_ROOT_BYTES = 1024uz * 1024uz;
constexpr auto K_BATCH_BYTES = 8uz * K_ROOT_BYTES;
constexpr std::array K_METHODS { "INFERRED", "GENERATED", "LIBRARY" };
constexpr std::array K_SUBJECTS { "OBJECT_GEOMETRY", "SHELL_GEOMETRY" };
constexpr std::array K_CLASSES { "UNKNOWN", "OBSERVED", "INFERRED", "GENERATED", "MANUALLY_EDITED" };
constexpr std::array K_REJECTIONS {
    "NONE", "ANCHOR_CHECK_FAILED", "REGION_CHECK_FAILED", "DECLARED_RANKED_ALTERNATIVE" };

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return *result;
}

template <class Enum, std::size_t N>
const char* name(Enum value, const std::array<const char*, N>& names) {
    const auto K_INDEX = static_cast<std::size_t>(value);
    require(K_INDEX < N, "Unsupported completion selection enum", ErrorCode::UNSUPPORTED_OPERATION);
    return names[K_INDEX];
}

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<const char*, N>& names) {
    const auto K_NAME = value.get<std::string>();
    for (std::size_t index { 0uz }; index < N; ++index) {
        if (K_NAME == names[index]) {
            return static_cast<Enum>(index);
        }
    }
    throw RecordFailure(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported completion metadata enum");
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

std::uint32_t index(const Json& value, std::size_t count) {
    const auto K_INDEX = integer(value);
    require(K_INDEX < count, "Completion index outside admitted geometry");
    return static_cast<std::uint32_t>(K_INDEX);
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    digest_text(ref.m_content_sha256);
    const auto K_FOUND =
        std::ranges::find(receipt.graph().m_products, ref, &AssetDependencyNode::m_product);
    require(ref.m_product.m_value && ref.m_revision && K_FOUND != receipt.graph().m_products.end(),
            "Completion product differs from selected receipt",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

using Parents = std::map<AssetProductId, AssetRevisionRef>;

void parents(const AssetDependencyNode& node, const Parents& expected) {
    std::vector<AssetRevisionRef> wanted;
    wanted.reserve(expected.size());
    for (const auto& [K_ID, K_REF] : expected) {
        wanted.push_back(K_REF);
    }
    auto actual = node.m_dependencies;
    std::ranges::sort(actual);
    std::ranges::sort(wanted);
    require(actual == wanted, "Completion dependency set differs");
}

void header(const Json& doc,
            const AssetDependencyNode& node,
            const AssetDependencyContext& expected) {
    require(integer(doc.at("schema_version")) == 1,
            "Unsupported completion schema",
            ErrorCode::UNSUPPORTED_OPERATION);
    require(integer(doc.at("world_epoch")) == expected.m_world_epoch.m_value &&
                integer(doc.at("pose_revision")) == expected.m_pose_revision.m_value &&
                digest(doc.at("recipe_sha256")) == node.m_recipe_sha256,
            "Completion context/recipe differs",
            ErrorCode::RESYNC_REQUIRED);
    require(doc.at("manual_ancestor").is_boolean() &&
                doc.at("manual_ancestor").get<bool>() == node.m_manual_ancestor,
            "Completion manual ancestry differs");
}

std::string key(const char* prefix, const AssetRevisionRef& ref) {
    return std::string(prefix) + std::to_string(ref.m_product.m_value) + "-" +
           std::to_string(ref.m_revision);
}

struct OwnedRecords {
    std::vector<ImmutableRecord> m_records;
    std::size_t m_bytes {};
    std::set<AssetRevisionRef> m_inputs;

    void add(std::string record_key, std::vector<std::byte> bytes) {
        require(bytes.size() <= K_BATCH_BYTES - m_bytes,
                "Completion owned byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        m_bytes += bytes.size();
        m_records.push_back({ std::move(record_key), std::move(bytes) });
    }

    std::vector<std::byte> input(const AssetRevisionRef& ref,
                                 const AdmittedAssetReceipt& receipt,
                                 std::size_t maximum,
                                 const std::stop_token& stop) {
        (void)bind(ref, receipt);
        auto bytes = take(receipt.copy_blob(ref.m_content_sha256, maximum, stop));
        require(!bytes.empty(), "Empty completion method input");
        if (m_inputs.insert(ref).second) {
            add(key("completion-input-", ref), bytes);
        }
        return bytes;
    }
};

void method(const CompletionMethodSelection& selected,
            const AdmittedAssetReceipt& receipt,
            const AssetDependencyContext& expected,
            OwnedRecords& owned,
            const std::stop_token& stop) {
    (void)name(selected.m_method, K_METHODS);
    require((selected.m_method == CompletionMethod::GENERATED) == selected.m_prompt.has_value() &&
                (selected.m_method == CompletionMethod::LIBRARY) ==
                    selected.m_library.has_value() &&
                (selected.m_method != CompletionMethod::LIBRARY || selected.m_seed == 0),
            "Completion method/prompt/library/seed profile differs");
    const auto& node = bind(selected.m_configuration, receipt);
    auto bytes = owned.input(selected.m_configuration, receipt, 16uz * 1024uz, stop);
    const auto K_DOC = parse(bytes, 16uz * 1024uz, stop);
    keys(K_DOC,
         { "schema_version",
          "kind",
          "world_epoch",
          "pose_revision",
          "recipe_sha256",
          "method",
          "seed",
          "algorithm",
          "prompt",
          "library",
          "rights",
          "manual_ancestor" });
    header(K_DOC, node, expected);
    require(K_DOC.at("kind").get<std::string>() == "COMPLETION_CONFIGURATION" &&
                K_DOC.at("method").get<std::string>() == name(selected.m_method, K_METHODS) &&
                integer(K_DOC.at("seed")) == selected.m_seed &&
                reference(K_DOC.at("algorithm")) == selected.m_algorithm &&
                reference(K_DOC.at("rights")) == selected.m_rights,
            "Completion method pins differ",
            ErrorCode::RESYNC_REQUIRED);
    Parents dependencies;
    const auto K_PIN = [&](const AssetRevisionRef& ref) {
        require(ref.m_product != selected.m_configuration.m_product &&
                    dependencies.emplace(ref.m_product, ref).second,
                "Completion method roles alias a product");
    };
    K_PIN(selected.m_algorithm);
    K_PIN(selected.m_rights);
    (void)owned.input(selected.m_algorithm, receipt, 64uz * 1024uz, stop);
    // Actual selected rights bytes are retained, without interpreting them as approval.
    (void)owned.input(selected.m_rights, receipt, 64uz * 1024uz, stop);
    if (selected.m_prompt) {
        const auto& prompt = selected.m_prompt.value();
        require(reference(K_DOC.at("prompt")) == prompt,
                "Completion prompt pin differs",
                ErrorCode::RESYNC_REQUIRED);
        K_PIN(prompt);
        auto prompt_bytes = owned.input(prompt, receipt, 8uz * 1024uz, stop);
        const auto K_PROMPT = parse(prompt_bytes, 8uz * 1024uz, stop);
        keys(K_PROMPT, { "schema_version", "kind", "text" });
        const auto K_TEXT = K_PROMPT.at("text").get<std::string>();
        require(integer(K_PROMPT.at("schema_version")) == 1 &&
                    K_PROMPT.at("kind").get<std::string>() == "COMPLETION_PROMPT" &&
                    !K_TEXT.empty() && K_TEXT.size() <= 4096uz && K_TEXT.find('\0') == K_TEXT.npos,
                "Invalid bounded completion prompt");
    } else {
        require(K_DOC.at("prompt").is_null(), "Unexpected completion prompt");
    }
    if (selected.m_library) {
        const auto& library = selected.m_library.value();
        require(reference(K_DOC.at("library")) == library,
                "Completion library pin differs",
                ErrorCode::RESYNC_REQUIRED);
        K_PIN(library);
        (void)owned.input(library, receipt, K_ROOT_BYTES, stop);
    } else {
        require(K_DOC.at("library").is_null(), "Unexpected completion library asset");
    }
    parents(node, dependencies);
}

bool same_anchor(const GeometryAnchor& first, const GeometryAnchor& second) {
    return first.m_triangle == second.m_triangle && first.m_weights == second.m_weights;
}

bool unchanged(const CompletionProtection& mapping,
               const AdmittedGeometryProduct& source,
               const AdmittedGeometryProduct& result) {
    const auto& original = source.support()[mapping.m_source_face];
    const auto& target = result.support()[mapping.m_result_face];
    if (target.m_class != AssetEvidenceClass::OBSERVED || !original.m_anchors ||
        !target.m_anchors ||
        source.mesh().m_groups[mapping.m_source_face] !=
            result.mesh().m_groups[mapping.m_result_face] ||
        original.m_uncertainty_units != target.m_uncertainty_units) {
        return false;
    }
    const std::set<AssetSourceViewId> K_ORIGINAL_VIEWS(original.m_views.begin(),
                                                       original.m_views.end()),
        K_TARGET_VIEWS(target.m_views.begin(), target.m_views.end());
    if (K_ORIGINAL_VIEWS != K_TARGET_VIEWS) {
        return false;
    }
    for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
        const auto K_TARGET_CORNER = (corner + mapping.m_rotation) % 3uz;
        const auto K_ORIGINAL_VERTEX = source.mesh().m_triangles[mapping.m_source_face][corner],
                   K_TARGET_VERTEX =
                       result.mesh().m_triangles[mapping.m_result_face][K_TARGET_CORNER];
        if (source.mesh().m_positions[K_ORIGINAL_VERTEX] !=
                result.mesh().m_positions[K_TARGET_VERTEX] ||
            source.mesh().m_normals[K_ORIGINAL_VERTEX] !=
                result.mesh().m_normals[K_TARGET_VERTEX] ||
            !same_anchor(original.m_anchors.value()[corner],
                         target.m_anchors.value()[K_TARGET_CORNER])) {
            return false;
        }
    }
    return true;
}

struct AggregateBudget {
    std::size_t m_protected {}, m_region_faces {};
};

std::vector<StructureSurfaceId> surface_ids(const Json& rows) {
    array(rows, 32uz);
    std::vector<StructureSurfaceId> result;
    std::set<StructureSurfaceId> seen;
    for (const auto& row : rows) {
        const StructureSurfaceId K_ID { integer(row) };
        require(K_ID.m_value && seen.insert(K_ID).second, "Zero/duplicate completion surface ID");
        result.push_back(K_ID);
    }
    std::ranges::sort(result);
    return result;
}

CompletionCandidate candidate(const Json& doc,
                              const CompletionCandidateSelection& selected,
                              const AdmittedGeometryProduct& source,
                              const AdmittedGeometryProduct& result,
                              const CompletionProductSelection& product,
                              AggregateBudget& budget,
                              const std::stop_token& stop) {
    keys(
        doc,
        { "id", "rank", "geometry", "configuration", "rejection", "protected", "regions", "checks" });
    CompletionCandidate value { selected, 0, CompletionRejection::NONE, {}, {}, { true, true, true } };
    const auto K_RANK = integer(doc.at("rank"));
    require(integer(doc.at("id")) == selected.m_id &&
                reference(doc.at("geometry")) == selected.m_geometry &&
                reference(doc.at("configuration")) == selected.m_method.m_configuration && K_RANK &&
                K_RANK <= product.m_candidates.size(),
            "Completion candidate independent pins/rank differ",
            ErrorCode::RESYNC_REQUIRED);
    value.m_rank = static_cast<std::uint32_t>(K_RANK);
    value.m_rejection = named<CompletionRejection>(doc.at("rejection"), K_REJECTIONS);
    array(doc.at("protected"), 8192uz);
    require(doc.at("protected").size() <= 8192uz - budget.m_protected,
            "Completion protection budget exceeded",
            ErrorCode::RESOURCE_EXHAUSTED);
    budget.m_protected += doc.at("protected").size();
    std::set<std::uint32_t> original_faces, target_faces;
    std::vector<unsigned int> partition(result.mesh().m_triangles.size(), 0U);
    for (const auto& row : doc.at("protected")) {
        check_stop(stop);
        keys(row, { "source_face", "result_face", "rotation" });
        CompletionProtection mapping {
            index(row.at("source_face"), source.mesh().m_triangles.size()),
            index(row.at("result_face"), result.mesh().m_triangles.size()),
            index(row.at("rotation"), 3uz) };
        require(source.support()[mapping.m_source_face].m_class == AssetEvidenceClass::OBSERVED &&
                    original_faces.insert(mapping.m_source_face).second &&
                    target_faces.insert(mapping.m_result_face).second,
                "Completion protection repeats or selects nonobserved source faces");
        value.m_checks.m_observed_anchors_preserved =
            unchanged(mapping, source, result) && value.m_checks.m_observed_anchors_preserved;
        ++partition[mapping.m_result_face];
        value.m_protected.push_back(mapping);
    }
    value.m_checks.m_observed_anchors_preserved = original_faces.size() == source.coverage()[1uz] &&
                                                  value.m_checks.m_observed_anchors_preserved;
    // Every observed output face must be one of these original protected faces.
    for (std::size_t face { 0uz }; face < result.support().size(); ++face) {
        if (result.support()[face].m_class == AssetEvidenceClass::OBSERVED &&
            !target_faces.contains(static_cast<std::uint32_t>(face))) {
            value.m_checks.m_observed_anchors_preserved = false;
        }
    }
    array(doc.at("regions"), 32uz);
    std::set<std::uint64_t> region_ids;
    for (const auto& row : doc.at("regions")) {
        check_stop(stop);
        keys(row, { "id", "faces", "class", "confidence", "surfaces", "bounds" });
        CompletionRegion region {};
        region.m_id = integer(row.at("id"));
        require(region.m_id && region_ids.insert(region.m_id).second,
                "Zero/duplicate completion region ID");
        region.m_class = named<AssetEvidenceClass>(row.at("class"), K_CLASSES);
        require(region.m_class != AssetEvidenceClass::OBSERVED,
                "Completion regions cannot create observed support");
        require(selected.m_method.m_method != CompletionMethod::INFERRED ||
                    region.m_class == AssetEvidenceClass::INFERRED ||
                    region.m_class == AssetEvidenceClass::UNKNOWN,
                "Inferred completion hides generated/edited support");
        require(selected.m_method.m_method != CompletionMethod::LIBRARY ||
                    region.m_class == AssetEvidenceClass::GENERATED ||
                    region.m_class == AssetEvidenceClass::UNKNOWN ||
                    region.m_class == AssetEvidenceClass::MANUALLY_EDITED,
                "Library completion cannot claim inferred source correspondence");
        if (region.m_class == AssetEvidenceClass::UNKNOWN) {
            require(row.at("confidence").is_null(),
                    "Unknown completion confidence must remain absent");
        } else {
            const auto K_CONFIDENCE = number(row.at("confidence"));
            require(K_CONFIDENCE >= 0 && K_CONFIDENCE <= 1, "Invalid completion confidence");
            region.m_confidence = K_CONFIDENCE;
        }
        region.m_surfaces = surface_ids(row.at("surfaces"));
        if (product.m_subject == CompletionSubject::OBJECT_GEOMETRY) {
            require(region.m_surfaces.empty(), "Object geometry cannot self-select shell surfaces");
        } else {
            require(!region.m_surfaces.empty() &&
                        std::ranges::all_of(region.m_surfaces,
                                            [&](auto id) {
                                                return std::ranges::find(product.m_surfaces, id) !=
                                                       product.m_surfaces.end();
                                            }),
                    "Completion region surface differs from independently selected shell");
        }
        array(row.at("faces"), 8192uz);
        require(!row.at("faces").empty() &&
                    row.at("faces").size() <= 32768uz - budget.m_region_faces,
                "Completion region face budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        budget.m_region_faces += row.at("faces").size();
        std::set<std::uint32_t> seen;
        bool first = true;
        for (const auto& item : row.at("faces")) {
            const auto K_FACE = index(item, result.mesh().m_triangles.size());
            require(seen.insert(K_FACE).second &&
                        result.support()[K_FACE].m_class == region.m_class,
                    "Completion region face class differs or repeats");
            ++partition[K_FACE];
            region.m_faces.push_back(K_FACE);
            for (auto vertex : result.mesh().m_triangles[K_FACE]) {
                const auto K_POINT = result.mesh().m_positions[vertex];
                require(std::max({ std::abs(K_POINT.m_x),
                                  std::abs(K_POINT.m_y),
                                  std::abs(K_POINT.m_z) }) <= 1e6,
                        "Completion coordinate budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                if (first) {
                    region.m_bounds = { K_POINT, K_POINT };
                    first = false;
                }
                region.m_bounds.m_minimum = { std::min(region.m_bounds.m_minimum.m_x, K_POINT.m_x),
                                             std::min(region.m_bounds.m_minimum.m_y, K_POINT.m_y),
                                             std::min(region.m_bounds.m_minimum.m_z, K_POINT.m_z) };
                region.m_bounds.m_maximum = { std::max(region.m_bounds.m_maximum.m_x, K_POINT.m_x),
                                             std::max(region.m_bounds.m_maximum.m_y, K_POINT.m_y),
                                             std::max(region.m_bounds.m_maximum.m_z, K_POINT.m_z) };
            }
        }
        const auto& bounds = row.at("bounds");
        keys(bounds, { "minimum", "maximum" });
        require(vector3(bounds.at("minimum")) == region.m_bounds.m_minimum &&
                    vector3(bounds.at("maximum")) == region.m_bounds.m_maximum,
                "Completion declared region bounds differ from actual geometry");
        const auto K_EXTENT = region.m_bounds.m_maximum - region.m_bounds.m_minimum;
        value.m_checks.m_region_extent_within_policy =
            std::max({ K_EXTENT.m_x, K_EXTENT.m_y, K_EXTENT.m_z }) <=
                product.m_max_region_extent_units &&
            value.m_checks.m_region_extent_within_policy;
        std::ranges::sort(region.m_faces);
        value.m_regions.push_back(std::move(region));
    }
    value.m_checks.m_complete_region_partition = std::ranges::all_of(partition, [](auto count) {
        return count == 1U;
    });
    const auto& checks = doc.at("checks");
    keys(
        checks,
        { "observed_anchors_preserved", "complete_region_partition", "region_extent_within_policy" });
    const auto K_CHECK = [&](const char* field, bool actual) {
        require(checks.at(field).is_boolean() && checks.at(field).get<bool>() == actual,
                "Completion declared consistency differs from recomputed result");
    };
    K_CHECK("observed_anchors_preserved", value.m_checks.m_observed_anchors_preserved);
    K_CHECK("complete_region_partition", value.m_checks.m_complete_region_partition);
    K_CHECK("region_extent_within_policy", value.m_checks.m_region_extent_within_policy);
    const bool K_VALID = value.m_checks.m_observed_anchors_preserved &&
                         value.m_checks.m_complete_region_partition &&
                         value.m_checks.m_region_extent_within_policy;
    if (selected.m_id == product.m_selected_candidate) {
        require(!value.m_regions.empty() && K_VALID && value.m_rank == 1 &&
                    value.m_rejection == CompletionRejection::NONE,
                "Selected completion failed consistency/rank/rejection checks");
    } else {
        const auto K_EXPECTED = !value.m_checks.m_observed_anchors_preserved
                                    ? CompletionRejection::ANCHOR_CHECK_FAILED
                                : !value.m_checks.m_complete_region_partition ||
                                        !value.m_checks.m_region_extent_within_policy
                                    ? CompletionRejection::REGION_CHECK_FAILED
                                    : CompletionRejection::DECLARED_RANKED_ALTERNATIVE;
        require(value.m_rank > 1 && value.m_rejection == K_EXPECTED,
                "Rejected completion reason differs from actual consistency");
    }
    std::ranges::sort(value.m_protected, {}, &CompletionProtection::m_source_face);
    std::ranges::sort(value.m_regions, {}, &CompletionRegion::m_id);
    return value;
}

}  // namespace

struct AdmittedCompletionProduct::Implementation {
    CompletionProductSelection m_selection;
    std::vector<CompletionCandidate> m_candidates;
    AdmittedGeometryProduct m_selected;
    AssetRevisionRef m_source, m_structure;
    std::string m_receipt;
    AssetDependencyContext m_context;
    bool m_synthetic {}, m_manual {};
    std::vector<ImmutableRecord> m_records;
};

AdmittedCompletionProduct::AdmittedCompletionProduct(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const CompletionProductSelection& AdmittedCompletionProduct::selection() const {
    return m_state->m_selection;
}

const std::vector<CompletionCandidate>& AdmittedCompletionProduct::candidates() const {
    return m_state->m_candidates;
}

const AdmittedGeometryProduct& AdmittedCompletionProduct::selected_geometry() const {
    return m_state->m_selected;
}

const AssetRevisionRef& AdmittedCompletionProduct::source_geometry() const {
    return m_state->m_source;
}

const AssetRevisionRef& AdmittedCompletionProduct::structure_product() const {
    return m_state->m_structure;
}

const std::string& AdmittedCompletionProduct::receipt_sha256() const {
    return m_state->m_receipt;
}

const AssetDependencyContext& AdmittedCompletionProduct::context() const {
    return m_state->m_context;
}

bool AdmittedCompletionProduct::synthetic_source() const {
    return m_state->m_synthetic;
}

bool AdmittedCompletionProduct::manual_ancestor() const {
    return m_state->m_manual;
}

Result<AdmittedCompletionProduct>
inspect_completion_product(const CompletionProductRequest& request, const std::stop_token& stop) {
    try {
        check_stop(stop);
        if (!request.m_selection || !request.m_source || !request.m_structure ||
            !request.m_capture || !request.m_receipt) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Independent completion selection, source geometry, structure, capture and "
                        "receipt are required");
        }
        const auto& selected = request.m_selection.value();
        const auto& source = request.m_source.value();
        const auto& structure = request.m_structure.value();
        const auto& capture = request.m_capture.value();
        const auto& receipt = request.m_receipt.value();
        const auto& expected = request.m_expected;
        (void)name(selected.m_subject, K_SUBJECTS);
        require(!selected.m_candidates.empty() && selected.m_candidates.size() <= 8uz &&
                    request.m_candidates.size() == selected.m_candidates.size(),
                "Completion independent candidate budget differs",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(std::isfinite(selected.m_max_region_extent_units) &&
                    selected.m_max_region_extent_units >= 0 &&
                    selected.m_max_region_extent_units <= 1e6,
                "Invalid independent completion extent policy",
                ErrorCode::INVALID_ARGUMENT);
        (void)take(validate_asset_dependencies(receipt.graph(), expected, stop));
        require(source.receipt_sha256() == receipt.receipt_sha256() &&
                    structure.receipt_sha256() == receipt.receipt_sha256() &&
                    capture.receipt_sha256() == receipt.receipt_sha256() &&
                    source.context().m_world_epoch == expected.m_world_epoch &&
                    source.context().m_pose_revision == expected.m_pose_revision &&
                    structure.context().m_world_epoch == expected.m_world_epoch &&
                    structure.context().m_pose_revision == expected.m_pose_revision &&
                    capture.record().m_context.m_world_epoch == expected.m_world_epoch &&
                    capture.record().m_context.m_pose_revision == expected.m_pose_revision &&
                    source.capture_product() == capture.record().m_selection.m_product &&
                    structure.geometry_product() == source.selection().m_product,
                "Completion source/structure/capture/receipt differs",
                ErrorCode::RESYNC_REQUIRED);
        (void)take(
            capture.validate_inference_dependency(receipt, selected.m_product, expected, stop));
        auto surfaces = selected.m_surfaces;
        std::ranges::sort(surfaces);
        require(surfaces.size() <= 32uz &&
                    std::adjacent_find(surfaces.begin(), surfaces.end()) == surfaces.end(),
                "Completion surface selection budget/uniqueness differs");
        require(selected.m_subject == CompletionSubject::SHELL_GEOMETRY ? !surfaces.empty()
                                                                        : surfaces.empty(),
                "Completion subject/surface profile differs");
        for (auto id : surfaces) {
            require(std::ranges::find(structure.surfaces(), id, &StructureSurface::m_id) !=
                        structure.surfaces().end(),
                    "Completion shell surface is not in selected structure",
                    ErrorCode::RESYNC_REQUIRED);
        }
        const auto& node = bind(selected.m_product, receipt);
        auto bytes =
            take(receipt.copy_blob(selected.m_product.m_content_sha256, K_ROOT_BYTES, stop));
        const auto K_DOC = parse(bytes, K_ROOT_BYTES, stop);
        keys(K_DOC,
             { "schema_version",
              "kind",
              "world_epoch",
              "pose_revision",
              "recipe_sha256",
              "source",
              "structure",
              "capture",
              "scope",
              "subject",
              "selected_candidate",
              "candidates",
              "manual_ancestor" });
        header(K_DOC, node, expected);
        require(K_DOC.at("kind").get<std::string>() == "COMPLETION_PRODUCT" &&
                    K_DOC.at("scope").get<std::string>() == "UNQUALIFIED_GEOMETRY_COMPLETION" &&
                    reference(K_DOC.at("source")) == source.selection().m_product &&
                    reference(K_DOC.at("structure")) == structure.selection().m_product &&
                    reference(K_DOC.at("capture")) == source.capture_product() &&
                    integer(K_DOC.at("selected_candidate")) == selected.m_selected_candidate,
                "Completion root profile/pins differ",
                ErrorCode::RESYNC_REQUIRED);
        const auto& subject = K_DOC.at("subject");
        keys(subject, { "kind", "surfaces" });
        require(subject.at("kind").get<std::string>() == name(selected.m_subject, K_SUBJECTS) &&
                    surface_ids(subject.at("surfaces")) == surfaces,
                "Completion independent subject differs",
                ErrorCode::RESYNC_REQUIRED);
        array(K_DOC.at("candidates"), 8uz);
        require(K_DOC.at("candidates").size() == selected.m_candidates.size(),
                "Completion candidate table size differs");
        Parents dependencies {
            { source.selection().m_product.m_product, source.selection().m_product },
            { structure.selection().m_product.m_product, structure.selection().m_product } };
        OwnedRecords owned;
        owned.add(key("completion-", selected.m_product), std::move(bytes));
        AggregateBudget budget;
        std::vector<CompletionCandidate> candidates;
        std::set<std::uint64_t> ids;
        std::set<std::uint32_t> ranks;
        std::set<AssetRevisionRef> geometries;
        std::optional<AdmittedGeometryProduct> selected_geometry;
        for (const auto& pin : selected.m_candidates) {
            check_stop(stop);
            require(pin.m_id && ids.insert(pin.m_id).second &&
                        geometries.insert(pin.m_geometry).second &&
                        pin.m_geometry != source.selection().m_product,
                    "Completion candidate ID/geometry duplicated or aliases source");
            const auto K_GEOMETRY =
                std::ranges::find_if(request.m_candidates, [&](const auto& value) {
                    return value.selection().m_product == pin.m_geometry;
                });
            require(K_GEOMETRY != request.m_candidates.end(),
                    "Completion candidate handle differs",
                    ErrorCode::RESYNC_REQUIRED);
            require(K_GEOMETRY->receipt_sha256() == receipt.receipt_sha256() &&
                        K_GEOMETRY->context().m_world_epoch == expected.m_world_epoch &&
                        K_GEOMETRY->context().m_pose_revision == expected.m_pose_revision &&
                        K_GEOMETRY->capture_product() == source.capture_product() &&
                        K_GEOMETRY->selection().m_native == source.selection().m_native &&
                        K_GEOMETRY->selection().m_source == source.selection().m_source &&
                        K_GEOMETRY->selection().m_views == source.selection().m_views,
                    "Completion candidate native source/receipt differs",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_ROW = std::ranges::find_if(K_DOC.at("candidates"), [&](const auto& row) {
                return integer(row.at("id")) == pin.m_id;
            });
            require(K_ROW != K_DOC.at("candidates").end(),
                    "Completion independent candidate ID differs",
                    ErrorCode::RESYNC_REQUIRED);
            method(pin.m_method, receipt, expected, owned, stop);
            auto parsed = candidate(*K_ROW, pin, source, *K_GEOMETRY, selected, budget, stop);
            require(ranks.insert(parsed.m_rank).second, "Completion candidate ranks repeat");
            if (pin.m_id == selected.m_selected_candidate) {
                selected_geometry = *K_GEOMETRY;
            }
            candidates.push_back(std::move(parsed));
            dependencies.emplace(pin.m_geometry.m_product, pin.m_geometry);
            dependencies.emplace(pin.m_method.m_configuration.m_product,
                                 pin.m_method.m_configuration);
        }
        if (!selected_geometry) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Completion selected candidate is unavailable");
        }
        parents(node, dependencies);
        auto normalized = selected;
        normalized.m_surfaces = std::move(surfaces);
        std::ranges::sort(normalized.m_candidates, {}, &CompletionCandidateSelection::m_id);
        std::ranges::sort(candidates, {}, &CompletionCandidate::m_rank);
        check_stop(stop);
        return AdmittedCompletionProduct(
            std::make_shared<const AdmittedCompletionProduct::Implementation>(
                AdmittedCompletionProduct::Implementation { std::move(normalized),
                                                           std::move(candidates),
                                                           selected_geometry.value(),
                                                           source.selection().m_product,
                                                           structure.selection().m_product,
                                                           receipt.receipt_sha256(),
                                                           expected,
                                                           source.synthetic_source(),
                                                           node.m_manual_ancestor,
                                                           std::move(owned.m_records) }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Completion allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_completion_product(const AdmittedCompletionProduct& completion,
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
    if (completion.receipt_sha256() != receipt.receipt_sha256() ||
        completion.context().m_world_epoch != expected.m_world_epoch ||
        completion.context().m_pose_revision != expected.m_pose_revision) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Completion publication context differs");
    }
    try {
        return publish_immutable_records(directory,
                                         operation,
                                         base_generation,
                                         completion.m_state->m_records,
                                         { receipt.source_directory() },
                                         stop,
                                         observed);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Completion publication allocation failed");
    }
}

Result<AdmittedCompletionProduct> read_completion_product(const std::filesystem::path& directory,
                                                          const RecordCommit& commit,
                                                          const CompletionProductRequest& request,
                                                          const std::stop_token& stop) {
    try {
        check_stop(stop);
        digest_text(commit.m_manifest_sha256);
        auto completion = take(inspect_completion_product(request, stop));
        const auto K_SNAPSHOT = take(read_immutable_records(directory, stop));
        require(commit.m_generation && K_SNAPSHOT.m_commit.m_generation == commit.m_generation &&
                    K_SNAPSHOT.m_commit.m_manifest_sha256 == commit.m_manifest_sha256,
                "Completion selected commit differs",
                ErrorCode::RESYNC_REQUIRED);
        std::vector<AssetRevisionRef> pins { completion.selection().m_product };
        for (const auto& value : completion.selection().m_candidates) {
            const auto& method = value.m_method;
            pins.insert(pins.end(), { method.m_configuration, method.m_algorithm, method.m_rights });
            if (method.m_prompt) {
                pins.push_back(method.m_prompt.value());
            }
            if (method.m_library) {
                pins.push_back(method.m_library.value());
            }
        }
        for (const auto& ref : pins) {
            const auto K_KEY = key(
                ref == completion.selection().m_product ? "completion-" : "completion-input-", ref);
            const auto K_FOUND = K_SNAPSHOT.m_records.find(K_KEY);
            require(K_FOUND != K_SNAPSHOT.m_records.end() &&
                        sha256(K_FOUND->second) == ref.m_content_sha256,
                    "Selected completion method/root bytes are not committed",
                    ErrorCode::RESYNC_REQUIRED);
        }
        return completion;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Completion reload allocation failed");
    }
}

}  // namespace world_explorer
