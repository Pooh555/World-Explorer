#include "world_explorer/application/external_model_inspection.hpp"

#include "../core/json_records.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"

#include <array>
#include <limits>

namespace world_explorer {
namespace {

using namespace json_record_detail;
namespace fs = std::filesystem;
constexpr auto K_REQUEST_BYTES = 2uz * 1024uz * 1024uz;

template <class T> T take(Result<T> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return std::move(*result);
}

std::string text(const Json& value, std::size_t maximum) {
    const auto K_TEXT = value.get<std::string>();
    require(!K_TEXT.empty() && K_TEXT.size() <= maximum && K_TEXT.find('\0') == K_TEXT.npos,
            "Invalid external MODEL request text");
    return K_TEXT;
}

bool boolean(const Json& value) {
    require(value.is_boolean(), "Expected external MODEL request boolean");
    return value.get<bool>();
}

fs::path path(const Json& value, const fs::path& base) {
    const auto K_TEXT = text(value, 4096uz);
    const fs::path K_PATH { std::u8string(K_TEXT.begin(), K_TEXT.end()) };
    require(K_PATH.is_absolute() || (!K_PATH.has_root_name() && !K_PATH.has_root_directory()),
            "Ambiguous external MODEL request path root");
    for (const auto& part : K_PATH) {
        require(part != "." && part != "..", "Traversal in external MODEL request path");
    }
    const auto K_PATH_FROM_REQUEST = base / K_PATH;
    require(K_PATH_FROM_REQUEST.is_absolute() &&
                K_PATH_FROM_REQUEST.lexically_normal() == K_PATH_FROM_REQUEST &&
                std::distance(K_PATH_FROM_REQUEST.begin(), K_PATH_FROM_REQUEST.end()) <= 64z,
            "Invalid external MODEL request native path");
    const auto K_UTF8 = K_PATH_FROM_REQUEST.generic_u8string();
    require(K_UTF8.size() <= 4096uz,
            "External MODEL request native path exceeds budget",
            ErrorCode::RESOURCE_EXHAUSTED);
    return K_PATH_FROM_REQUEST;
}

void directories(fs::path directory, const std::stop_token& stop) {
    while (!directory.empty()) {
        check_stop(stop);
        require(fs::is_directory(fs::symlink_status(directory)),
                "External MODEL request parent must be a nonsymlink directory");
        if (directory == directory.parent_path()) {
            break;
        }
        directory = directory.parent_path();
    }
}

AssetReceiptSelection receipt(const Json& value, const fs::path& base) {
    keys(value, { "directory", "sha256" });
    return { path(value.at("directory"), base), digest(value.at("sha256")) };
}

AssetRecipeHost host(const Json& value) {
    keys(value,
         { "contracts_only",
          "host_ram_bytes",
          "device_vram_bytes",
          "free_disk_bytes",
          "cpu_threads",
          "queued_jobs" });
    return { boolean(value.at("contracts_only")),
            integer(value.at("host_ram_bytes")),
            integer(value.at("device_vram_bytes")),
            integer(value.at("free_disk_bytes")),
            integer(value.at("cpu_threads")),
            integer(value.at("queued_jobs")) };
}

AssetExternalModelLimits limits(const Json& value) {
    keys(value, { "file_bytes", "total_bytes" });
    const AssetExternalModelLimits K_LIMITS { integer(value.at("file_bytes")),
                                             integer(value.at("total_bytes")) };
    require(K_LIMITS.m_file_bytes && K_LIMITS.m_file_bytes <= 4uz * 1024uz * 1024uz * 1024uz &&
                K_LIMITS.m_file_bytes <= std::numeric_limits<std::size_t>::max() &&
                K_LIMITS.m_total_bytes >= K_LIMITS.m_file_bytes &&
                K_LIMITS.m_total_bytes <= 8uz * 1024uz * 1024uz * 1024uz,
            "Invalid external MODEL request byte ceilings",
            ErrorCode::INVALID_ARGUMENT);
    return K_LIMITS;
}

std::vector<AssetExternalModelSelection> models(const Json& value,
                                                const fs::path& base,
                                                const AssetExternalModelLimits& ceiling,
                                                const std::stop_token& stop) {
    array(value, 16uz);
    require(
        !value.empty(), "Missing external MODEL request selections", ErrorCode::INVALID_ARGUMENT);
    std::vector<AssetExternalModelSelection> result;
    std::size_t count {};
    std::uint64_t total {};
    for (const auto& model : value) {
        check_stop(stop);
        keys(model, { "component_sha256", "files" });
        const auto& rows = model.at("files");
        array(rows, 64uz);
        require(!rows.empty() && rows.size() <= 256uz - count,
                "External MODEL request file-count budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        count += rows.size();
        AssetExternalModelSelection selected { digest(model.at("component_sha256")), {} };
        for (const auto& row : rows) {
            check_stop(stop);
            keys(row, { "path", "sha256", "bytes", "file" });
            const auto K_BYTES = integer(row.at("bytes"));
            require(K_BYTES && K_BYTES <= ceiling.m_file_bytes &&
                        K_BYTES <= ceiling.m_total_bytes - total,
                    "External MODEL request file/total byte budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            total += K_BYTES;
            selected.m_files.push_back({ text(row.at("path"), 240uz),
                                        digest(row.at("sha256")),
                                        K_BYTES,
                                        path(row.at("file"), base) });
        }
        result.push_back(std::move(selected));
    }
    return result;
}

std::vector<AssetReviewSelection> reviews(const Json& value) {
    array(value, 16uz);
    std::vector<AssetReviewSelection> result;
    for (const auto& row : value) {
        keys(row, { "role", "component_sha256", "review_sha256", "reviewer" });
        require(row.at("role").get<std::string>() == "MODEL",
                "External MODEL request review role differs");
        result.push_back({ AssetRecipeRole::MODEL,
                          digest(row.at("component_sha256")),
                          digest(row.at("review_sha256")),
                          text(row.at("reviewer"), 128uz) });
    }
    return result;
}

std::string report(const AssetExternalModelObservation& observation,
                   const Json& request,
                   const std::string& request_sha256) {
    const std::array K_BLOCKERS { "CONTRACTS_ONLY_HOST",
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
    auto blockers = Json::array(), model_rows = Json::array();
    for (auto blocker : observation.m_recipe.m_blockers) {
        blockers.push_back(K_BLOCKERS.at(static_cast<std::size_t>(blocker)));
    }
    for (const auto& model : observation.m_models) {
        auto files = Json::array();
        for (const auto& file : model.m_files) {
            const auto K_PATH = file.m_file.generic_u8string();
            files.push_back({ { "path", file.m_relative_path },
                             { "sha256", file.m_sha256 },
                             { "bytes", file.m_bytes },
                             { "file", std::string(K_PATH.begin(), K_PATH.end()) } });
        }
        model_rows.push_back({ { "component_sha256", model.m_component_sha256 },
                              { "terms_sha256", model.m_terms_sha256 },
                              { "review_sha256", model.m_review_sha256 },
                              { "reviewer", model.m_reviewer },
                              { "review_selected", model.m_review_selected },
                              { "files", files } });
    }
    const auto K_OPTIONAL = [](const std::optional<std::uint64_t>& value) {
        return value ? Json(*value) : Json(nullptr);
    };
    const Json K_REPORT {
        { "schema_version", 1 },
        { "kind", "EXTERNAL_MODEL_OBSERVATION" },
        { "source_kind", request.at("source_kind") },
        { "source_kind_origin", "CALLER_DECLARED" },
        { "qualification", "UNQUALIFIED" },
        { "observation_only", true },
        { "immutable_payloads", false },
        { "execution_authorized", false },
        { "request_sha256", request_sha256 },
        { "recipe_receipt_sha256", observation.m_recipe.m_receipt_sha256 },
        { "evidence_receipt_sha256", observation.m_evidence_receipt_sha256 },
        { "recipe_sha256", observation.m_recipe.m_recipe_sha256 },
        { "graph_sha256", observation.m_recipe.m_graph_sha256 },
        { "product_id", observation.m_recipe.m_product.m_value },
        { "context", request.at("context") },
        { "host", request.at("host") },
        { "limits", request.at("limits") },
        { "installation_bytes", K_OPTIONAL(observation.m_recipe.m_installation_bytes) },
        { "reserved_disk_bytes", K_OPTIONAL(observation.m_recipe.m_reserved_disk_bytes) },
        { "observed_bytes", observation.m_observed_bytes },
        { "hashed_bytes", observation.m_hashed_bytes },
        { "passes", 2 },
        { "models", model_rows },
        { "blockers", blockers } };
    return K_REPORT.dump();
}

}  // namespace

Result<std::string> inspect_asset_external_models_request(
    const fs::path& request,
    const std::string& expected_sha256,
    const std::stop_token& stop,
    const std::function<void(const AssetExternalModelProgress&)>& progress) {
    try {
        check_stop(stop);
        digest_text(expected_sha256);
        require(
            !request.empty(), "Missing external MODEL request path", ErrorCode::INVALID_ARGUMENT);
        directories(fs::absolute(request).parent_path(), stop);
        const auto K_BYTES = take(asset_files::read_bytes(request, K_REQUEST_BYTES, stop));
        require(sha256(K_BYTES) == expected_sha256,
                "External MODEL request checksum differs",
                ErrorCode::RESYNC_REQUIRED);
        const auto K_DOC = parse(K_BYTES, K_REQUEST_BYTES, stop);
        keys(K_DOC,
             { "schema_version",
              "kind",
              "source_kind",
              "recipe_receipt",
              "evidence_receipt",
              "product_id",
              "context",
              "host",
              "limits",
              "models",
              "reviews" });
        require(integer(K_DOC.at("schema_version")) == 1 &&
                    K_DOC.at("kind").get<std::string>() == "EXTERNAL_MODEL_OBSERVATION",
                "Unsupported external MODEL request profile",
                ErrorCode::UNSUPPORTED_OPERATION);
        const auto K_KIND = text(K_DOC.at("source_kind"), 16uz);
        require(K_KIND == "SYNTHETIC" || K_KIND == "UNQUALIFIED",
                "Unsupported external MODEL source label",
                ErrorCode::UNSUPPORTED_OPERATION);
        keys(K_DOC.at("context"), { "world_epoch", "pose_revision" });
        const AssetDependencyContext K_CONTEXT { { integer(K_DOC.at("context").at("world_epoch")) },
                                                { integer(K_DOC.at("context").at("pose_revision")) } };
        const AssetProductId K_PRODUCT { integer(K_DOC.at("product_id")) };
        require(K_PRODUCT.m_value && K_CONTEXT.m_pose_revision.m_value,
                "Unselected external MODEL product/pose",
                ErrorCode::INVALID_ARGUMENT);
        const auto K_BASE = fs::canonical(request).parent_path();
        const auto K_RECIPE = receipt(K_DOC.at("recipe_receipt"), K_BASE);
        const auto K_EVIDENCE = receipt(K_DOC.at("evidence_receipt"), K_BASE);
        const auto K_HOST = host(K_DOC.at("host"));
        const auto K_LIMITS = limits(K_DOC.at("limits"));
        const auto K_MODELS = models(K_DOC.at("models"), K_BASE, K_LIMITS, stop);
        const auto K_REVIEWS = reviews(K_DOC.at("reviews"));
        // Parse every selection/budget before opening either small receipt or
        // any external payload. The lower-level observer repeats semantic joins.
        directories(K_RECIPE.m_directory, stop);
        directories(K_EVIDENCE.m_directory, stop);
        const auto K_RECIPE_BYTES = take(read_asset_receipt(K_RECIPE, K_CONTEXT, stop));
        const auto K_EVIDENCE_BYTES = take(read_asset_receipt(K_EVIDENCE, K_CONTEXT, stop));
        const auto K_OBSERVATION = take(inspect_asset_external_models(K_RECIPE_BYTES,
                                                                      K_PRODUCT,
                                                                      K_EVIDENCE_BYTES,
                                                                      K_CONTEXT,
                                                                      K_HOST,
                                                                      K_REVIEWS,
                                                                      K_MODELS,
                                                                      K_LIMITS,
                                                                      stop,
                                                                      progress));
        auto result = report(K_OBSERVATION, K_DOC, expected_sha256);
        check_stop(stop);
        return result;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "External MODEL request allocation failed");
    } catch (const Json::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    } catch (const fs::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
