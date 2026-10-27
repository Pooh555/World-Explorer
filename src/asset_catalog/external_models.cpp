#include "world_explorer/asset_catalog/external_models.hpp"

#include "component_json.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <limits>

namespace world_explorer {
namespace {

using namespace asset_component_detail;
namespace fs = std::filesystem;
constexpr std::uint64_t K_FILE_LIMIT { 4uz * 1024uz * 1024uz * 1024uz };
constexpr std::uint64_t K_TOTAL_LIMIT { 8uz * 1024uz * 1024uz * 1024uz };

struct FileStamp {
    std::uint64_t m_bytes {};
    fs::file_time_type m_modified;
    auto operator<=>(const FileStamp&) const = default;
};

void directories(const fs::path& directory, const std::stop_token& stop) {
    for (auto current = directory; !current.empty(); current = current.parent_path()) {
        check_stop(stop);
        require(fs::is_directory(fs::symlink_status(current)),
                "External model parent is missing, linked or not a directory");
        if (current == current.parent_path()) {
            break;
        }
    }
}

FileStamp stamp(const AssetExternalModelFile& file, const std::stop_token& stop) {
    check_stop(stop);
    directories(file.m_file.parent_path(), stop);
    require(fs::is_regular_file(fs::symlink_status(file.m_file)) &&
                fs::hard_link_count(file.m_file) == 1,
            "External model must be a regular single-link nonsymlink file");
    require(fs::canonical(file.m_file) == file.m_file,
            "External model file path has an alias",
            ErrorCode::INVALID_ARGUMENT);
    return { fs::file_size(file.m_file), fs::last_write_time(file.m_file) };
}

template <class T> T take(Result<T> value) {
    if (!value) {
        throw RecipeFailure(value.error().m_code, value.error().m_message);
    }
    return std::move(*value);
}

void blockers(AssetRecipeInspection& recipe) {
    // MODEL files are observations, not the entire recipe's owned component,
    // review, installation or runtime closure. Preserve all original blockers.
    for (auto value : { AssetRecipeBlocker::COMPONENT_BYTES_UNVERIFIED,
                       AssetRecipeBlocker::INSTALLED_FILES_UNVERIFIED,
                       AssetRecipeBlocker::REVIEW_NOT_SELECTED,
                       AssetRecipeBlocker::RUNTIME_CLOSURE_UNVERIFIED,
                       AssetRecipeBlocker::WORKER_LIMITS_UNVERIFIED }) {
        recipe.m_blockers.push_back(value);
    }
    std::ranges::sort(recipe.m_blockers);
    const auto K_DUPLICATES = std::ranges::unique(recipe.m_blockers);
    recipe.m_blockers.erase(K_DUPLICATES.begin(), K_DUPLICATES.end());
}

}  // namespace

Result<AssetExternalModelObservation> inspect_asset_external_models(
    const AdmittedAssetReceipt& recipe_receipt,
    AssetProductId product,
    const AdmittedAssetReceipt& evidence_receipt,
    const AssetDependencyContext& expected,
    const AssetRecipeHost& host,
    const std::vector<AssetReviewSelection>& selected_reviews,
    const std::vector<AssetExternalModelSelection>& selected_models,
    const AssetExternalModelLimits& limits,
    const std::stop_token& stop,
    const std::function<void(const AssetExternalModelProgress&)>& progress) {
    try {
        check_stop(stop);
        require(limits.m_file_bytes && limits.m_file_bytes <= K_FILE_LIMIT &&
                    limits.m_file_bytes <= std::numeric_limits<std::size_t>::max() &&
                    limits.m_total_bytes >= limits.m_file_bytes &&
                    limits.m_total_bytes <= K_TOTAL_LIMIT,
                "Invalid independently selected external model byte limits",
                ErrorCode::INVALID_ARGUMENT);
        require(!selected_models.empty() && selected_models.size() <= 16uz &&
                    selected_reviews.size() <= 16uz,
                "Invalid external model/review selection count",
                ErrorCode::INVALID_ARGUMENT);
        (void)take(validate_asset_dependencies(evidence_receipt.graph(), expected, stop));
        auto recipe = take(inspect_asset_recipe(recipe_receipt, product, expected, host, stop));
        std::map<std::string, AssetExternalModelSelection> selections;
        for (const auto& model : selected_models) {
            check_stop(stop);
            (void)digest(model.m_component_sha256);
            require(!model.m_files.empty() && model.m_files.size() <= 64uz &&
                        selections.emplace(model.m_component_sha256, model).second,
                    "Duplicate, empty or excessive external model selection",
                    ErrorCode::INVALID_ARGUMENT);
        }
        std::map<std::string, AssetReviewSelection> reviews;
        for (const auto& review : selected_reviews) {
            (void)digest(review.m_component_sha256);
            (void)digest(review.m_review_sha256);
            (void)reviewer(review.m_reviewer);
            require(review.m_role == AssetRecipeRole::MODEL &&
                        selections.contains(review.m_component_sha256) &&
                        reviews.emplace(review.m_component_sha256, review).second,
                    "Duplicate or unrelated external MODEL review selection",
                    ErrorCode::INVALID_ARGUMENT);
        }
        AssetExternalModelObservation result {
            2, std::move(recipe), limits, evidence_receipt.receipt_sha256(), {}, 0, 0 };
        std::size_t file_count {};
        for (const auto& component : result.m_recipe.m_components) {
            check_stop(stop);
            if (component.m_role != AssetRecipeRole::MODEL) {
                continue;
            }
            const auto K_SELECTED = selections.find(component.m_sha256);
            require(K_SELECTED != selections.end(),
                    "Missing independent external MODEL selection",
                    ErrorCode::INVALID_ARGUMENT);
            require(
                component.m_archive_bytes.has_value() && component.m_archive_bytes.value() == 0 &&
                    component.m_installed_bytes.has_value() &&
                    component.m_terms_sha256.has_value() && component.m_review_sha256.has_value(),
                "External MODEL observation requires known costs, no archive and terms/review "
                "pins");
            const auto K_DOCUMENT =
                parse(read(evidence_receipt, component.m_sha256, 256uz * 1024uz, stop));
            keys(K_DOCUMENT,
                 { "schema_version", "kind", "role", "archive", "installed_bytes", "files" });
            require(integer(K_DOCUMENT.at("schema_version")) == 2 &&
                        K_DOCUMENT.at("kind").get<std::string>() == "EXTERNAL_MODEL_FILES",
                    "Unsupported external MODEL observation profile",
                    ErrorCode::UNSUPPORTED_OPERATION);
            require(role(K_DOCUMENT.at("role")) == AssetRecipeRole::MODEL &&
                        K_DOCUMENT.at("archive").is_null(),
                    "External MODEL manifest role/archive differs",
                    ErrorCode::RESYNC_REQUIRED);
            const auto& rows = K_DOCUMENT.at("files");
            require(rows.is_array() && !rows.empty() && rows.size() <= 64uz &&
                        rows.size() <= 256uz - file_count &&
                        rows.size() == K_SELECTED->second.m_files.size(),
                    "External MODEL file coverage/budget differs",
                    ErrorCode::RESOURCE_EXHAUSTED);
            file_count += rows.size();
            std::map<std::string, AssetExternalModelFile> selected_files;
            for (const auto& file : K_SELECTED->second.m_files) {
                const auto K_KEY = portable_path(file.m_relative_path);
                (void)digest(file.m_sha256);
                require(file.m_file.is_absolute() &&
                            file.m_file.generic_u8string().size() <= 4096uz &&
                            file.m_file.native().find(fs::path::value_type {}) ==
                                fs::path::string_type::npos &&
                            std::distance(file.m_file.begin(), file.m_file.end()) <= 64z &&
                            file.m_file.lexically_normal() == file.m_file &&
                            selected_files.emplace(K_KEY, file).second,
                        "Invalid or duplicate independent external MODEL file/path",
                        ErrorCode::INVALID_ARGUMENT);
            }
            AssetExternalModelComponent observed;
            observed.m_component_sha256 = component.m_sha256;
            observed.m_terms_sha256 = component.m_terms_sha256.value();
            observed.m_review_sha256 = component.m_review_sha256.value();
            std::uint64_t bytes {};
            std::set<std::string> paths;
            for (const auto& row : rows) {
                check_stop(stop);
                keys(row, { "path", "sha256", "bytes" });
                const auto K_KEY = portable_path(row.at("path"));
                require(paths.insert(K_KEY).second,
                        "Duplicate/case-aliased external MODEL inventory path");
                const auto K_SIZE = integer(row.at("bytes"));
                require(K_SIZE && K_SIZE <= limits.m_file_bytes &&
                            K_SIZE <= limits.m_total_bytes - result.m_observed_bytes,
                        "External MODEL file/aggregate byte budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                const auto K_FILE = selected_files.find(K_KEY);
                require(K_FILE != selected_files.end() &&
                            K_FILE->second.m_relative_path == row.at("path").get<std::string>() &&
                            K_FILE->second.m_bytes == K_SIZE &&
                            K_FILE->second.m_sha256 == digest(row.at("sha256")),
                        "External MODEL file identity differs from independent selection",
                        ErrorCode::RESYNC_REQUIRED);
                observed.m_files.push_back(K_FILE->second);
                selected_files.erase(K_FILE);
                bytes = add(bytes, K_SIZE);
                result.m_observed_bytes = add(result.m_observed_bytes, K_SIZE);
            }
            for (const auto& path : paths) {
                for (auto slash = path.find('/'); slash != path.npos;
                     slash = path.find('/', slash + 1uz)) {
                    require(!paths.contains(path.substr(0uz, slash)),
                            "External MODEL file is another logical file's parent");
                }
            }
            require(selected_files.empty() && integer(K_DOCUMENT.at("installed_bytes")) == bytes &&
                        component.m_installed_bytes.value() == bytes,
                    "External MODEL costs/coverage differ from recipe",
                    ErrorCode::RESYNC_REQUIRED);
            (void)read(evidence_receipt, observed.m_terms_sha256, 64uz * 1024uz, stop);
            const auto K_REVIEW =
                parse(read(evidence_receipt, observed.m_review_sha256, 64uz * 1024uz, stop));
            keys(K_REVIEW,
                 { "schema_version",
                  "role",
                  "component_sha256",
                  "terms_sha256",
                  "reviewer",
                  "local_use",
                  "distribution" });
            require(integer(K_REVIEW.at("schema_version")) == 1 &&
                        role(K_REVIEW.at("role")) == AssetRecipeRole::MODEL &&
                        digest(K_REVIEW.at("component_sha256")) == component.m_sha256 &&
                        digest(K_REVIEW.at("terms_sha256")) == observed.m_terms_sha256 &&
                        rights(K_REVIEW.at("local_use")) == component.m_local_use &&
                        rights(K_REVIEW.at("distribution")) == component.m_distribution,
                    "External MODEL review differs from recipe/terms",
                    ErrorCode::RESYNC_REQUIRED);
            observed.m_reviewer = reviewer(K_REVIEW.at("reviewer").get<std::string>());
            const auto K_REVIEW_SELECTED = reviews.find(component.m_sha256);
            if (K_REVIEW_SELECTED != reviews.end()) {
                require(K_REVIEW_SELECTED->second.m_review_sha256 == observed.m_review_sha256 &&
                            K_REVIEW_SELECTED->second.m_reviewer == observed.m_reviewer,
                        "External MODEL review differs from independent selection",
                        ErrorCode::RESYNC_REQUIRED);
                observed.m_review_selected = true;
            }
            result.m_models.push_back(std::move(observed));
            selections.erase(K_SELECTED);
        }
        require(!result.m_models.empty() && selections.empty(),
                "Missing or unrelated external MODEL component",
                ErrorCode::INVALID_ARGUMENT);
        // Files are explicit observations, not a closed directory or native
        // environment. Neighbouring unselected entries are not inspected.
        std::vector<AssetExternalModelFile> files;
        std::vector<FileStamp> baseline;
        for (const auto& model : result.m_models) {
            for (const auto& file : model.m_files) {
                const auto K_STAMP = stamp(file, stop);
                require(K_STAMP.m_bytes == file.m_bytes,
                        "External MODEL declared file size differs");
                for (const auto& previous : files) {
                    check_stop(stop);
                    require(!fs::equivalent(previous.m_file, file.m_file),
                            "External MODEL files alias",
                            ErrorCode::INVALID_ARGUMENT);
                }
                files.push_back(file);
                baseline.push_back(K_STAMP);
            }
        }
        const auto K_RECHECK = [&](std::size_t index) {
            require(stamp(files[index], stop) == baseline[index],
                    "External MODEL file changed during observation",
                    ErrorCode::RESYNC_REQUIRED);
        };
        for (std::size_t pass { 1uz }; pass <= 2uz; ++pass) {
            for (std::size_t index { 0uz }; index < files.size(); ++index) {
                check_stop(stop);
                K_RECHECK(index);
                const auto& file = files[index];
                const auto K_DIGEST =
                    take(sha256_file(file.m_file, static_cast<std::size_t>(file.m_bytes), stop));
                K_RECHECK(index);
                require(K_DIGEST == file.m_sha256, "External MODEL content checksum differs");
                result.m_hashed_bytes = add(result.m_hashed_bytes, file.m_bytes);
                if (progress) {
                    progress({ pass, index, result.m_hashed_bytes });
                }
                check_stop(stop);
                K_RECHECK(index);
            }
            for (std::size_t index { 0uz }; index < files.size(); ++index) {
                K_RECHECK(index);
            }
        }
        blockers(result.m_recipe);
        check_stop(stop);
        return result;
    } catch (const RecipeFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "External MODEL observation allocation failed");
    } catch (const Json::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    } catch (const fs::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
