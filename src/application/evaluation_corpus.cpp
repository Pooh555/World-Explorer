#include "world_explorer/application/evaluation_corpus.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace world_explorer {
namespace {

using namespace json_record_detail;

constexpr std::array K_EVIDENCE {
    std::pair { AssetCorpusEvidence::CAPTURE_ENVELOPE, "CAPTURE_ENVELOPE" },
    std::pair { AssetCorpusEvidence::INVENTORY, "INVENTORY" },
    std::pair { AssetCorpusEvidence::VISIBILITY, "VISIBILITY" },
    std::pair { AssetCorpusEvidence::INDEPENDENT_SCALE, "INDEPENDENT_SCALE" },
    std::pair { AssetCorpusEvidence::TEMPORAL_BLOCKS, "TEMPORAL_BLOCKS" },
    std::pair { AssetCorpusEvidence::INSPECTION_PATH, "INSPECTION_PATH" },
    std::pair { AssetCorpusEvidence::BACKSIDE, "BACKSIDE" },
    std::pair { AssetCorpusEvidence::EMPTY_BACKGROUND, "EMPTY_BACKGROUND" },
    std::pair { AssetCorpusEvidence::CHANGED_LIGHT, "CHANGED_LIGHT" },
    std::pair { AssetCorpusEvidence::EVALUATION_PROTOCOL, "EVALUATION_PROTOCOL" } };
constexpr std::array K_TRACKS { std::pair { AssetCorpusTrack::SYNTHETIC, "SYNTHETIC" },
                               std::pair { AssetCorpusTrack::PILOT, "PILOT" },
                               std::pair { AssetCorpusTrack::RELEASE, "RELEASE" } };
constexpr std::array K_RECIPE_BLOCKERS { "CONTRACTS_ONLY_HOST",
                                        "UNKNOWN_INSTALLATION_SIZE",
                                        "UNREVIEWED_LOCAL_RIGHTS",
                                        "RESTRICTED_LOCAL_RIGHTS",
                                        "DISTRIBUTION_NOT_CLEARED",
                                        "HOST_CAPACITY_EXCEEDED",
                                        "COMPONENT_BYTES_UNVERIFIED",
                                        "INSTALLED_FILES_UNVERIFIED",
                                        "REVIEW_NOT_SELECTED",
                                        "RUNTIME_CLOSURE_UNVERIFIED",
                                        "WORKER_LIMITS_UNVERIFIED" };
constexpr auto K_MAX_BYTES = 256uz * 1024uz;

template <class Enum, std::size_t N>
std::string name(Enum value, const std::array<std::pair<Enum, const char*>, N>& names) {
    for (const auto& entry : names) {
        if (entry.first == value) {
            return entry.second;
        }
    }
    throw RecordFailure(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported corpus enum");
}

template <class Enum, std::size_t N>
Enum named(const Json& value, const std::array<std::pair<Enum, const char*>, N>& names) {
    const auto K_TEXT = value.get<std::string>();
    for (const auto& entry : names) {
        if (K_TEXT == entry.second) {
            return entry.first;
        }
    }
    throw RecordFailure(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported corpus enum");
}

void token(const std::string& value) {
    require(!value.empty() && value.size() <= 64uz &&
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == value.npos,
            "Invalid corpus identity/label");
}

Json reference(const AssetRevisionRef& ref) {
    require(ref.m_product.m_value != 0 && ref.m_revision != 0, "Unselected corpus product");
    digest_text(ref.m_content_sha256);
    return { { "product_id", ref.m_product.m_value },
            { "revision", ref.m_revision },
            { "content_sha256", ref.m_content_sha256 } };
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

const AssetDependencyNode& bind(const AssetRevisionRef& ref, const AdmittedAssetReceipt& receipt) {
    (void)reference(ref);
    const auto K_FOUND = std::ranges::find(receipt.graph().m_products, ref, [](const auto& node) {
        return node.m_product;
    });
    require(K_FOUND != receipt.graph().m_products.end(),
            "Corpus product/revision differs from selected receipt",
            ErrorCode::RESYNC_REQUIRED);
    return *K_FOUND;
}

Json host(const AssetRecipeHost& value) {
    return { { "contracts_only", value.m_contracts_only },
            { "host_ram_bytes", value.m_host_ram_bytes },
            { "device_vram_bytes", value.m_device_vram_bytes },
            { "free_disk_bytes", value.m_free_disk_bytes },
            { "cpu_threads", value.m_cpu_threads },
            { "queued_jobs", value.m_queued_jobs } };
}

AssetRecipeHost host(const Json& value) {
    keys(value,
         { "contracts_only",
          "host_ram_bytes",
          "device_vram_bytes",
          "free_disk_bytes",
          "cpu_threads",
          "queued_jobs" });
    require(value.at("contracts_only").is_boolean(), "Invalid corpus host flag");
    return { value.at("contracts_only").get<bool>(),
            integer(value.at("host_ram_bytes")),
            integer(value.at("device_vram_bytes")),
            integer(value.at("free_disk_bytes")),
            integer(value.at("cpu_threads")),
            integer(value.at("queued_jobs")) };
}

std::string capture_key(const AssetCorpusCapture& value) {
    return "capture-" + std::to_string(value.m_selection.m_value) + "-" +
           std::to_string(value.m_revision);
}

void registry(const AssetCorpusCapture& pin,
              const AssetCorpusCaptureInput& source,
              const std::stop_token& stop) {
    digest_text(pin.m_commit.m_manifest_sha256);
    require(pin.m_commit.m_generation != 0, "Unselected capture registry commit");
    const auto K_SNAPSHOT = read_immutable_records(source.m_registry, stop);
    if (!K_SNAPSHOT) {
        throw RecordFailure(K_SNAPSHOT.error().m_code, K_SNAPSHOT.error().m_message);
    }
    require(K_SNAPSHOT->m_commit.m_generation == pin.m_commit.m_generation &&
                K_SNAPSHOT->m_commit.m_manifest_sha256 == pin.m_commit.m_manifest_sha256,
            "Capture registry commit differs from independent pin",
            ErrorCode::RESYNC_REQUIRED);
    const auto K_FOUND = K_SNAPSHOT->m_records.find(capture_key(pin));
    require(K_FOUND != K_SNAPSHOT->m_records.end() &&
                K_FOUND->second == source.m_selection.encoded_bytes(),
            "Selected capture is not committed in its registry",
            ErrorCode::RESYNC_REQUIRED);
}

std::vector<std::byte> encoded(const std::string& text) {
    const auto K_BYTES = std::as_bytes(std::span(text));
    return { K_BYTES.begin(), K_BYTES.end() };
}

}  // namespace

struct AdmittedAssetCorpus::Implementation {
    AssetCorpusDefinition m_definition;
    AssetCorpusRef m_reference;
    std::string m_canonical;
    std::vector<AssetCorpusBlocker> m_blockers;
    std::vector<AssetRecipeInspection> m_recipes;
    std::vector<AssetCorpusCaptureInput> m_captures;
    std::vector<AssetCorpusRecipeInput> m_recipe_inputs;
};

AdmittedAssetCorpus::AdmittedAssetCorpus(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const AssetCorpusDefinition& AdmittedAssetCorpus::definition() const {
    return m_state->m_definition;
}

const AssetCorpusRef& AdmittedAssetCorpus::reference() const {
    return m_state->m_reference;
}

const std::string& AdmittedAssetCorpus::canonical_json() const {
    return m_state->m_canonical;
}

const std::vector<AssetCorpusBlocker>& AdmittedAssetCorpus::blockers() const {
    return m_state->m_blockers;
}

const std::vector<AssetRecipeInspection>& AdmittedAssetCorpus::recipes() const {
    return m_state->m_recipes;
}

Result<AdmittedAssetCorpus> admit_asset_corpus(const AssetCorpusDefinition& definition,
                                               const std::vector<AssetCorpusCaptureInput>& captures,
                                               const std::vector<AssetCorpusRecipeInput>& recipes,
                                               const std::stop_token& stop) {
    try {
        check_stop(stop);
        token(definition.m_corpus);
        require(definition.m_revision != 0, "Unselected corpus revision");
        require(!definition.m_captures.empty() && definition.m_captures.size() <= 16uz &&
                    !definition.m_recipes.empty() && definition.m_recipes.size() <= 16uz,
                "Corpus capture/recipe count outside budget",
                ErrorCode::RESOURCE_EXHAUSTED);
        require(captures.size() == definition.m_captures.size() &&
                    recipes.size() == definition.m_recipes.size(),
                "Corpus independently selected input count differs",
                ErrorCode::RESYNC_REQUIRED);
        // Bound caller-owned strings/tables before copying a frozen definition.
        for (const auto& capture : definition.m_captures) {
            token(capture.m_label);
            (void)reference(capture.m_product);
            digest_text(capture.m_receipt_sha256);
            digest_text(capture.m_commit.m_manifest_sha256);
            require(capture.m_evidence.size() <= K_EVIDENCE.size(),
                    "Corpus evidence budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            for (const auto& [K_KIND, K_REF] : capture.m_evidence) {
                (void)name(K_KIND, K_EVIDENCE);
                (void)reference(K_REF);
            }
        }
        for (const auto& recipe : definition.m_recipes) {
            token(recipe.m_label);
            (void)reference(recipe.m_product);
            digest_text(recipe.m_receipt_sha256);
            digest_text(recipe.m_recipe_sha256);
        }
        for (const auto& capture : captures) {
            token(capture.m_label);
        }
        for (const auto& recipe : recipes) {
            token(recipe.m_label);
        }
        auto value = definition;
        std::ranges::sort(value.m_captures, {}, &AssetCorpusCapture::m_label);
        std::ranges::sort(value.m_recipes, {}, &AssetCorpusRecipe::m_label);
        std::vector<AssetCorpusBlocker> blockers;
        std::vector<AssetRecipeInspection> inspections;
        auto capture_rows = Json::array();
        auto recipe_rows = Json::array();
        std::set<std::string> labels, source_hashes;
        std::size_t frame_count { 0uz };
        for (const auto& pin : value.m_captures) {
            check_stop(stop);
            token(pin.m_label);
            require(labels.insert(pin.m_label).second, "Duplicate corpus capture label");
            const auto K_INPUT =
                std::ranges::find(captures, pin.m_label, &AssetCorpusCaptureInput::m_label);
            require(K_INPUT != captures.end() &&
                        std::ranges::count(
                            captures, pin.m_label, &AssetCorpusCaptureInput::m_label) == 1,
                    "Missing/duplicate independently selected capture",
                    ErrorCode::RESYNC_REQUIRED);
            const auto& selected = K_INPUT->m_selection.record().m_selection;
            digest_text(pin.m_receipt_sha256);
            require(pin.m_product == selected.m_product &&
                        pin.m_selection == selected.m_selection &&
                        pin.m_revision == selected.m_revision &&
                        pin.m_receipt_sha256 == K_INPUT->m_receipt.receipt_sha256() &&
                        pin.m_receipt_sha256 == K_INPUT->m_selection.receipt_sha256(),
                    "Corpus capture selection/receipt differs",
                    ErrorCode::RESYNC_REQUIRED);
            registry(pin, *K_INPUT, stop);
            frame_count += selected.m_frames.size();
            require(frame_count <= 256uz,
                    "Corpus frame budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            // One submission per full source; another label cannot hide a repeat.
            std::set<std::string> current_sources;
            for (const auto& source : selected.m_sources) {
                current_sources.insert(source.m_source_sha256);
            }
            for (const auto& source : current_sources) {
                require(source_hashes.insert(source).second,
                        "Source reused across corpus captures");
            }
            require(pin.m_evidence.size() <= K_EVIDENCE.size(),
                    "Corpus evidence budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            for (const auto& [K_KIND, K_REF] : pin.m_evidence) {
                (void)name(K_KIND, K_EVIDENCE);
                (void)bind(K_REF, K_INPUT->m_receipt);
            }
            auto evidence = Json::object();
            for (const auto& [K_KIND, K_NAME] : K_EVIDENCE) {
                const auto K_FOUND = pin.m_evidence.find(K_KIND);
                if (K_FOUND == pin.m_evidence.end()) {
                    evidence[K_NAME] = nullptr;
                    blockers.push_back({ pin.m_label, std::string(K_NAME) + "_MISSING" });
                } else {
                    evidence[K_NAME] = reference(K_FOUND->second);
                    if ((!value.m_require_typed_scale ||
                         K_KIND != AssetCorpusEvidence::INDEPENDENT_SCALE) &&
                        (!value.m_require_typed_inventory ||
                         (K_KIND != AssetCorpusEvidence::INVENTORY &&
                          K_KIND != AssetCorpusEvidence::VISIBILITY)) &&
                        (!value.m_require_typed_paths ||
                         (K_KIND != AssetCorpusEvidence::TEMPORAL_BLOCKS &&
                          K_KIND != AssetCorpusEvidence::INSPECTION_PATH))) {
                        blockers.push_back({ pin.m_label, std::string(K_NAME) + "_UNVERIFIED" });
                    }
                }
            }
            const auto K_SCALE = pin.m_evidence.find(AssetCorpusEvidence::INDEPENDENT_SCALE);
            require((K_SCALE == pin.m_evidence.end() && !selected.m_measurement) ||
                        (K_SCALE != pin.m_evidence.end() && selected.m_measurement &&
                         K_SCALE->second == *selected.m_measurement),
                    "Independent-scale reference differs from capture measurement",
                    ErrorCode::RESYNC_REQUIRED);
            if (value.m_require_typed_scale && K_SCALE != pin.m_evidence.end()) {
                require(K_INPUT->m_measurement.has_value(),
                        "Corpus requires independently admitted typed scale",
                        ErrorCode::RESYNC_REQUIRED);
                const auto& measurement = K_INPUT->m_measurement.value();
                require(measurement.selection().m_product == K_SCALE->second &&
                            measurement.capture_product() == selected.m_product &&
                            measurement.receipt_sha256() == pin.m_receipt_sha256,
                        "Corpus typed measurement/capture differs",
                        ErrorCode::RESYNC_REQUIRED);
                blockers.push_back({ pin.m_label, "MEASUREMENT_AUTHENTICITY_UNVERIFIED" });
                blockers.push_back({ pin.m_label, "GEOMETRY_CORRESPONDENCE_UNVERIFIED" });
                if (measurement.alignment().m_synthetic) {
                    blockers.push_back({ pin.m_label, "SYNTHETIC_SCALE_DIAGNOSTIC" });
                }
            } else {
                require(!K_INPUT->m_measurement,
                        "Typed measurement requires schema two and a selected scale ref");
            }
            const auto K_INVENTORY = pin.m_evidence.find(AssetCorpusEvidence::INVENTORY);
            const auto K_VISIBILITY = pin.m_evidence.find(AssetCorpusEvidence::VISIBILITY);
            if (value.m_require_typed_inventory) {
                require((K_INVENTORY == pin.m_evidence.end()) ==
                            (K_VISIBILITY == pin.m_evidence.end()),
                        "Typed inventory and visibility refs must be selected together");
                if (K_INVENTORY != pin.m_evidence.end()) {
                    require(K_INPUT->m_inventory.has_value(),
                            "Corpus requires independently admitted inventory/visibility",
                            ErrorCode::RESYNC_REQUIRED);
                    const auto& inventory = K_INPUT->m_inventory.value();
                    require(inventory.selection().m_inventory == K_INVENTORY->second &&
                                inventory.selection().m_visibility == K_VISIBILITY->second &&
                                inventory.capture_product() == selected.m_product &&
                                inventory.receipt_sha256() == pin.m_receipt_sha256,
                            "Corpus inventory/visibility capture differs",
                            ErrorCode::RESYNC_REQUIRED);
                    blockers.push_back({ pin.m_label, "INVENTORY_LABELS_UNVERIFIED" });
                    blockers.push_back({ pin.m_label, "INVENTORY_COMPLETENESS_UNVERIFIED" });
                    blockers.push_back({ pin.m_label, "VISIBILITY_CORRESPONDENCE_UNVERIFIED" });
                } else {
                    require(!K_INPUT->m_inventory, "Typed inventory handle has no selected refs");
                }
            } else {
                require(!K_INPUT->m_inventory, "Typed inventory requires corpus schema three");
            }
            const auto K_TEMPORAL = pin.m_evidence.find(AssetCorpusEvidence::TEMPORAL_BLOCKS);
            const auto K_PATH = pin.m_evidence.find(AssetCorpusEvidence::INSPECTION_PATH);
            if (value.m_require_typed_paths) {
                require((K_TEMPORAL == pin.m_evidence.end()) == (K_PATH == pin.m_evidence.end()),
                        "Temporal and inspection refs must be selected together");
                if (K_TEMPORAL != pin.m_evidence.end()) {
                    require(K_INPUT->m_paths.has_value(),
                            "Corpus requires independently admitted evaluation paths",
                            ErrorCode::RESYNC_REQUIRED);
                    const auto& paths = K_INPUT->m_paths.value();
                    require(paths.selection().m_temporal == K_TEMPORAL->second &&
                                paths.selection().m_inspection == K_PATH->second &&
                                paths.capture_product() == selected.m_product &&
                                paths.receipt_sha256() == pin.m_receipt_sha256,
                            "Corpus evaluation paths/capture differ",
                            ErrorCode::RESYNC_REQUIRED);
                    blockers.push_back({ pin.m_label, "TEMPORAL_SAMPLING_UNVERIFIED" });
                    blockers.push_back({ pin.m_label, "INSPECTION_INDEPENDENCE_UNVERIFIED" });
                    blockers.push_back({ pin.m_label, "EVALUATION_POSES_UNVERIFIED" });
                    blockers.push_back({ pin.m_label, "PRETUNING_FREEZE_UNVERIFIED" });
                    if (paths.has_unassessed_or_uncertain_poses()) {
                        blockers.push_back({ pin.m_label, "EVALUATION_POSE_UNCERTAINTY_DECLARED" });
                    }
                } else {
                    require(!K_INPUT->m_paths, "Evaluation-path handle has no selected refs");
                }
            } else {
                require(!K_INPUT->m_paths, "Typed evaluation paths require schema four");
            }
            if (std::ranges::none_of(selected.m_frames, [](const auto& frame) {
                    return frame.m_role == CaptureFrameRole::HELD_OUT;
                })) {
                blockers.push_back({ pin.m_label, "HELD_OUT_PATH_MISSING" });
            }
            blockers.push_back({ pin.m_label, "SOURCE_RIGHTS_UNVERIFIED" });
            blockers.push_back({ pin.m_label, "REAL_CAPTURE_AUTHENTICITY_UNVERIFIED" });
            capture_rows.push_back({ { "label", pin.m_label },
                                    { "product", reference(pin.m_product) },
                                    { "selection_id", pin.m_selection.m_value },
                                    { "revision", pin.m_revision },
                                    { "receipt_sha256", pin.m_receipt_sha256 },
                                    { "commit",
                                     { { "generation", pin.m_commit.m_generation },
                                      { "manifest_sha256", pin.m_commit.m_manifest_sha256 } } },
                                    { "evidence", evidence } });
        }
        labels.clear();
        for (const auto& pin : value.m_recipes) {
            check_stop(stop);
            token(pin.m_label);
            require(labels.insert(pin.m_label).second, "Duplicate corpus recipe label");
            const auto K_INPUT =
                std::ranges::find(recipes, pin.m_label, &AssetCorpusRecipeInput::m_label);
            require(
                K_INPUT != recipes.end() &&
                    std::ranges::count(recipes, pin.m_label, &AssetCorpusRecipeInput::m_label) == 1,
                "Missing/duplicate independently selected recipe",
                ErrorCode::RESYNC_REQUIRED);
            digest_text(pin.m_receipt_sha256);
            digest_text(pin.m_recipe_sha256);
            require(pin.m_receipt_sha256 == K_INPUT->m_receipt.receipt_sha256() &&
                        bind(pin.m_product, K_INPUT->m_receipt).m_recipe_sha256 ==
                            pin.m_recipe_sha256,
                    "Corpus recipe receipt/lock differs",
                    ErrorCode::RESYNC_REQUIRED);
            auto inspected = inspect_asset_recipe(
                K_INPUT->m_receipt, pin.m_product.m_product, pin.m_context, pin.m_host, stop);
            if (!inspected) {
                return std::unexpected(inspected.error());
            }
            for (auto code : inspected->m_blockers) {
                const auto K_INDEX = static_cast<std::size_t>(code);
                require(K_INDEX < K_RECIPE_BLOCKERS.size(), "Unknown recipe blocker");
                blockers.push_back({ "recipe-" + pin.m_label, K_RECIPE_BLOCKERS[K_INDEX] });
            }
            // This freeze has not admitted component reviews, installed trees,
            // actual runtime closure or enforced worker limits.
            for (const auto* code : { "CAPTURE_INPUT_BINDING_UNVERIFIED",
                                     "INSTALLED_FILES_UNVERIFIED",
                                     "REVIEW_NOT_SELECTED",
                                     "RUNTIME_CLOSURE_UNVERIFIED",
                                     "WORKER_LIMITS_UNVERIFIED" }) {
                blockers.push_back({ "recipe-" + pin.m_label, code });
            }
            inspections.push_back(std::move(*inspected));
            recipe_rows.push_back({ { "label", pin.m_label },
                                   { "product", reference(pin.m_product) },
                                   { "receipt_sha256", pin.m_receipt_sha256 },
                                   { "recipe_sha256", pin.m_recipe_sha256 },
                                   { "world_epoch", pin.m_context.m_world_epoch.m_value },
                                   { "pose_revision", pin.m_context.m_pose_revision.m_value },
                                   { "host", host(pin.m_host) } });
        }
        if (value.m_track == AssetCorpusTrack::SYNTHETIC) {
            blockers.push_back({ "corpus", "SYNTHETIC_INPUTS" });
        }
        std::ranges::sort(blockers);
        Json document { { "schema_version",
                        value.m_require_typed_paths       ? 4
                        : value.m_require_typed_inventory ? 3
                        : value.m_require_typed_scale     ? 2
                                                          : 1 },
                       { "corpus", value.m_corpus },
                       { "revision", value.m_revision },
                       { "track", name(value.m_track, K_TRACKS) },
                       { "captures", capture_rows },
                       { "recipes", recipe_rows } };
        if (value.m_require_typed_scale || value.m_require_typed_inventory ||
            value.m_require_typed_paths) {
            document["typed_scale"] = value.m_require_typed_scale;
        }
        if (value.m_require_typed_inventory || value.m_require_typed_paths) {
            document["typed_inventory"] = value.m_require_typed_inventory;
        }
        if (value.m_require_typed_paths) {
            document["typed_paths"] = true;
        }
        auto canonical = document.dump();
        require(canonical.size() <= K_MAX_BYTES,
                "Corpus byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        AssetCorpusRef ref {
            value.m_corpus, value.m_revision, sha256(std::as_bytes(std::span(canonical))) };
        check_stop(stop);
        return AdmittedAssetCorpus(std::make_shared<const AdmittedAssetCorpus::Implementation>(
            AdmittedAssetCorpus::Implementation { std::move(value),
                                                 std::move(ref),
                                                 std::move(canonical),
                                                 std::move(blockers),
                                                 std::move(inspections),
                                                 captures,
                                                 recipes }));
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Corpus admission allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<AdmittedAssetCorpus>
decode_asset_corpus(std::span<const std::byte> bytes,
                    const AssetCorpusRef& selected,
                    const std::vector<AssetCorpusCaptureInput>& captures,
                    const std::vector<AssetCorpusRecipeInput>& recipes,
                    const std::stop_token& stop) {
    try {
        digest_text(selected.m_sha256);
        const auto K_DOC = parse(bytes, K_MAX_BYTES, stop);
        require(sha256(bytes) == selected.m_sha256,
                "Corpus bytes differ from selected hash",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_SCHEMA = integer(K_DOC.at("schema_version"));
        require(K_SCHEMA >= 1 && K_SCHEMA <= 4,
                "Unsupported corpus schema",
                ErrorCode::UNSUPPORTED_OPERATION);
        AssetCorpusDefinition value;
        if (K_SCHEMA == 4) {
            keys(K_DOC,
                 { "schema_version",
                  "corpus",
                  "revision",
                  "track",
                  "captures",
                  "recipes",
                  "typed_scale",
                  "typed_inventory",
                  "typed_paths" });
            require(K_DOC.at("typed_scale").is_boolean() &&
                        K_DOC.at("typed_inventory").is_boolean() &&
                        K_DOC.at("typed_paths").is_boolean() && K_DOC.at("typed_paths").get<bool>(),
                    "Schema four requires typed paths");
            value.m_require_typed_scale = K_DOC.at("typed_scale").get<bool>();
            value.m_require_typed_inventory = K_DOC.at("typed_inventory").get<bool>();
            value.m_require_typed_paths = true;
        } else if (K_SCHEMA == 3) {
            keys(K_DOC,
                 { "schema_version",
                  "corpus",
                  "revision",
                  "track",
                  "captures",
                  "recipes",
                  "typed_scale",
                  "typed_inventory" });
            require(K_DOC.at("typed_scale").is_boolean() &&
                        K_DOC.at("typed_inventory").is_boolean() &&
                        K_DOC.at("typed_inventory").get<bool>(),
                    "Schema three requires typed inventory");
            value.m_require_typed_scale = K_DOC.at("typed_scale").get<bool>();
            value.m_require_typed_inventory = true;
        } else if (K_SCHEMA == 2) {
            keys(K_DOC,
                 { "schema_version",
                  "corpus",
                  "revision",
                  "track",
                  "captures",
                  "recipes",
                  "typed_scale" });
            require(K_DOC.at("typed_scale").is_boolean() && K_DOC.at("typed_scale").get<bool>(),
                    "Schema two requires typed scale");
            value.m_require_typed_scale = true;
        } else {
            keys(K_DOC, { "schema_version", "corpus", "revision", "track", "captures", "recipes" });
        }
        value.m_corpus = K_DOC.at("corpus").get<std::string>();
        value.m_revision = integer(K_DOC.at("revision"));
        require(value.m_corpus == selected.m_corpus && value.m_revision == selected.m_revision,
                "Corpus identity differs from independent selection",
                ErrorCode::RESYNC_REQUIRED);
        value.m_track = named(K_DOC.at("track"), K_TRACKS);
        array(K_DOC.at("captures"), 16uz);
        array(K_DOC.at("recipes"), 16uz);
        for (const auto& row : K_DOC.at("captures")) {
            check_stop(stop);
            keys(row,
                 { "label",
                  "product",
                  "selection_id",
                  "revision",
                  "receipt_sha256",
                  "commit",
                  "evidence" });
            const auto& commit = row.at("commit");
            keys(commit, { "generation", "manifest_sha256" });
            AssetCorpusCapture capture {
                row.at("label").get<std::string>(),
                reference(row.at("product")),
                { integer(row.at("selection_id")) },
                integer(row.at("revision")),
                digest(row.at("receipt_sha256")),
                { integer(commit.at("generation")), digest(commit.at("manifest_sha256")) },
                {} };
            const auto& evidence = row.at("evidence");
            require(evidence.is_object() && evidence.size() == K_EVIDENCE.size(),
                    "Invalid corpus evidence table");
            for (const auto& [K_KIND, K_NAME] : K_EVIDENCE) {
                require(evidence.contains(K_NAME), "Missing corpus evidence field");
                if (!evidence.at(K_NAME).is_null()) {
                    capture.m_evidence.emplace(K_KIND, reference(evidence.at(K_NAME)));
                }
            }
            value.m_captures.push_back(std::move(capture));
        }
        for (const auto& row : K_DOC.at("recipes")) {
            check_stop(stop);
            keys(row,
                 { "label",
                  "product",
                  "receipt_sha256",
                  "recipe_sha256",
                  "world_epoch",
                  "pose_revision",
                  "host" });
            value.m_recipes.push_back(
                { row.at("label").get<std::string>(),
                 reference(row.at("product")),
                 digest(row.at("receipt_sha256")),
                 digest(row.at("recipe_sha256")),
                 { { integer(row.at("world_epoch")) }, { integer(row.at("pose_revision")) } },
                 host(row.at("host")) });
        }
        auto result = admit_asset_corpus(value, captures, recipes, stop);
        if (!result) {
            return std::unexpected(result.error());
        }
        require(same_bytes(bytes, result->canonical_json()), "Corpus bytes are not canonical");
        return result;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Corpus decode allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_asset_corpus(const AdmittedAssetCorpus& corpus,
                                          const std::filesystem::path& directory,
                                          OperationId operation,
                                          const std::stop_token& stop,
                                          const std::function<void(std::size_t)>& observed) {
    try {
        check_stop(stop);
        std::vector<std::filesystem::path> roots;
        for (const auto& pin : corpus.definition().m_captures) {
            const auto K_SOURCE = std::ranges::find(
                corpus.m_state->m_captures, pin.m_label, &AssetCorpusCaptureInput::m_label);
            registry(pin, *K_SOURCE, stop);
            roots.push_back(K_SOURCE->m_registry);
            roots.push_back(K_SOURCE->m_receipt.source_directory());
        }
        for (const auto& source : corpus.m_state->m_recipe_inputs) {
            roots.push_back(source.m_receipt.source_directory());
        }
        const auto K_COMMIT =
            publish_immutable_records(directory,
                                      operation,
                                      0,
                                      { { "sealed-corpus", encoded(corpus.canonical_json()) } },
                                      roots,
                                      stop,
                                      observed);
        if (K_COMMIT && K_COMMIT->m_generation != 1) {
            return fail(ErrorCode::RESYNC_REQUIRED, "Corpus directory contains later publications");
        }
        return K_COMMIT;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Corpus publication allocation failed");
    }
}

Result<AdmittedAssetCorpus> read_asset_corpus(const std::filesystem::path& directory,
                                              const RecordCommit& commit,
                                              const AssetCorpusRef& selected,
                                              const std::vector<AssetCorpusCaptureInput>& captures,
                                              const std::vector<AssetCorpusRecipeInput>& recipes,
                                              const std::stop_token& stop) {
    const auto K_SNAPSHOT = read_immutable_records(directory, stop);
    if (!K_SNAPSHOT) {
        return std::unexpected(K_SNAPSHOT.error());
    }
    if (commit.m_generation != 1 || K_SNAPSHOT->m_commit.m_generation != 1 ||
        K_SNAPSHOT->m_commit.m_manifest_sha256 != commit.m_manifest_sha256 ||
        K_SNAPSHOT->m_records.size() != 1uz || !K_SNAPSHOT->m_records.contains("sealed-corpus")) {
        return fail(ErrorCode::RESYNC_REQUIRED,
                    "Corpus seal commit/contents differs from selection");
    }
    return decode_asset_corpus(
        K_SNAPSHOT->m_records.at("sealed-corpus"), selected, captures, recipes, stop);
}

}  // namespace world_explorer
