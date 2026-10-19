#include "map_persistence.hpp"

#include "checkpoint_generated.h"
#include "map_codec.hpp"
#include "map_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <set>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace world_explorer {
namespace {
namespace wire = map_wire;
constexpr std::size_t K_MAX_MANIFEST_BYTES { 64uz * 1024uz };
constexpr std::size_t K_MAX_RECEIPTS { 65536uz };

std::span<const std::byte> bytes_of(const std::string& text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

Result<nlohmann::json> manifest_json(std::span<const std::byte> bytes) {
    try {
        return nlohmann::json::parse(
            reinterpret_cast<const char*>(bytes.data()),
            reinterpret_cast<const char*>(bytes.data() + bytes.size()),
            [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
                if (depth > 8) {
                    throw std::invalid_argument("Manifest nesting exceeds limit");
                }
                return true;
            });
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Invalid map manifest: ") + e.what());
    }
}

bool config_valid(const PersistentMapConfig& config) {
    return !config.m_directory.empty() &&
           config.m_directory.native().find(std::filesystem::path::value_type {}) ==
               std::filesystem::path::string_type::npos &&
           map_codec::limits_valid({ config.m_checkpoint_bytes, config.m_records }) &&
           config.m_records.m_remembered_operations > 0 &&
           config.m_records.m_remembered_operations <= K_MAX_RECEIPTS &&
           config.m_retained_asset_bytes.m_bytes > 0 &&
           config.m_retained_asset_bytes.m_bytes <= 256uz * 1024 * 1024 * 1024;
}

std::filesystem::path checkpoint_path(const std::filesystem::path& root, const std::string& hash) {
    return root / "assets" / ("checkpoint-" + hash + ".fb");
}

std::filesystem::path pose_path(const std::filesystem::path& root, const std::string& hash) {
    return root / "assets" / ("poses-" + hash + ".weposes");
}

}  // namespace

MapPersistence::MapPersistence(PersistentMapConfig config) : config_(std::move(config)) { }

MapPersistence::~MapPersistence() {
#ifdef _WIN32
    if (writer_handle_)
        CloseHandle(writer_handle_);
#else
    if (writer_descriptor_ >= 0) {
        close(writer_descriptor_);
    }
#endif
}

Result<void> MapPersistence::lock() {
    const auto K_PATH = config_.m_directory / "writer.lock";
#ifdef _WIN32
    const auto handle = CreateFileW(K_PATH.c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                    nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return fail(GetLastError() == ERROR_SHARING_VIOLATION ? ErrorCode::WOULD_BLOCK
                                                              : ErrorCode::IO_ERROR,
                    "Cannot acquire persistent map writer lock");
    BY_HANDLE_FILE_INFORMATION information {};
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(handle);
        return fail(ErrorCode::INVALID_DATA, "Map writer lock is not a regular file");
    }
    writer_handle_ = handle;
#else
    const int K_DESCRIPTOR =
        ::open(K_PATH.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (K_DESCRIPTOR < 0) {
        return fail(ErrorCode::IO_ERROR, "Cannot open persistent map writer lock");
    }
    struct stat information {};
    if (fstat(K_DESCRIPTOR, &information) != 0 || !S_ISREG(information.st_mode)) {
        close(K_DESCRIPTOR);
        return fail(ErrorCode::INVALID_DATA, "Map writer lock is not a regular file");
    }
    if (flock(K_DESCRIPTOR, LOCK_EX | LOCK_NB) != 0) {
        const int K_CAUSE = errno;
        close(K_DESCRIPTOR);
        return fail(K_CAUSE == EWOULDBLOCK || K_CAUSE == EAGAIN ? ErrorCode::WOULD_BLOCK
                                                                : ErrorCode::IO_ERROR,
                    "Persistent map already has a writer or locking failed");
    }
    writer_descriptor_ = K_DESCRIPTOR;
#endif
    return {};
}

Result<std::unique_ptr<MapStore>> MapPersistence::open(PersistentMapConfig config,
                                                       const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Opening map cancelled");
        }
        if (!config_valid(config)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid persistent map configuration");
        }
        if (!std::filesystem::exists(config.m_directory)) {
            if (!config.m_create_if_missing) {
                return fail(ErrorCode::NOT_FOUND, "Persistent map directory is missing");
            }
            std::filesystem::create_directory(config.m_directory);
            const auto K_FLUSHED = map_files::flush_directory_entry(config.m_directory);
            if (!K_FLUSHED) {
                return std::unexpected(K_FLUSHED.error());
            }
        }
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(config.m_directory))) {
            return fail(ErrorCode::INVALID_DATA,
                        "Persistent map root must be a nonsymlink directory");
        }
        config.m_directory = std::filesystem::canonical(config.m_directory);
        auto persistence = std::unique_ptr<MapPersistence>(new MapPersistence(std::move(config)));
        const auto K_LOCKED = persistence->lock();
        if (!K_LOCKED) {
            return std::unexpected(K_LOCKED.error());
        }
        auto store = std::make_unique<MapStore>(persistence->config_.m_records);
        const auto K_ROOT = persistence->config_.m_directory;
        const bool K_HAS_MANIFEST =
            std::filesystem::exists(std::filesystem::symlink_status(K_ROOT / "manifest.json"));
        if (K_HAS_MANIFEST) {
            if (!std::filesystem::is_directory(
                    std::filesystem::symlink_status(K_ROOT / "assets"))) {
                return fail(ErrorCode::INVALID_DATA, "Map assets directory is missing or linked");
            }
            const auto K_ADMITTED = persistence->admit_asset(0);
            if (!K_ADMITTED) {
                return std::unexpected(K_ADMITTED.error());
            }
            const auto K_LOADED = persistence->reload(*store, stop);
            if (!K_LOADED) {
                return std::unexpected(K_LOADED.error());
            }
        } else {
            if (!persistence->config_.m_create_if_missing) {
                return fail(ErrorCode::NOT_FOUND, "Map manifest is missing; no implicit reset");
            }
            for (const auto& entry : std::filesystem::directory_iterator(K_ROOT)) {
                if (entry.path().filename() != "writer.lock" &&
                    entry.path().filename() != "assets") {
                    return fail(ErrorCode::INVALID_DATA,
                                "Refusing to initialize an occupied directory");
                }
            }
            if (std::filesystem::exists(K_ROOT / "assets")) {
                if (!std::filesystem::is_directory(
                        std::filesystem::symlink_status(K_ROOT / "assets")) ||
                    !std::filesystem::is_empty(K_ROOT / "assets")) {
                    return fail(ErrorCode::INVALID_DATA,
                                "Missing manifest with existing assets; "
                                "preserve data and recover explicitly");
                }
            } else {
                std::filesystem::create_directory(K_ROOT / "assets");
            }
            const auto K_INITIAL =
                persistence->publish(*store->m_current, store->m_operations, stop);
            if (!K_INITIAL) {
                return std::unexpected(K_INITIAL.error());
            }
        }
        store->m_persistence = std::move(persistence);
        return store;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Persistent map allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Opening persistent map failed: ") + e.what());
    }
}

Result<void> MapPersistence::admit_asset(std::size_t bytes) {
    if (!std::filesystem::is_directory(
            std::filesystem::symlink_status(config_.m_directory / "assets"))) {
        return fail(ErrorCode::INVALID_DATA, "Map assets directory is missing or linked");
    }
    std::uintmax_t total {};
    std::size_t entries {};
    const auto K_BUDGET = config_.m_retained_asset_bytes.m_bytes;
    for (const auto& entry : std::filesystem::directory_iterator(config_.m_directory / "assets")) {
        if (++entries > 4096) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Retained map asset count exceeds 4096");
        }
        if (!std::filesystem::is_regular_file(entry.symlink_status())) {
            return fail(ErrorCode::INVALID_DATA, "Map assets contain a nonregular or linked entry");
        }
        const auto K_COUNT = entry.file_size();
        if (K_COUNT > K_BUDGET - total) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Retained map asset byte budget exceeded");
        }
        total += K_COUNT;
    }
    if (bytes && entries >= 4096) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "New map asset exceeds retained entry budget");
    }
    if (bytes > K_BUDGET - total) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "New map asset exceeds retained byte budget");
    }
    return {};
}

Result<std::vector<std::byte>>
MapPersistence::encode(const MapState& state,
                       const std::deque<MapStore::Remembered>& receipts,
                       const std::stop_token& stop) {
    const PoseAssetLimits K_LIMITS { config_.m_checkpoint_bytes, config_.m_records };
    const auto K_VALID = map_codec::validate_state(state, K_LIMITS, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    flatbuffers::FlatBufferBuilder builder;
    const auto K_RECORDS = map_codec::encode_records(builder, state, K_LIMITS, stop);
    if (!K_RECORDS) {
        return std::unexpected(K_RECORDS.error());
    }
    std::vector<flatbuffers::Offset<wire::receipt>> saved;
    saved.reserve(receipts.size());
    for (const auto& receipt : receipts) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Map checkpoint cancelled");
        }
        const auto K_DIGEST = builder.CreateString(receipt.m_digest);
        saved.push_back(wire::Createreceipt(
            builder, receipt.m_operation.m_value, receipt.m_revision.m_value, K_DIGEST));
        if (builder.GetSize() > K_LIMITS.m_encoded_bytes.m_bytes) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map checkpoint exceeds byte budget");
        }
    }
    const auto K_CALIBRATIONS = builder.CreateVector(K_RECORDS->calibrations);
    const auto K_OBSERVATIONS = builder.CreateVector(K_RECORDS->observations);
    const auto K_ALIGNMENTS = builder.CreateVector(K_RECORDS->alignments);
    const auto K_POSES = builder.CreateVector(K_RECORDS->poses);
    const auto K_INVALIDATED = builder.CreateVector(K_RECORDS->invalidated);
    const auto K_OPERATIONS = builder.CreateVector(saved);
    const auto K_INITIAL = state.m_initial_pose_asset_sha256
                               ? builder.CreateString(*state.m_initial_pose_asset_sha256)
                               : 0;
    const auto K_ROOT =
        wire::Createcheckpoint(builder,
                               1,
                               pose_wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER,
                               pose_wire::scale_provenance::ARBITRARY_MONOCULAR,
                               state.m_epoch.m_value,
                               state.m_revision.m_value,
                               K_CALIBRATIONS,
                               K_OBSERVATIONS,
                               K_ALIGNMENTS,
                               K_POSES,
                               K_INVALIDATED,
                               K_OPERATIONS,
                               K_INITIAL);
    wire::FinishcheckpointBuffer(builder, K_ROOT);
    if (builder.GetSize() > K_LIMITS.m_encoded_bytes.m_bytes) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map checkpoint exceeds byte budget");
    }
    const auto K_BYTES = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return std::vector<std::byte>(K_BYTES.begin(), K_BYTES.end());
}

Result<void> MapPersistence::reload(MapStore& store, const std::stop_token& stop) {
    try {
        const auto K_MANIFEST = map_files::read_bytes(
            config_.m_directory / "manifest.json", K_MAX_MANIFEST_BYTES, stop);
        if (!K_MANIFEST) {
            return std::unexpected(K_MANIFEST.error());
        }
        const auto K_PARSED = manifest_json(*K_MANIFEST);
        if (!K_PARSED) {
            return std::unexpected(K_PARSED.error());
        }
        const auto& value = *K_PARSED;
        if (!value.is_object() || value.size() != 5 ||
            !value.at("schema_version").is_number_unsigned() || value.at("schema_version") != 1 ||
            !value.at("epoch").is_number_unsigned() || !value.at("revision").is_number_unsigned() ||
            !value.at("sha256").is_string() || !value.at("bytes").is_number_unsigned()) {
            return fail(ErrorCode::INVALID_DATA, "Unsupported or malformed map manifest");
        }
        const auto K_HASH = value.at("sha256").get<std::string>();
        if (!map_codec::digest_valid(K_HASH)) {
            return fail(ErrorCode::INVALID_DATA, "Invalid checkpoint hash");
        }
        const auto K_COUNT = value.at("bytes").get<std::uint64_t>();
        if (!K_COUNT || K_COUNT > config_.m_checkpoint_bytes.m_bytes) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Checkpoint byte budget exceeded");
        }
        const auto K_BYTES = map_files::read_bytes(
            checkpoint_path(config_.m_directory, K_HASH), config_.m_checkpoint_bytes.m_bytes, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        if (K_BYTES->size() != K_COUNT || sha256(*K_BYTES) != K_HASH) {
            return fail(ErrorCode::INVALID_DATA, "Checkpoint checksum or size mismatch");
        }
        flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(K_BYTES->data()),
                                       K_BYTES->size(),
                                       16,
                                       static_cast<flatbuffers::uoffset_t>(
                                           config_.m_records.m_max_frames * 2 +
                                           config_.m_records.m_max_calibrations +
                                           config_.m_records.m_max_submaps + K_MAX_RECEIPTS + 1));
        if (!wire::VerifycheckpointBuffer(verifier)) {
            return fail(ErrorCode::INVALID_DATA, "Malformed map checkpoint");
        }
        const auto* root = wire::Getcheckpoint(K_BYTES->data());
        if (root->schema_version() != 1 || root->digest_profile() != 1 ||
            root->coordinates() != pose_wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER ||
            root->scale() != pose_wire::scale_provenance::ARBITRARY_MONOCULAR ||
            root->epoch() != value.at("epoch").get<std::uint64_t>() ||
            root->revision() != value.at("revision").get<std::uint64_t>()) {
            return fail(ErrorCode::INVALID_DATA,
                        "Unsupported or inconsistent map checkpoint profile");
        }
        const auto K_STATE =
            map_codec::decode_records({ root->epoch(),
                                       root->revision(),
                                       root->calibrations(),
                                       root->observations(),
                                       root->alignments(),
                                       root->poses(),
                                       root->invalidated_frames() },
                                      { config_.m_checkpoint_bytes, config_.m_records },
                                      stop);
        if (!K_STATE) {
            return std::unexpected(K_STATE.error());
        }
        auto next = std::make_shared<MapState>(**K_STATE);
        if (root->initial_pose_asset_sha256()) {
            if (!next->m_revision.m_value ||
                !map_codec::digest_valid(root->initial_pose_asset_sha256()->string_view())) {
                return fail(ErrorCode::INVALID_DATA, "Invalid initial pose dependency");
            }
            next->m_initial_pose_asset_sha256 = root->initial_pose_asset_sha256()->str();
            const auto K_DEPENDENCY =
                read_pose_asset(pose_path(config_.m_directory, *next->m_initial_pose_asset_sha256),
                                *next->m_initial_pose_asset_sha256,
                                {},
                                stop);
            if (!K_DEPENDENCY) {
                return std::unexpected(K_DEPENDENCY.error());
            }
            if (next->m_epoch.m_value || K_DEPENDENCY->m_snapshot->m_epoch.m_value ||
                K_DEPENDENCY->m_snapshot->m_revision.m_value != 1) {
                return fail(ErrorCode::INVALID_DATA,
                            "Unsupported initial canonical dependency epoch/revision");
            }
        }
        if (root->receipts()->size() > config_.m_records.m_remembered_operations) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED,
                        "Stored retry window exceeds configured receipt budget; do not silently "
                        "discard it");
        }
        std::deque<MapStore::Remembered> receipts;
        std::set<OperationId> ids;
        std::uint64_t previous {};
        for (const auto* receipt : *root->receipts()) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Map recovery cancelled");
            }
            if (!receipt->operation() || receipt->revision() <= previous ||
                (previous && receipt->revision() != previous + 1) ||
                receipt->revision() > next->m_revision.m_value ||
                !map_codec::digest_valid(receipt->digest()->string_view()) ||
                !ids.emplace(OperationId { receipt->operation() }).second) {
                return fail(ErrorCode::INVALID_DATA, "Invalid duplicate or unordered map receipt");
            }
            previous = receipt->revision();
            receipts.push_back(
                { { receipt->operation() }, receipt->digest()->str(), { receipt->revision() } });
        }
        if ((next->m_revision.m_value != 0 &&
             (receipts.empty() || receipts.back().m_revision != next->m_revision)) ||
            (!next->m_revision.m_value && !receipts.empty())) {
            return fail(ErrorCode::INVALID_DATA,
                        "Map receipt tail does not identify the committed revision");
        }
        store.m_current = std::move(next);
        store.m_operations = std::move(receipts);
        manifest_hash_ = sha256(*K_MANIFEST);
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map recovery allocation failed");
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Malformed map manifest: ") + e.what());
    }
}

Result<void> MapPersistence::install_pose_asset(std::span<const std::byte> bytes,
                                                const std::string& hash,
                                                const std::stop_token& stop) {
    const auto K_CHECKED = decode_pose_asset(bytes, hash, {}, stop);
    if (!K_CHECKED) {
        return std::unexpected(K_CHECKED.error());
    }
    const auto K_DESTINATION = pose_path(config_.m_directory, hash);
    const auto K_EXISTING_BUDGET = admit_asset(0);
    if (!K_EXISTING_BUDGET) {
        return std::unexpected(K_EXISTING_BUDGET.error());
    }
    if (std::filesystem::exists(std::filesystem::symlink_status(K_DESTINATION))) {
        const auto K_EXISTING = read_pose_asset(K_DESTINATION, hash, {}, stop);
        if (!K_EXISTING) {
            return std::unexpected(K_EXISTING.error());
        }
        return {};
    }
    const auto K_ADMITTED = admit_asset(bytes.size());
    if (!K_ADMITTED) {
        return std::unexpected(K_ADMITTED.error());
    }
    const auto K_WRITTEN = map_files::publish_asset(K_DESTINATION, bytes, stop);
    if (!K_WRITTEN) {
        return std::unexpected(K_WRITTEN.error());
    }
    return {};
}

Result<void> MapPersistence::publish(const MapState& state,
                                     const std::deque<MapStore::Remembered>& receipts,
                                     const std::stop_token& stop) {
    if (uncertain_) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Map publication outcome requires close/reopen");
    }
    const auto K_EXISTING_BUDGET = admit_asset(0);
    if (!K_EXISTING_BUDGET) {
        return std::unexpected(K_EXISTING_BUDGET.error());
    }
    const auto K_MANIFEST = config_.m_directory / "manifest.json";
    if (!manifest_hash_.empty()) {
        const auto K_CURRENT = map_files::read_bytes(K_MANIFEST, K_MAX_MANIFEST_BYTES, stop);
        if (!K_CURRENT) {
            return std::unexpected(K_CURRENT.error());
        }
        if (sha256(*K_CURRENT) != manifest_hash_) {
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "Map manifest changed outside the owning writer");
        }
    }
    const auto K_BYTES = encode(state, receipts, stop);
    if (!K_BYTES) {
        return std::unexpected(K_BYTES.error());
    }
    const auto K_HASH = sha256(*K_BYTES);
    const auto K_PATH = checkpoint_path(config_.m_directory, K_HASH);
    if (std::filesystem::exists(std::filesystem::symlink_status(K_PATH))) {
        const auto K_PRIOR =
            map_files::read_bytes(K_PATH, config_.m_checkpoint_bytes.m_bytes, stop);
        if (!K_PRIOR) {
            return std::unexpected(K_PRIOR.error());
        }
        if (*K_PRIOR != *K_BYTES) {
            return fail(ErrorCode::INVALID_DATA, "Existing checkpoint is corrupt; never overwrite");
        }
    } else {
        const auto K_ADMITTED = admit_asset(K_BYTES->size());
        if (!K_ADMITTED) {
            return std::unexpected(K_ADMITTED.error());
        }
        const auto K_WRITTEN = map_files::publish_asset(K_PATH, *K_BYTES, stop);
        if (!K_WRITTEN) {
            return std::unexpected(K_WRITTEN.error());
        }
    }
    auto text = nlohmann::json { { "schema_version", 1 },
                                { "epoch", state.m_epoch.m_value },
                                { "revision", state.m_revision.m_value },
                                { "sha256", K_HASH },
                                { "bytes", K_BYTES->size() } }
                    .dump(2);
    auto next_hash =
        sha256(bytes_of(text));  // All allocations occur before replacing the manifest.
    bool replaced {};
    const auto K_RESULT = map_files::replace_manifest(K_MANIFEST, bytes_of(text), stop, replaced);
    if (!K_RESULT) {
        if (replaced) {
            uncertain_ = true;
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "Manifest replaced but durability flush failed; close/reopen to resolve "
                        "commit outcome");
        }
        return std::unexpected(K_RESULT.error());
    }
    manifest_hash_.swap(next_hash);
    return {};
}

}  // namespace world_explorer
