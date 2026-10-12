#include "map_persistence.hpp"
#include "checkpoint_generated.h"
#include "map_codec.hpp"
#include "map_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include <cerrno>
#include <nlohmann/json.hpp>
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
constexpr std::size_t MAX_MANIFEST_BYTES = std::size_t{64} * 1024;
constexpr std::size_t MAX_RECEIPTS = 65536;
std::span<const std::byte> bytes_of(const std::string& text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}
result<nlohmann::json> manifest_json(std::span<const std::byte> bytes) {
    try {
        return nlohmann::json::parse(
            reinterpret_cast<const char*>(bytes.data()),
            reinterpret_cast<const char*>(bytes.data() + bytes.size()),
            [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
                if (depth > 8)
                    throw std::invalid_argument("Manifest nesting exceeds limit");
                return true;
            });
    } catch (const std::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Invalid map manifest: ") + e.what());
    }
}
bool config_valid(const persistent_map_config& config) {
    return !config.directory.empty() &&
           config.directory.native().find(std::filesystem::path::value_type{}) ==
               std::filesystem::path::string_type::npos &&
           map_codec::limits_valid({config.checkpoint_bytes, config.records}) &&
           config.records.remembered_operations > 0 &&
           config.records.remembered_operations <= MAX_RECEIPTS &&
           config.retained_asset_bytes.bytes > 0 &&
           config.retained_asset_bytes.bytes <= std::size_t{256} * 1024 * 1024 * 1024;
}
std::filesystem::path checkpoint_path(const std::filesystem::path& root, const std::string& hash) {
    return root / "assets" / ("checkpoint-" + hash + ".fb");
}
std::filesystem::path pose_path(const std::filesystem::path& root, const std::string& hash) {
    return root / "assets" / ("poses-" + hash + ".weposes");
}
} // namespace
map_persistence::map_persistence(persistent_map_config config) : config_(std::move(config)) {}
map_persistence::~map_persistence() {
#ifdef _WIN32
    if (writer_handle_)
        CloseHandle(writer_handle_);
#else
    if (writer_descriptor_ >= 0)
        close(writer_descriptor_);
#endif
}
result<void> map_persistence::lock() {
    const auto path = config_.directory / "writer.lock";
#ifdef _WIN32
    const auto handle =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return fail(GetLastError() == ERROR_SHARING_VIOLATION ? error_code::WOULD_BLOCK
                                                              : error_code::IO_ERROR,
                    "Cannot acquire persistent map writer lock");
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(handle);
        return fail(error_code::INVALID_DATA, "Map writer lock is not a regular file");
    }
    writer_handle_ = handle;
#else
    const int descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
        return fail(error_code::IO_ERROR, "Cannot open persistent map writer lock");
    struct stat information{};
    if (fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode)) {
        close(descriptor);
        return fail(error_code::INVALID_DATA, "Map writer lock is not a regular file");
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int cause = errno;
        close(descriptor);
        return fail(cause == EWOULDBLOCK || cause == EAGAIN ? error_code::WOULD_BLOCK
                                                            : error_code::IO_ERROR,
                    "Persistent map already has a writer or locking failed");
    }
    writer_descriptor_ = descriptor;
#endif
    return {};
}
result<std::unique_ptr<map_store>> map_persistence::open(persistent_map_config config,
                                                         const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Opening map cancelled");
        if (!config_valid(config))
            return fail(error_code::INVALID_ARGUMENT, "Invalid persistent map configuration");
        if (!std::filesystem::exists(config.directory)) {
            if (!config.create_if_missing)
                return fail(error_code::NOT_FOUND, "Persistent map directory is missing");
            std::filesystem::create_directory(config.directory);
            const auto flushed = map_files::flush_directory_entry(config.directory);
            if (!flushed)
                return std::unexpected(flushed.error());
        }
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(config.directory)))
            return fail(error_code::INVALID_DATA,
                        "Persistent map root must be a nonsymlink directory");
        config.directory = std::filesystem::canonical(config.directory);
        auto persistence = std::unique_ptr<map_persistence>(new map_persistence(std::move(config)));
        const auto locked = persistence->lock();
        if (!locked)
            return std::unexpected(locked.error());
        auto store = std::make_unique<map_store>(persistence->config_.records);
        const auto root = persistence->config_.directory;
        const bool has_manifest =
            std::filesystem::exists(std::filesystem::symlink_status(root / "manifest.json"));
        if (has_manifest) {
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(root / "assets")))
                return fail(error_code::INVALID_DATA, "Map assets directory is missing or linked");
            const auto admitted = persistence->admit_asset(0);
            if (!admitted)
                return std::unexpected(admitted.error());
            const auto loaded = persistence->reload(*store, stop);
            if (!loaded)
                return std::unexpected(loaded.error());
        } else {
            if (!persistence->config_.create_if_missing)
                return fail(error_code::NOT_FOUND, "Map manifest is missing; no implicit reset");
            for (const auto& entry : std::filesystem::directory_iterator(root))
                if (entry.path().filename() != "writer.lock" && entry.path().filename() != "assets")
                    return fail(error_code::INVALID_DATA,
                                "Refusing to initialize an occupied directory");
            if (std::filesystem::exists(root / "assets")) {
                if (!std::filesystem::is_directory(
                        std::filesystem::symlink_status(root / "assets")) ||
                    !std::filesystem::is_empty(root / "assets"))
                    return fail(error_code::INVALID_DATA, "Missing manifest with existing assets; "
                                                          "preserve data and recover explicitly");
            } else
                std::filesystem::create_directory(root / "assets");
            const auto initial = persistence->publish(*store->current_, store->operations_, stop);
            if (!initial)
                return std::unexpected(initial.error());
        }
        store->persistence_ = std::move(persistence);
        return store;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Persistent map allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR,
                    std::string("Opening persistent map failed: ") + e.what());
    }
}
result<void> map_persistence::admit_asset(std::size_t bytes) {
    if (!std::filesystem::is_directory(
            std::filesystem::symlink_status(config_.directory / "assets")))
        return fail(error_code::INVALID_DATA, "Map assets directory is missing or linked");
    std::uintmax_t total{};
    std::size_t entries{};
    const auto budget = config_.retained_asset_bytes.bytes;
    for (const auto& entry : std::filesystem::directory_iterator(config_.directory / "assets")) {
        if (++entries > 4096)
            return fail(error_code::RESOURCE_EXHAUSTED, "Retained map asset count exceeds 4096");
        if (!std::filesystem::is_regular_file(entry.symlink_status()))
            return fail(error_code::INVALID_DATA,
                        "Map assets contain a nonregular or linked entry");
        const auto count = entry.file_size();
        if (count > budget - total)
            return fail(error_code::RESOURCE_EXHAUSTED, "Retained map asset byte budget exceeded");
        total += count;
    }
    if (bytes && entries >= 4096)
        return fail(error_code::RESOURCE_EXHAUSTED, "New map asset exceeds retained entry budget");
    if (bytes > budget - total)
        return fail(error_code::RESOURCE_EXHAUSTED, "New map asset exceeds retained byte budget");
    return {};
}
result<std::vector<std::byte>>
map_persistence::encode(const map_state& state, const std::deque<map_store::remembered>& receipts,
                        const std::stop_token& stop) {
    const pose_asset_limits limits{config_.checkpoint_bytes, config_.records};
    const auto valid = map_codec::validate_state(state, limits, stop);
    if (!valid)
        return std::unexpected(valid.error());
    flatbuffers::FlatBufferBuilder builder;
    const auto records = map_codec::encode_records(builder, state, limits, stop);
    if (!records)
        return std::unexpected(records.error());
    std::vector<flatbuffers::Offset<wire::receipt>> saved;
    saved.reserve(receipts.size());
    for (const auto& receipt : receipts) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Map checkpoint cancelled");
        const auto digest = builder.CreateString(receipt.digest);
        saved.push_back(
            wire::Createreceipt(builder, receipt.operation.value, receipt.revision.value, digest));
        if (builder.GetSize() > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Map checkpoint exceeds byte budget");
    }
    const auto calibrations = builder.CreateVector(records->calibrations);
    const auto observations = builder.CreateVector(records->observations);
    const auto alignments = builder.CreateVector(records->alignments);
    const auto poses = builder.CreateVector(records->poses);
    const auto invalidated = builder.CreateVector(records->invalidated);
    const auto operations = builder.CreateVector(saved);
    const auto initial = state.initial_pose_asset_sha256
                             ? builder.CreateString(*state.initial_pose_asset_sha256)
                             : 0;
    const auto root = wire::Createcheckpoint(
        builder, 1, pose_wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER,
        pose_wire::scale_provenance::ARBITRARY_MONOCULAR, state.epoch.value, state.revision.value,
        calibrations, observations, alignments, poses, invalidated, operations, initial);
    wire::FinishcheckpointBuffer(builder, root);
    if (builder.GetSize() > limits.encoded_bytes.bytes)
        return fail(error_code::RESOURCE_EXHAUSTED, "Map checkpoint exceeds byte budget");
    const auto bytes = std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize()));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}
result<void> map_persistence::reload(map_store& store, const std::stop_token& stop) {
    try {
        const auto manifest =
            map_files::read_bytes(config_.directory / "manifest.json", MAX_MANIFEST_BYTES, stop);
        if (!manifest)
            return std::unexpected(manifest.error());
        const auto parsed = manifest_json(*manifest);
        if (!parsed)
            return std::unexpected(parsed.error());
        const auto& value = *parsed;
        if (!value.is_object() || value.size() != 5 ||
            !value.at("schema_version").is_number_unsigned() || value.at("schema_version") != 1 ||
            !value.at("epoch").is_number_unsigned() || !value.at("revision").is_number_unsigned() ||
            !value.at("sha256").is_string() || !value.at("bytes").is_number_unsigned())
            return fail(error_code::INVALID_DATA, "Unsupported or malformed map manifest");
        const auto hash = value.at("sha256").get<std::string>();
        if (!map_codec::digest_valid(hash))
            return fail(error_code::INVALID_DATA, "Invalid checkpoint hash");
        const auto count = value.at("bytes").get<std::uint64_t>();
        if (!count || count > config_.checkpoint_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Checkpoint byte budget exceeded");
        const auto bytes = map_files::read_bytes(checkpoint_path(config_.directory, hash),
                                                 config_.checkpoint_bytes.bytes, stop);
        if (!bytes)
            return std::unexpected(bytes.error());
        if (bytes->size() != count || sha256(*bytes) != hash)
            return fail(error_code::INVALID_DATA, "Checkpoint checksum or size mismatch");
        flatbuffers::Verifier verifier(
            reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size(), 16,
            static_cast<flatbuffers::uoffset_t>(config_.records.max_frames * 2 +
                                                config_.records.max_calibrations +
                                                config_.records.max_submaps + MAX_RECEIPTS + 1));
        if (!wire::VerifycheckpointBuffer(verifier))
            return fail(error_code::INVALID_DATA, "Malformed map checkpoint");
        const auto* root = wire::Getcheckpoint(bytes->data());
        if (root->schema_version() != 1 || root->digest_profile() != 1 ||
            root->coordinates() != pose_wire::coordinate_profile::RIGHT_Y_UP_OPTICAL_WXYZ_INTEGER ||
            root->scale() != pose_wire::scale_provenance::ARBITRARY_MONOCULAR ||
            root->epoch() != value.at("epoch").get<std::uint64_t>() ||
            root->revision() != value.at("revision").get<std::uint64_t>())
            return fail(error_code::INVALID_DATA,
                        "Unsupported or inconsistent map checkpoint profile");
        const auto state = map_codec::decode_records(
            {root->epoch(), root->revision(), root->calibrations(), root->observations(),
             root->alignments(), root->poses(), root->invalidated_frames()},
            {config_.checkpoint_bytes, config_.records}, stop);
        if (!state)
            return std::unexpected(state.error());
        auto next = std::make_shared<map_state>(**state);
        if (root->initial_pose_asset_sha256()) {
            if (!map_codec::digest_valid(root->initial_pose_asset_sha256()->string_view()))
                return fail(error_code::INVALID_DATA, "Invalid initial pose dependency");
            next->initial_pose_asset_sha256 = root->initial_pose_asset_sha256()->str();
            const auto dependency =
                read_pose_asset(pose_path(config_.directory, *next->initial_pose_asset_sha256),
                                *next->initial_pose_asset_sha256, {}, stop);
            if (!dependency)
                return std::unexpected(dependency.error());
            if (next->epoch.value || dependency->snapshot->epoch.value ||
                dependency->snapshot->revision.value != 1)
                return fail(error_code::INVALID_DATA,
                            "Unsupported initial canonical dependency epoch/revision");
        }
        if (root->receipts()->size() > config_.records.remembered_operations)
            return fail(error_code::RESOURCE_EXHAUSTED,
                        "Stored retry window exceeds configured receipt budget; do not silently "
                        "discard it");
        std::deque<map_store::remembered> receipts;
        std::set<operation_id> ids;
        std::uint64_t previous{};
        for (const auto* receipt : *root->receipts()) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Map recovery cancelled");
            if (!receipt->operation() || receipt->revision() <= previous ||
                (previous && receipt->revision() != previous + 1) ||
                receipt->revision() > next->revision.value ||
                !map_codec::digest_valid(receipt->digest()->string_view()) ||
                !ids.emplace(operation_id{receipt->operation()}).second)
                return fail(error_code::INVALID_DATA, "Invalid duplicate or unordered map receipt");
            previous = receipt->revision();
            receipts.push_back(
                {{receipt->operation()}, receipt->digest()->str(), {receipt->revision()}});
        }
        if ((next->revision.value != 0 &&
             (receipts.empty() || receipts.back().revision != next->revision)) ||
            (!next->revision.value && !receipts.empty()))
            return fail(error_code::INVALID_DATA,
                        "Map receipt tail does not identify the committed revision");
        store.current_ = std::move(next);
        store.operations_ = std::move(receipts);
        manifest_hash_ = sha256(*manifest);
        return {};
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Map recovery allocation failed");
    } catch (const nlohmann::json::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Malformed map manifest: ") + e.what());
    }
}
result<void> map_persistence::install_pose_asset(const std::filesystem::path& path,
                                                 const std::string& hash,
                                                 const std::stop_token& stop) {
    const auto bytes = map_files::read_bytes(path, std::size_t{256} * 1024 * 1024, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    const auto checked = decode_pose_asset(*bytes, hash, {}, stop);
    if (!checked)
        return std::unexpected(checked.error());
    const auto destination = pose_path(config_.directory, hash);
    const auto existing_budget = admit_asset(0);
    if (!existing_budget)
        return std::unexpected(existing_budget.error());
    if (std::filesystem::exists(std::filesystem::symlink_status(destination))) {
        const auto existing = read_pose_asset(destination, hash, {}, stop);
        if (!existing)
            return std::unexpected(existing.error());
        return {};
    }
    const auto admitted = admit_asset(bytes->size());
    if (!admitted)
        return std::unexpected(admitted.error());
    const auto written = map_files::publish_asset(destination, *bytes, stop);
    if (!written)
        return std::unexpected(written.error());
    return {};
}
result<void> map_persistence::publish(const map_state& state,
                                      const std::deque<map_store::remembered>& receipts,
                                      const std::stop_token& stop) {
    if (uncertain_)
        return fail(error_code::RESYNC_REQUIRED, "Map publication outcome requires close/reopen");
    const auto existing_budget = admit_asset(0);
    if (!existing_budget)
        return std::unexpected(existing_budget.error());
    const auto manifest = config_.directory / "manifest.json";
    if (!manifest_hash_.empty()) {
        const auto current = map_files::read_bytes(manifest, MAX_MANIFEST_BYTES, stop);
        if (!current)
            return std::unexpected(current.error());
        if (sha256(*current) != manifest_hash_)
            return fail(error_code::RESYNC_REQUIRED,
                        "Map manifest changed outside the owning writer");
    }
    const auto bytes = encode(state, receipts, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    const auto hash = sha256(*bytes);
    const auto path = checkpoint_path(config_.directory, hash);
    if (std::filesystem::exists(std::filesystem::symlink_status(path))) {
        const auto prior = map_files::read_bytes(path, config_.checkpoint_bytes.bytes, stop);
        if (!prior)
            return std::unexpected(prior.error());
        if (*prior != *bytes)
            return fail(error_code::INVALID_DATA,
                        "Existing checkpoint is corrupt; never overwrite");
    } else {
        const auto admitted = admit_asset(bytes->size());
        if (!admitted)
            return std::unexpected(admitted.error());
        const auto written = map_files::publish_asset(path, *bytes, stop);
        if (!written)
            return std::unexpected(written.error());
    }
    auto text = nlohmann::json{{"schema_version", 1},
                               {"epoch", state.epoch.value},
                               {"revision", state.revision.value},
                               {"sha256", hash},
                               {"bytes", bytes->size()}}
                    .dump(2);
    auto next_hash = sha256(bytes_of(text)); // All allocations occur before replacing the manifest.
    bool replaced{};
    const auto result = map_files::replace_manifest(manifest, bytes_of(text), stop, replaced);
    if (!result) {
        if (replaced) {
            uncertain_ = true;
            return fail(error_code::RESYNC_REQUIRED,
                        "Manifest replaced but durability flush failed; close/reopen to resolve "
                        "commit outcome");
        }
        return std::unexpected(result.error());
    }
    manifest_hash_.swap(next_hash);
    return {};
}
} // namespace world_explorer
