#include "world_explorer/asset_catalog/component_evidence.hpp"

#include "component_json.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <limits>
#include <new>

namespace world_explorer {
namespace {

using namespace asset_recipe_detail;
using namespace asset_component_detail;
using ComponentKey = std::pair<AssetRecipeRole, std::string>;
constexpr std::size_t K_RECORD_LIMIT { 64uz * 1024uz };
constexpr std::size_t K_MANIFEST_LIMIT { 256uz * 1024uz };
constexpr std::size_t K_PAYLOAD_LIMIT { 16uz * 1024uz * 1024uz };

}  // namespace

Result<AssetRecipeEvidenceInspection>
inspect_asset_recipe_evidence(const AdmittedAssetReceipt& recipe_receipt,
                              AssetProductId product,
                              const AdmittedAssetReceipt& evidence_receipt,
                              const AssetDependencyContext& expected,
                              const AssetRecipeHost& host,
                              const std::vector<AssetReviewSelection>& selected_reviews,
                              const std::stop_token& stop) {
    try {
        check_stop(stop);
        const auto K_GRAPH = validate_asset_dependencies(evidence_receipt.graph(), expected, stop);
        if (!K_GRAPH) {
            return std::unexpected(K_GRAPH.error());
        }
        auto inspected = inspect_asset_recipe(recipe_receipt, product, expected, host, stop);
        if (!inspected) {
            return std::unexpected(inspected.error());
        }
        require(selected_reviews.size() <= 64uz,
                "Too many independent review selections",
                ErrorCode::INVALID_ARGUMENT);
        std::map<ComponentKey, AssetReviewSelection> selections;
        for (const auto& selection : selected_reviews) {
            require(selections
                        .emplace(ComponentKey { selection.m_role, selection.m_component_sha256 },
                                 selection)
                        .second,
                    "Duplicate independent review selection",
                    ErrorCode::INVALID_ARGUMENT);
        }
        std::set<ComponentKey> required;
        for (const auto& component : inspected->m_components) {
            required.emplace(component.m_role, component.m_sha256);
        }
        for (const auto& [K_KEY, K_SELECTION] : selections) {
            require(required.contains(K_KEY),
                    "Review selection names an unrelated component",
                    ErrorCode::INVALID_ARGUMENT);
            digest(K_SELECTION.m_review_sha256);
            reviewer(K_SELECTION.m_reviewer);
        }
        // Configuration/prompt records are opaque, but their selected bytes must exist.
        read(evidence_receipt, inspected->m_configuration_sha256, K_RECORD_LIMIT, stop);
        read(evidence_receipt, inspected->m_prompt_sha256, K_RECORD_LIMIT, stop);
        AssetRecipeEvidenceInspection result {
            std::move(*inspected), evidence_receipt.receipt_sha256(), {} };
        std::map<std::string, std::size_t> payload_sizes;
        const auto K_PAYLOAD_SIZE = [&](const std::string& sha) {
            const auto K_FOUND = payload_sizes.find(sha);
            if (K_FOUND != payload_sizes.end()) {
                return K_FOUND->second;
            }
            const auto K_SIZE = read(evidence_receipt, sha, K_PAYLOAD_LIMIT, stop).size();
            payload_sizes.emplace(sha, K_SIZE);
            return K_SIZE;
        };
        std::size_t file_entries {};
        bool all_selected { true };
        for (const auto& component : result.m_recipe.m_components) {
            check_stop(stop);
            const auto K_ARCHIVE_BYTES = component.m_archive_bytes;
            const auto K_INSTALLED_BYTES = component.m_installed_bytes;
            const auto K_TERMS_SHA = component.m_terms_sha256;
            const auto K_REVIEW_SHA = component.m_review_sha256;
            if (!K_ARCHIVE_BYTES.has_value() || !K_INSTALLED_BYTES.has_value() ||
                !K_TERMS_SHA.has_value() || !K_REVIEW_SHA.has_value()) {
                return fail(ErrorCode::INVALID_DATA,
                            "Component evidence requires known sizes and terms/review identities");
            }
            AssetComponentEvidence verified {
                component.m_role, component.m_sha256, 0uz, 0, 0, false, {} };
            if (component.m_role == AssetRecipeRole::INPUT) {
                verified.m_file_count = 1uz;
                verified.m_installed_bytes =
                    read(recipe_receipt, component.m_sha256, K_PAYLOAD_LIMIT, stop).size();
                require(++file_entries <= 4096uz,
                        "Component file-entry budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
            } else {
                const auto K_DOCUMENT =
                    parse(read(evidence_receipt, component.m_sha256, K_MANIFEST_LIMIT, stop));
                keys(K_DOCUMENT, { "schema_version", "role", "archive", "installed_bytes", "files" });
                require(integer(K_DOCUMENT.at("schema_version")) == 1,
                        "Unsupported component schema",
                        ErrorCode::UNSUPPORTED_OPERATION);
                require(role(K_DOCUMENT.at("role")) == component.m_role,
                        "Component manifest role differs from recipe",
                        ErrorCode::RESYNC_REQUIRED);
                const auto& files = K_DOCUMENT.at("files");
                require(files.is_array() && !files.empty(), "Missing component inventory");
                require(files.size() <= 1024uz && files.size() <= 4096uz - file_entries,
                        "Component file-entry budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                file_entries += files.size();
                std::set<std::string> paths;
                for (const auto& file : files) {
                    check_stop(stop);
                    keys(file, { "path", "sha256", "bytes" });
                    require(paths.insert(portable_path(file.at("path"))).second,
                            "Duplicate/case-aliased component path");
                    const auto K_SIZE = integer(file.at("bytes"));
                    require(K_SIZE <= K_PAYLOAD_LIMIT,
                            "Component payload exceeds receipt budget",
                            ErrorCode::RESOURCE_EXHAUSTED);
                    const auto K_SHA = digest(file.at("sha256"));
                    if (K_SIZE == 0) {
                        require(K_SHA == sha256(std::span<const std::byte> {}),
                                "Empty component file has a nonempty content identity");
                    } else {
                        require(K_PAYLOAD_SIZE(K_SHA) == K_SIZE, "Component payload size mismatch");
                    }
                    verified.m_installed_bytes = add(verified.m_installed_bytes, K_SIZE);
                    verified.m_files.push_back({ file.at("path").get<std::string>(), K_SHA, K_SIZE });
                }
                for (const auto& path : paths) {
                    for (auto slash = path.find('/'); slash != path.npos;
                         slash = path.find('/', slash + 1uz)) {
                        require(!paths.contains(path.substr(0uz, slash)),
                                "Component file is another file's parent");
                    }
                }
                verified.m_file_count = files.size();
                require(integer(K_DOCUMENT.at("installed_bytes")) == verified.m_installed_bytes,
                        "Component inventory byte sum mismatch");
                if (!K_DOCUMENT.at("archive").is_null()) {
                    const auto& archive = K_DOCUMENT.at("archive");
                    keys(archive, { "sha256", "bytes" });
                    verified.m_archive_bytes = count(archive.at("bytes"), K_PAYLOAD_LIMIT);
                    require(K_PAYLOAD_SIZE(digest(archive.at("sha256"))) ==
                                verified.m_archive_bytes,
                            "Component archive byte size mismatch");
                }
            }
            require(verified.m_archive_bytes == K_ARCHIVE_BYTES.value() &&
                        verified.m_installed_bytes == K_INSTALLED_BYTES.value(),
                    "Component byte costs differ from selected recipe",
                    ErrorCode::RESYNC_REQUIRED);
            read(evidence_receipt, K_TERMS_SHA.value(), K_RECORD_LIMIT, stop);
            const auto K_REVIEW =
                parse(read(evidence_receipt, K_REVIEW_SHA.value(), K_RECORD_LIMIT, stop));
            keys(K_REVIEW,
                 { "schema_version",
                  "role",
                  "component_sha256",
                  "terms_sha256",
                  "reviewer",
                  "local_use",
                  "distribution" });
            require(integer(K_REVIEW.at("schema_version")) == 1,
                    "Unsupported review schema",
                    ErrorCode::UNSUPPORTED_OPERATION);
            require(role(K_REVIEW.at("role")) == component.m_role &&
                        digest(K_REVIEW.at("component_sha256")) == component.m_sha256 &&
                        digest(K_REVIEW.at("terms_sha256")) == K_TERMS_SHA.value() &&
                        rights(K_REVIEW.at("local_use")) == component.m_local_use &&
                        rights(K_REVIEW.at("distribution")) == component.m_distribution,
                    "Review record differs from selected recipe/terms",
                    ErrorCode::RESYNC_REQUIRED);
            const auto K_REVIEWER = reviewer(K_REVIEW.at("reviewer").get<std::string>());
            const auto K_SELECTED = selections.find({ component.m_role, component.m_sha256 });
            if (K_SELECTED != selections.end()) {
                require(K_SELECTED->second.m_review_sha256 == K_REVIEW_SHA.value() &&
                            K_SELECTED->second.m_reviewer == K_REVIEWER,
                        "Review record differs from independent selection",
                        ErrorCode::RESYNC_REQUIRED);
                verified.m_review_selected = true;
            } else {
                all_selected = false;
            }
            result.m_components.push_back(std::move(verified));
        }
        auto& blockers = result.m_recipe.m_blockers;
        std::erase(blockers, AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED);
        blockers.push_back(AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED);
        if (!all_selected) {
            blockers.push_back(AssetRecipeBlocker::REVIEW_NOT_SELECTED);
        }
        std::ranges::sort(blockers);
        check_stop(stop);
        return result;
    } catch (const RecipeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Component evidence allocation failed");
    } catch (const Json::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
