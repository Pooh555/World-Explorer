#include "world_explorer/core/immutable_records.hpp"

#include "json_records.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/core/hash.hpp"

#include <map>
#include <set>

namespace world_explorer {
namespace {

using namespace json_record_detail;
namespace fs = std::filesystem;
constexpr std::size_t K_MIB { 1024uz * 1024uz };
constexpr std::string_view K_OWNER { R"({"role":"immutable_record_store_v1","schema_version":1})" };

template <class Value> Value take(Result<Value> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
    return std::move(*result);
}

void take(Result<void> result) {
    if (!result) {
        throw RecordFailure(result.error().m_code, result.error().m_message);
    }
}

bool entry_exists(const fs::path& path) {
    return fs::symlink_status(path).type() != fs::file_type::not_found;
}

void regular(const fs::path& path) {
    require(fs::is_regular_file(fs::symlink_status(path)) && fs::hard_link_count(path) == 1,
            "Record store file is linked or not regular");
}

void directory(const fs::path& path) {
    require(fs::is_directory(fs::symlink_status(path)),
            "Record store directory is linked or absent");
}

bool prefix(const fs::path& first, const fs::path& second) {
    const auto K_MATCH = std::mismatch(first.begin(), first.end(), second.begin(), second.end());
    return K_MATCH.first == first.end();
}

fs::path location(const fs::path& selected) {
    require(!selected.empty() && selected.has_filename() && selected.filename() != "." &&
                selected.filename() != "..",
            "Record store requires a directory name",
            ErrorCode::INVALID_ARGUMENT);
    const auto K_ABSOLUTE = fs::absolute(selected);
    fs::path current;
    for (const auto& part : K_ABSOLUTE) {
        current /= part;
        if (entry_exists(current)) {
            directory(current);
        }
    }
    directory(K_ABSOLUTE.parent_path());
    return fs::weakly_canonical(K_ABSOLUTE);
}

std::vector<std::byte> bytes(std::string_view text) {
    const auto K_SPAN = std::as_bytes(std::span(text));
    return { K_SPAN.begin(), K_SPAN.end() };
}

std::vector<std::byte>
read(const fs::path& path, std::size_t maximum, const std::stop_token& stop) {
    regular(path);
    return take(asset_files::read_bytes(path, maximum, stop));
}

void immutable(const fs::path& path,
               const std::vector<std::byte>& data,
               const std::stop_token& stop) {
    check_stop(stop);
    if (entry_exists(path)) {
        require(read(path, data.size(), stop) == data, "Existing immutable record bytes differ");
    } else {
        take(asset_files::publish_asset(path, data, stop));
    }
}

void key(const std::string& value) {
    require(!value.empty() && value.size() <= 64uz &&
                value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == value.npos,
            "Invalid immutable record key");
}

struct Binding {
    std::string m_sha;
    std::size_t m_bytes {};
    auto operator<=>(const Binding&) const = default;
};

using Bindings = std::map<std::string, Binding>;

Json rows(const Bindings& records) {
    auto result = Json::array();
    for (const auto& [K_NAME, K_BINDING] : records) {
        result.push_back(
            { { "key", K_NAME }, { "sha256", K_BINDING.m_sha }, { "bytes", K_BINDING.m_bytes } });
    }
    return result;
}

struct RecordLimits {
    std::size_t m_count, m_bytes;
};

Bindings bindings(const Json& value, RecordLimits limits) {
    array(value, limits.m_count);
    Bindings result;
    std::size_t total {};
    for (const auto& row : value) {
        keys(row, { "key", "sha256", "bytes" });
        const auto K_NAME = row.at("key").get<std::string>();
        key(K_NAME);
        const auto K_SIZE = integer(row.at("bytes"));
        require(K_SIZE && K_SIZE <= 4uz * K_MIB && K_SIZE <= limits.m_bytes - total,
                "Record byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        total += static_cast<std::size_t>(K_SIZE);
        require(result
                    .emplace(K_NAME,
                             Binding { digest(row.at("sha256")), static_cast<std::size_t>(K_SIZE) })
                    .second,
                "Duplicate record key");
    }
    return result;
}

struct Intent {
    std::uint64_t m_operation {}, m_base {};
    Bindings m_records;
    std::string m_sha;
};

Intent intent(const std::vector<std::byte>& data, const std::stop_token& stop) {
    const auto K_DOC = parse(data, 64uz * 1024uz, stop);
    keys(K_DOC, { "schema_version", "operation_id", "base_generation", "records" });
    require(integer(K_DOC.at("schema_version")) == 1, "Unsupported record intent schema");
    Intent result { integer(K_DOC.at("operation_id")),
                   integer(K_DOC.at("base_generation")),
                   bindings(K_DOC.at("records"), { 65uz, 8uz * K_MIB }),
                   sha256(data) };
    require(result.m_operation && result.m_base < 1024 && !result.m_records.empty(),
            "Invalid record intent identity/base");
    require(same_bytes(data, K_DOC.dump()) && K_DOC.at("records") == rows(result.m_records),
            "Noncanonical record intent");
    return result;
}

struct State {
    RecordCommit m_commit;
    Bindings m_records;
    std::map<std::uint64_t, Intent> m_intents;
    std::map<std::string, std::vector<std::byte>> m_blobs;
    Json m_operations = Json::array();
    std::vector<std::byte> m_manifest;
    std::size_t m_intent_bytes {}, m_blob_bytes {};
    bool m_owned {};
};

State inspect(const fs::path& root, bool allow_unowned, const std::stop_token& stop) {
    State state;
    check_stop(stop);
    if (!entry_exists(root)) {
        require(allow_unowned, "Record store not found", ErrorCode::NOT_FOUND);
        return state;
    }
    directory(root);
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(root)) {
        check_stop(stop);
        const auto K_NAME = entry.path().filename().string();
        require(names.size() < 5uz && names.insert(K_NAME).second,
                "Record store has unexpected entries");
        if (K_NAME == "blobs" || K_NAME == "intents") {
            directory(entry.path());
        } else {
            require(K_NAME == "owner.json" || K_NAME == "manifest.json" || K_NAME == ".writer.lock",
                    "Foreign record store entry");
            regular(entry.path());
        }
    }
    if (!names.contains("owner.json")) {
        require(allow_unowned && (names.empty() || names == std::set<std::string> { ".writer.lock" }),
                "Record store is unowned");
        return state;
    }
    require(same_bytes(read(root / "owner.json", 1024uz, stop), K_OWNER),
            "Wrong record store owner");
    state.m_owned = true;
    if (names.contains("blobs")) {
        for (const auto& entry : fs::directory_iterator(root / "blobs")) {
            check_stop(stop);
            require(state.m_blobs.size() < 8192uz,
                    "Record blob count budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            const auto K_NAME = entry.path().filename().string();
            require(K_NAME.size() == 68uz && K_NAME.ends_with(".bin"),
                    "Foreign record blob filename");
            const auto K_SHA = K_NAME.substr(0uz, 64uz);
            digest_text(K_SHA);
            auto data =
                read(entry.path(), std::min(4uz * K_MIB, 64uz * K_MIB - state.m_blob_bytes), stop);
            require(sha256(data) == K_SHA, "Corrupt record blob");
            state.m_blob_bytes += data.size();
            state.m_blobs.emplace(K_SHA, std::move(data));
        }
    }
    if (names.contains("intents")) {
        for (const auto& entry : fs::directory_iterator(root / "intents")) {
            check_stop(stop);
            require(state.m_intents.size() < 1024uz,
                    "Record intent count budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            auto data = read(
                entry.path(), std::min(64uz * 1024uz, 16uz * K_MIB - state.m_intent_bytes), stop);
            state.m_intent_bytes += data.size();
            auto value = intent(data, stop);
            require(entry.path().filename() == std::to_string(value.m_operation) + ".json",
                    "Foreign record intent filename");
            require(state.m_intents.emplace(value.m_operation, std::move(value)).second,
                    "Duplicate record intent");
        }
    }
    std::map<std::string, std::size_t> intended;
    for (const auto& [K_OPERATION, K_VALUE] : state.m_intents) {
        for (const auto& [K_NAME, K_BINDING] : K_VALUE.m_records) {
            const auto K_FOUND = intended.find(K_BINDING.m_sha);
            require(K_FOUND == intended.end() || K_FOUND->second == K_BINDING.m_bytes,
                    "Conflicting intended blob size");
            intended[K_BINDING.m_sha] = K_BINDING.m_bytes;
        }
    }
    for (const auto& [K_SHA, K_DATA] : state.m_blobs) {
        const auto K_FOUND = intended.find(K_SHA);
        require(K_FOUND != intended.end() && K_FOUND->second == K_DATA.size(),
                "Record blob has no matching owned intent");
    }
    if (!names.contains("manifest.json")) {
        return state;
    }
    state.m_manifest = read(root / "manifest.json", K_MIB, stop);
    const auto K_DOC = parse(state.m_manifest, K_MIB, stop);
    keys(K_DOC, { "schema_version", "generation", "records", "operations" });
    require(integer(K_DOC.at("schema_version")) == 1, "Unsupported record manifest schema");
    state.m_commit = { integer(K_DOC.at("generation")), sha256(state.m_manifest) };
    state.m_records = bindings(K_DOC.at("records"), { 4096uz, 64uz * K_MIB });
    state.m_operations = K_DOC.at("operations");
    array(state.m_operations, 1024uz);
    require(state.m_commit.m_generation > 0 &&
                state.m_commit.m_generation == state.m_operations.size(),
            "Invalid record store generation");
    Bindings committed;
    std::set<std::uint64_t> generations, operations;
    std::uint64_t previous {};
    for (const auto& row : state.m_operations) {
        keys(row, { "operation_id", "intent_sha256", "new_generation" });
        const auto K_OP = integer(row.at("operation_id"));
        const auto K_GEN = integer(row.at("new_generation"));
        require(K_OP > previous && operations.insert(K_OP).second && K_GEN > 0 &&
                    K_GEN <= state.m_commit.m_generation && generations.insert(K_GEN).second,
                "Invalid record operation history");
        previous = K_OP;
        const auto K_FOUND = state.m_intents.find(K_OP);
        require(K_FOUND != state.m_intents.end(), "Missing committed record intent");
        const auto& selected = K_FOUND->second;
        require(selected.m_sha == digest(row.at("intent_sha256")) && selected.m_base == K_GEN - 1,
                "Committed intent differs");
        for (const auto& [K_NAME, K_BINDING] : selected.m_records) {
            const auto K_EXISTING = committed.find(K_NAME);
            require(K_EXISTING == committed.end() || K_EXISTING->second == K_BINDING,
                    "Conflicting committed record revision");
            committed[K_NAME] = K_BINDING;
        }
    }
    require(committed == state.m_records, "Committed record inventory differs");
    require(same_bytes(state.m_manifest, K_DOC.dump()) &&
                K_DOC.at("records") == rows(state.m_records),
            "Noncanonical record manifest");
    for (const auto& [K_NAME, K_BINDING] : state.m_records) {
        const auto K_FOUND = state.m_blobs.find(K_BINDING.m_sha);
        require(K_FOUND != state.m_blobs.end() && K_FOUND->second.size() == K_BINDING.m_bytes,
                "Missing committed record bytes");
    }
    require(read(root / "manifest.json", K_MIB, stop) == state.m_manifest,
            "Record manifest changed during observation",
            ErrorCode::RESYNC_REQUIRED);
    return state;
}

void make_directory(const fs::path& path) {
    if (fs::create_directory(path)) {
        take(asset_files::flush_directory_entry(path));
    }
    directory(path);
}

}  // namespace

Result<RecordStoreSnapshot> read_immutable_records(const fs::path& selected,
                                                   const std::stop_token& stop) {
    try {
        auto state = inspect(location(selected), false, stop);
        RecordStoreSnapshot result { state.m_commit, {} };
        for (const auto& [K_NAME, K_BINDING] : state.m_records) {
            result.m_records.emplace(K_NAME, state.m_blobs.at(K_BINDING.m_sha));
        }
        check_stop(stop);
        return result;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Record snapshot allocation failed");
    } catch (const fs::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<RecordCommit> publish_immutable_records(const fs::path& selected,
                                               OperationId operation,
                                               std::uint64_t base_generation,
                                               const std::vector<ImmutableRecord>& records,
                                               const std::vector<fs::path>& protected_roots,
                                               const std::stop_token& stop,
                                               const std::function<void(std::size_t)>& observed) {
    try {
        check_stop(stop);
        require(operation.m_value && base_generation < 1024,
                "Invalid record operation/base",
                ErrorCode::INVALID_ARGUMENT);
        require(!records.empty() && records.size() <= 65uz,
                "Record batch count budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        Bindings requested;
        std::size_t total {};
        for (const auto& record : records) {
            check_stop(stop);
            key(record.m_key);
            require(!record.m_bytes.empty() && record.m_bytes.size() <= 4uz * K_MIB &&
                        record.m_bytes.size() <= 8uz * K_MIB - total,
                    "Record batch byte budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            total += record.m_bytes.size();
            require(
                requested
                    .emplace(record.m_key, Binding { sha256(record.m_bytes), record.m_bytes.size() })
                    .second,
                "Duplicate record batch key");
        }
        const auto K_ROOT = location(selected);
        for (const auto& protected_root : protected_roots) {
            require(!protected_root.empty(),
                    "Empty protected record root",
                    ErrorCode::INVALID_ARGUMENT);
            const auto K_PROTECTED = fs::weakly_canonical(protected_root);
            require(!prefix(K_ROOT, K_PROTECTED) && !prefix(K_PROTECTED, K_ROOT),
                    "Record store overlaps protected source",
                    ErrorCode::INVALID_ARGUMENT);
        }
        const Json K_INTENT { { "schema_version", 1 },
                             { "operation_id", operation.m_value },
                             { "base_generation", base_generation },
                             { "records", rows(requested) } };
        const auto K_INTENT_BYTES = bytes(K_INTENT.dump());
        const auto K_INTENT_SHA = sha256(K_INTENT_BYTES);
        auto state = inspect(K_ROOT, true, stop);
        // Validate conflicts before creating any output, then repeat under the lock.
        const auto K_VALIDATE = [&] {
            const auto K_PRIOR = state.m_intents.find(operation.m_value);
            require(K_PRIOR == state.m_intents.end() || K_PRIOR->second.m_sha == K_INTENT_SHA,
                    "Record operation ID collision",
                    ErrorCode::DUPLICATE_OPERATION);
            bool completed {};
            for (const auto& row : state.m_operations) {
                completed = completed || integer(row.at("operation_id")) == operation.m_value;
            }
            if (completed) {
                return true;
            }
            require(state.m_commit.m_generation == base_generation,
                    "Wrong record store base; reopen",
                    ErrorCode::RESYNC_REQUIRED);
            require(state.m_operations.size() < 1024uz,
                    "Record operation budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            auto merged = state.m_records;
            for (const auto& [K_NAME, K_BINDING] : requested) {
                const auto K_FOUND = merged.find(K_NAME);
                require(K_FOUND == merged.end() || K_FOUND->second == K_BINDING,
                        "Committed record ID/revision conflict",
                        ErrorCode::RESYNC_REQUIRED);
                merged[K_NAME] = K_BINDING;
            }
            require(merged.size() <= 4096uz,
                    "Committed record count budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            std::size_t committed_bytes {};
            for (const auto& [K_NAME, K_BINDING] : merged) {
                require(K_BINDING.m_bytes <= 64uz * K_MIB - committed_bytes,
                        "Committed record byte budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
                committed_bytes += K_BINDING.m_bytes;
            }
            std::size_t additional {};
            std::set<std::string> missing;
            for (const auto& [K_NAME, K_BINDING] : requested) {
                if (!state.m_blobs.contains(K_BINDING.m_sha) &&
                    missing.insert(K_BINDING.m_sha).second) {
                    additional += K_BINDING.m_bytes;
                }
            }
            require(additional <= 64uz * K_MIB - state.m_blob_bytes &&
                        state.m_blobs.size() + missing.size() <= 8192uz,
                    "Physical record blob budget exceeded",
                    ErrorCode::RESOURCE_EXHAUSTED);
            if (K_PRIOR == state.m_intents.end()) {
                require(state.m_intents.size() < 1024uz &&
                            K_INTENT_BYTES.size() <= 16uz * K_MIB - state.m_intent_bytes,
                        "Physical record intent budget exceeded",
                        ErrorCode::RESOURCE_EXHAUSTED);
            }
            return false;
        };
        if (K_VALIDATE()) {
            return state.m_commit;
        }
        make_directory(K_ROOT);
        auto lock = take(FileLock::acquire(K_ROOT / ".writer.lock"));
        state = inspect(K_ROOT, true, stop);
        if (K_VALIDATE()) {
            return state.m_commit;
        }
        immutable(K_ROOT / "owner.json", bytes(K_OWNER), stop);
        make_directory(K_ROOT / "blobs");
        make_directory(K_ROOT / "intents");
        immutable(K_ROOT / "intents" / (std::to_string(operation.m_value) + ".json"),
                  K_INTENT_BYTES,
                  stop);
        std::size_t count {};
        for (const auto& record : records) {
            immutable(K_ROOT / "blobs" / (requested.at(record.m_key).m_sha + ".bin"),
                      record.m_bytes,
                      stop);
            ++count;
            if (observed) {
                try {
                    observed(count);
                } catch (const std::exception& error) {
                    return fail(ErrorCode::BACKEND_FAILURE, error.what());
                } catch (...) {
                    return fail(ErrorCode::BACKEND_FAILURE, "Record observer failed");
                }
            }
            check_stop(stop);
        }
        const auto K_CURRENT = inspect(K_ROOT, false, stop);
        require(K_CURRENT.m_manifest == state.m_manifest,
                "Record manifest changed during publication",
                ErrorCode::RESYNC_REQUIRED);
        for (const auto& [K_NAME, K_BINDING] : requested) {
            state.m_records[K_NAME] = K_BINDING;
        }
        state.m_operations.push_back({ { "operation_id", operation.m_value },
                                      { "intent_sha256", K_INTENT_SHA },
                                      { "new_generation", base_generation + 1 } });
        std::ranges::sort(state.m_operations.get_ref<Json::array_t&>(), {}, [](const Json& row) {
            return integer(row.at("operation_id"));
        });
        const Json K_MANIFEST { { "schema_version", 1 },
                               { "generation", base_generation + 1 },
                               { "records", rows(state.m_records) },
                               { "operations", state.m_operations } };
        const auto K_DATA = bytes(K_MANIFEST.dump());
        require(K_DATA.size() <= K_MIB,
                "Record manifest byte budget exceeded",
                ErrorCode::RESOURCE_EXHAUSTED);
        RecordCommit commit { base_generation + 1, sha256(K_DATA) };
        bool replaced {};
        const auto K_RESULT =
            asset_files::replace_manifest(K_ROOT / "manifest.json", K_DATA, stop, replaced);
        if (!K_RESULT) {
            return fail(replaced ? ErrorCode::RESYNC_REQUIRED : K_RESULT.error().m_code,
                        K_RESULT.error().m_message);
        }
        return commit;
    } catch (const RecordFailure& error) {
        return fail(error.m_code, error.what());
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Record publication allocation failed");
    } catch (const fs::filesystem_error& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

}  // namespace world_explorer
