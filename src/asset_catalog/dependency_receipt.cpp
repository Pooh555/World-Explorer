#include "world_explorer/asset_catalog/dependency_receipt.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/core/hash.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace world_explorer {
namespace {

using Json = nlohmann::json;
constexpr std::size_t K_MANIFEST_LIMIT { 1024uz * 1024uz };
constexpr std::size_t K_FILE_LIMIT { 16uz * 1024uz * 1024uz };
constexpr std::size_t K_TOTAL_LIMIT { 64uz * 1024uz * 1024uz };
constexpr std::size_t K_FILE_COUNT { 768uz };

struct ReceiptFailure : std::runtime_error {
    ErrorCode m_code;

    ReceiptFailure(ErrorCode code, const std::string& message)
        : std::runtime_error(message), m_code(code) { }
};

void require(bool condition, ErrorCode code, const std::string& message) {
    if (!condition) {
        throw ReceiptFailure(code, message);
    }
}

template <class Value> Value take(Result<Value> result) {
    if (!result) {
        throw ReceiptFailure(result.error().m_code, result.error().m_message);
    }
    return std::move(*result);
}

void take(Result<void> result) {
    if (!result) {
        throw ReceiptFailure(result.error().m_code, result.error().m_message);
    }
}

void check_stop(const std::stop_token& stop) {
    require(!stop.stop_requested(), ErrorCode::CANCELLED, "Asset receipt cancelled");
}

bool digest_valid(const std::string& value) {
    return value.size() == 64uz && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

void keys(const Json& value, std::initializer_list<std::string_view> names) {
    require(value.is_object() && value.size() == names.size(),
            ErrorCode::INVALID_DATA,
            "Unexpected asset receipt fields");
    for (const auto K_NAME : names) {
        require(value.contains(K_NAME), ErrorCode::INVALID_DATA, "Missing asset receipt field");
    }
}

std::uint64_t integer(const Json& value) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            ErrorCode::INVALID_DATA,
            "Asset receipt requires an unsigned integer");
    return value.get<std::uint64_t>();
}

std::string digest(const Json& value) {
    const auto K_VALUE = value.get<std::string>();
    require(digest_valid(K_VALUE), ErrorCode::INVALID_DATA, "Invalid asset receipt SHA-256");
    return K_VALUE;
}

Json parse(std::span<const std::byte> bytes) {
    const std::string_view K_TEXT(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    require(K_TEXT.find('\0') == K_TEXT.npos, ErrorCode::INVALID_DATA, "NUL in asset receipt JSON");
    std::vector<std::set<std::string>> object_keys;
    return Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 16,
                ErrorCode::RESOURCE_EXHAUSTED,
                "Asset receipt JSON nesting exceeds budget");
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            object_keys.resize(K_DEPTH + 1uz);
            object_keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key) {
            require(object_keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second,
                    ErrorCode::INVALID_DATA,
                    "Duplicate asset receipt JSON key");
        }
        return true;
    });
}

AssetRevisionRef reference(const Json& value) {
    keys(value, { "product_id", "revision", "content_sha256" });
    return { { integer(value.at("product_id")) },
            integer(value.at("revision")),
            digest(value.at("content_sha256")) };
}

AssetDependencyGraph graph_from_json(const Json& value) {
    keys(value, { "schema_version", "world_epoch", "pose_revision", "products" });
    require(integer(value.at("schema_version")) == 1,
            ErrorCode::UNSUPPORTED_OPERATION,
            "Unsupported asset graph schema");
    AssetDependencyGraph graph;
    graph.m_context = { { integer(value.at("world_epoch")) }, { integer(value.at("pose_revision")) } };
    const auto& products = value.at("products");
    require(products.is_array() && !products.empty(),
            ErrorCode::INVALID_DATA,
            "Missing asset products");
    require(
        products.size() <= 256uz, ErrorCode::RESOURCE_EXHAUSTED, "Asset product budget exceeded");
    std::size_t edges {};
    for (const auto& row : products) {
        keys(row,
             { "product",
              "dependencies",
              "recipe_sha256",
              "manual_action_sha256",
              "manual_ancestor" });
        AssetDependencyNode node;
        node.m_product = reference(row.at("product"));
        node.m_recipe_sha256 = digest(row.at("recipe_sha256"));
        if (!row.at("manual_action_sha256").is_null()) {
            node.m_manual_action_sha256 = digest(row.at("manual_action_sha256"));
        }
        node.m_manual_ancestor = row.at("manual_ancestor").get<bool>();
        const auto& dependencies = row.at("dependencies");
        require(dependencies.is_array(),
                ErrorCode::INVALID_DATA,
                "Asset dependencies must be an array");
        require(dependencies.size() <= 64uz && dependencies.size() <= 4096uz - edges,
                ErrorCode::RESOURCE_EXHAUSTED,
                "Asset dependency budget exceeded");
        edges += dependencies.size();
        for (const auto& parent : dependencies) {
            node.m_dependencies.push_back(reference(parent));
        }
        graph.m_products.push_back(std::move(node));
    }
    return graph;
}

bool entry_exists(const std::filesystem::path& path) {
    return std::filesystem::exists(std::filesystem::symlink_status(path));
}

void regular_directory(const std::filesystem::path& path) {
    require(std::filesystem::is_directory(std::filesystem::symlink_status(path)),
            ErrorCode::INVALID_DATA,
            "Asset receipt directory must not be linked");
}

bool overlaps(const std::filesystem::path& first, const std::filesystem::path& second) {
    const auto K_PREFIX = [](const auto& parent, const auto& child) {
        auto entry = child.begin();
        for (const auto& component : parent) {
            if (entry == child.end() || *entry != component) {
                return false;
            }
            ++entry;
        }
        return true;
    };
    return K_PREFIX(first, second) || K_PREFIX(second, first);
}

struct Blob {
    std::string m_sha256;
    std::vector<std::byte> m_bytes;
};

}  // namespace

struct AdmittedAssetReceipt::Implementation {
    AssetDependencyGraph m_graph;
    OperationId m_operation;
    std::filesystem::path m_source;
    std::string m_sha256;
    std::vector<std::byte> m_document;
    std::map<std::string, Blob> m_blobs;
};

AdmittedAssetReceipt::AdmittedAssetReceipt(std::shared_ptr<const Implementation> state)
    : m_state(std::move(state)) { }

const AssetDependencyGraph& AdmittedAssetReceipt::graph() const {
    return m_state->m_graph;
}

const std::string& AdmittedAssetReceipt::receipt_sha256() const {
    return m_state->m_sha256;
}

const std::filesystem::path& AdmittedAssetReceipt::source_directory() const {
    return m_state->m_source;
}

OperationId AdmittedAssetReceipt::operation() const {
    return m_state->m_operation;
}

std::size_t AdmittedAssetReceipt::file_count() const {
    return m_state->m_blobs.size();
}

Result<std::vector<std::byte>> AdmittedAssetReceipt::copy_blob(const std::string& digest,
                                                               std::size_t max_bytes,
                                                               const std::stop_token& stop) const {
    try {
        check_stop(stop);
        require(digest_valid(digest) && max_bytes > 0uz && max_bytes <= K_FILE_LIMIT,
                ErrorCode::INVALID_ARGUMENT,
                "Invalid admitted asset blob selection/budget");
        const auto K_FOUND = m_state->m_blobs.find(digest);
        require(
            K_FOUND != m_state->m_blobs.end(), ErrorCode::NOT_FOUND, "Unknown admitted asset blob");
        require(K_FOUND->second.m_bytes.size() <= max_bytes,
                ErrorCode::RESOURCE_EXHAUSTED,
                "Admitted asset blob exceeds copy budget");
        return K_FOUND->second.m_bytes;
    } catch (const ReceiptFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Admitted asset blob copy allocation failed");
    }
}

Result<AdmittedAssetReceipt> read_asset_receipt(const AssetReceiptSelection& selection,
                                                const AssetDependencyContext& expected,
                                                const std::stop_token& stop) {
    try {
        check_stop(stop);
        require(!selection.m_directory.empty() && digest_valid(selection.m_receipt_sha256),
                ErrorCode::INVALID_ARGUMENT,
                "Asset receipt selection requires directory and SHA-256");
        require(expected.m_pose_revision.m_value != 0,
                ErrorCode::INVALID_ARGUMENT,
                "Expected asset pose is unselected");
        regular_directory(selection.m_directory);
        auto state = std::make_shared<AdmittedAssetReceipt::Implementation>();
        state->m_source = std::filesystem::canonical(selection.m_directory);
        state->m_document =
            take(asset_files::read_bytes(state->m_source / "receipt.json", K_MANIFEST_LIMIT, stop));
        state->m_sha256 = sha256(state->m_document);
        require(state->m_sha256 == selection.m_receipt_sha256,
                ErrorCode::INVALID_DATA,
                "Asset receipt selection hash mismatch");
        const auto K_VALUE = parse(state->m_document);
        keys(K_VALUE, { "schema_version", "operation_id", "graph_sha256", "graph", "files" });
        require(integer(K_VALUE.at("schema_version")) == 1,
                ErrorCode::UNSUPPORTED_OPERATION,
                "Unsupported asset receipt schema");
        state->m_operation = { integer(K_VALUE.at("operation_id")) };
        require(state->m_operation.m_value != 0,
                ErrorCode::INVALID_DATA,
                "Asset receipt operation is unselected");
        state->m_graph = graph_from_json(K_VALUE.at("graph"));
        const auto K_GRAPH = take(validate_asset_dependencies(state->m_graph, expected, stop));
        require(digest(K_VALUE.at("graph_sha256")) == K_GRAPH.m_graph_sha256,
                ErrorCode::INVALID_DATA,
                "Asset receipt graph hash mismatch");
        std::set<std::string> required;
        for (const auto& node : state->m_graph.m_products) {
            required.insert(node.m_product.m_content_sha256);
            required.insert(node.m_recipe_sha256);
            if (node.m_manual_action_sha256) {
                required.insert(*node.m_manual_action_sha256);
            }
        }
        const auto& files = K_VALUE.at("files");
        require(
            files.is_array(), ErrorCode::INVALID_DATA, "Asset receipt inventory must be an array");
        require(files.size() <= K_FILE_COUNT,
                ErrorCode::RESOURCE_EXHAUSTED,
                "Asset receipt file count exceeded");
        require(files.size() == required.size(),
                ErrorCode::INVALID_DATA,
                "Asset receipt inventory is not exact");
        std::map<std::string, std::size_t> inventory;
        std::size_t total {};
        for (const auto& file : files) {
            keys(file, { "file", "sha256", "bytes" });
            const auto K_SHA = digest(file.at("sha256"));
            const auto K_BYTES = integer(file.at("bytes"));
            require(K_BYTES <= K_FILE_LIMIT && K_BYTES <= K_TOTAL_LIMIT - total,
                    ErrorCode::RESOURCE_EXHAUSTED,
                    "Asset receipt byte budget exceeded");
            require(required.contains(K_SHA) &&
                        inventory.emplace(K_SHA, static_cast<std::size_t>(K_BYTES)).second &&
                        file.at("file") == "blobs/" + K_SHA + ".bin" && K_BYTES > 0,
                    ErrorCode::INVALID_DATA,
                    "Invalid or duplicate asset receipt file");
            total += static_cast<std::size_t>(K_BYTES);
        }
        regular_directory(state->m_source / "blobs");
        for (const auto& [K_SHA, K_BYTES] : inventory) {
            check_stop(stop);
            auto bytes = take(asset_files::read_bytes(
                state->m_source / "blobs" / (K_SHA + ".bin"), K_BYTES, stop));
            require(bytes.size() == K_BYTES && sha256(bytes) == K_SHA,
                    ErrorCode::INVALID_DATA,
                    "Asset receipt blob size/hash mismatch");
            state->m_blobs.emplace(K_SHA, Blob { K_SHA, std::move(bytes) });
        }
        // Detect a changed selection while the independently hashed blobs loaded.
        const auto K_AGAIN =
            take(asset_files::read_bytes(state->m_source / "receipt.json", K_MANIFEST_LIMIT, stop));
        require(K_AGAIN == state->m_document,
                ErrorCode::INVALID_DATA,
                "Asset receipt changed during admission");
        return AdmittedAssetReceipt(std::move(state));
    } catch (const ReceiptFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset receipt allocation failed");
    } catch (const Json::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

Result<AssetReceiptSelection>
publish_asset_receipt(const AdmittedAssetReceipt& receipt,
                      const std::filesystem::path& directory,
                      const AssetDependencyContext& expected,
                      const std::stop_token& stop,
                      const std::function<void(std::size_t)>& observed) {
    try {
        check_stop(stop);
        take(validate_asset_dependencies(receipt.m_state->m_graph, expected, stop));
        require(!directory.empty() && directory.has_filename() && directory.filename() != "." &&
                    directory.filename() != "..",
                ErrorCode::INVALID_ARGUMENT,
                "Asset receipt output must name a directory");
        const auto K_ROOT = std::filesystem::weakly_canonical(directory);
        require(!overlaps(K_ROOT, receipt.m_state->m_source),
                ErrorCode::INVALID_ARGUMENT,
                "Asset receipt output overlaps source");
        regular_directory(K_ROOT.parent_path());
        AssetReceiptSelection result { K_ROOT, receipt.m_state->m_sha256 };
        const Json K_OWNER { { "schema_version", 1 },
                            { "role", "asset_dependency_receipt" },
                            { "operation_id", receipt.operation().m_value },
                            { "receipt_sha256", result.m_receipt_sha256 } };
        const auto K_OWNER_TEXT = K_OWNER.dump();
        const auto K_OWNER_BYTES = std::as_bytes(std::span(K_OWNER_TEXT));
        const auto K_OWNER_SHA = sha256(K_OWNER_BYTES);
        const auto K_CHECK = [&]() {
            if (!entry_exists(directory)) {
                return false;
            }
            regular_directory(directory);
            const auto K_OWNED = entry_exists(K_ROOT / "owner.json");
            if (K_OWNED) {
                const auto K_BYTES =
                    take(asset_files::read_bytes(K_ROOT / "owner.json", 1024uz, stop));
                require(sha256(K_BYTES) == K_OWNER_SHA,
                        ErrorCode::INVALID_DATA,
                        "Asset receipt output belongs to a different operation/selection");
            }
            std::size_t entries {};
            for (const auto& entry : std::filesystem::directory_iterator(K_ROOT)) {
                const auto K_NAME = entry.path().filename().string();
                require(++entries <= 4uz && !entry.is_symlink() &&
                            (K_NAME == ".writer.lock" ||
                             (K_OWNED && (K_NAME == "owner.json" || K_NAME == "blobs" ||
                                          K_NAME == "receipt.json"))) &&
                            (K_NAME == "blobs" ? entry.is_directory() : entry.is_regular_file()),
                        ErrorCode::INVALID_DATA,
                        "Asset receipt output is unowned or contains foreign entries");
            }
            const auto K_COMPLETE = entry_exists(K_ROOT / "receipt.json");
            std::size_t blobs {};
            if (entry_exists(K_ROOT / "blobs")) {
                regular_directory(K_ROOT / "blobs");
                for (const auto& entry : std::filesystem::directory_iterator(K_ROOT / "blobs")) {
                    check_stop(stop);
                    require(++blobs <= K_FILE_COUNT &&
                                std::filesystem::is_regular_file(entry.symlink_status()),
                            ErrorCode::INVALID_DATA,
                            "Unexpected asset receipt blob entry");
                    const auto K_NAME = entry.path().filename().string();
                    require(K_NAME.size() == 68uz && K_NAME.ends_with(".bin"),
                            ErrorCode::INVALID_DATA,
                            "Unexpected asset receipt blob filename");
                    const auto K_FOUND = receipt.m_state->m_blobs.find(K_NAME.substr(0uz, 64uz));
                    require(K_FOUND != receipt.m_state->m_blobs.end(),
                            ErrorCode::INVALID_DATA,
                            "Foreign asset receipt blob");
                    const auto K_BYTES = take(asset_files::read_bytes(
                        entry.path(), K_FOUND->second.m_bytes.size(), stop));
                    require(K_BYTES == K_FOUND->second.m_bytes,
                            ErrorCode::INVALID_DATA,
                            "Corrupt asset receipt blob; never overwrite");
                }
            }
            if (K_COMPLETE) {
                require(blobs == receipt.m_state->m_blobs.size(),
                        ErrorCode::INVALID_DATA,
                        "Completed asset receipt is missing blobs");
                const auto K_BYTES =
                    take(asset_files::read_bytes(K_ROOT / "receipt.json", K_MANIFEST_LIMIT, stop));
                require(K_BYTES == receipt.m_state->m_document,
                        ErrorCode::INVALID_DATA,
                        "Different/corrupt asset receipt completion");
            }
            return K_COMPLETE;
        };
        K_CHECK();
        check_stop(stop);
        if (std::filesystem::create_directory(K_ROOT)) {
            take(asset_files::flush_directory_entry(K_ROOT));
        }
        auto lock = take(FileLock::acquire(K_ROOT / ".writer.lock"));
        if (K_CHECK()) {
            return result;
        }
        if (!entry_exists(K_ROOT / "owner.json")) {
            take(asset_files::publish_asset(K_ROOT / "owner.json", K_OWNER_BYTES, stop));
        }
        if (std::filesystem::create_directory(K_ROOT / "blobs")) {
            take(asset_files::flush_directory_entry(K_ROOT / "blobs"));
        }
        std::size_t installed {};
        for (const auto& [K_SHA, K_BLOB] : receipt.m_state->m_blobs) {
            check_stop(stop);
            const auto K_PATH = K_ROOT / "blobs" / (K_SHA + ".bin");
            if (!entry_exists(K_PATH)) {
                take(asset_files::publish_asset(K_PATH, K_BLOB.m_bytes, stop));
            } else {
                const auto K_BYTES =
                    take(asset_files::read_bytes(K_PATH, K_BLOB.m_bytes.size(), stop));
                require(K_BYTES == K_BLOB.m_bytes,
                        ErrorCode::INVALID_DATA,
                        "Changed asset receipt blob");
            }
            if (observed) {
                observed(++installed);
            }
        }
        check_stop(stop);
        take(validate_asset_dependencies(receipt.graph(), expected, stop));
        K_CHECK();
        const auto K_PUBLISHED =
            asset_files::publish_asset(K_ROOT / "receipt.json", receipt.m_state->m_document, stop);
        if (!K_PUBLISHED && entry_exists(K_ROOT / "receipt.json")) {
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "Asset receipt publication outcome requires reopening");
        }
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
        return result;
    } catch (const ReceiptFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset receipt publication allocation failed");
    } catch (const std::filesystem::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE, error.what());
    }
}

}  // namespace world_explorer
