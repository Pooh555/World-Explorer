#include "owned_job.hpp"

#include "job_validation.hpp"
#include "world_explorer/core/hash.hpp"

#include <fstream>

namespace world_explorer::job_detail {
namespace {

constexpr std::size_t K_MAX_JSON { 64uz * 1024uz };

Result<bool> check_directory(const std::filesystem::path& root,
                             const job_file_names& names,
                             const std::string& owner_hash,
                             const std::set<std::string>& allowed) {
    if (!std::filesystem::exists(std::filesystem::symlink_status(root))) {
        return false;
    }
    if (std::filesystem::is_symlink(root) || !std::filesystem::is_directory(root)) {
        return fail(ErrorCode::INVALID_DATA, "Worker job must be a local directory");
    }
    const auto K_OWNER = root / names.owner;
    const bool K_OWNED = std::filesystem::exists(std::filesystem::symlink_status(K_OWNER));
    if (K_OWNED) {
        if (std::filesystem::is_symlink(K_OWNER)) {
            return fail(ErrorCode::INVALID_DATA, "Job owner may not be a symlink");
        }
        const auto K_CHECKED = read_json(K_OWNER, K_MAX_JSON, owner_hash);
        if (!K_CHECKED) {
            return fail(ErrorCode::INVALID_DATA,
                        "Job belongs to different inputs/configuration "
                        "or is corrupt; select a fresh directory");
        }
    }
    std::size_t entries {};
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const auto K_NAME = entry.path().filename().string();
        if (++entries > 16 || entry.is_symlink() || !allowed.contains(K_NAME) ||
            (!K_OWNED && K_NAME != names.lock) ||
            (K_NAME == "stages" ? !entry.is_directory() : !entry.is_regular_file())) {
            return fail(
                ErrorCode::INVALID_DATA,
                "Job output is unowned or contains unexpected entries; select a fresh directory");
        }
    }
    return K_OWNED;
}

}  // namespace

Result<void> publish_owned_json(const std::filesystem::path& path,
                                const nlohmann::json& value,
                                const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Job metadata publication cancelled");
    }
    const auto K_TEXT = value.dump(2) + '\n';
    if (K_TEXT.size() > K_MAX_JSON) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Job metadata exceeds 64 KiB");
    }
    if (std::filesystem::exists(std::filesystem::symlink_status(path))) {
        if (std::filesystem::is_symlink(path)) {
            return fail(ErrorCode::INVALID_DATA, "Job metadata may not be a symlink");
        }
        const auto K_CHECKED =
            read_json(path, K_MAX_JSON, sha256(std::as_bytes(std::span(K_TEXT))));
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        return {};
    }
    auto pending = path;
    pending.replace_extension(".pending");
    if (std::filesystem::exists(std::filesystem::symlink_status(pending))) {
        return fail(ErrorCode::INVALID_DATA,
                    "Incomplete job metadata retained; select a fresh directory");
    }
    std::ofstream output(pending, std::ios::binary);
    if (!output) {
        return fail(ErrorCode::IO_ERROR, "Cannot create job metadata");
    }
    output.write(K_TEXT.data(), static_cast<std::streamsize>(K_TEXT.size()));
    output.close();
    if (!output) {
        return fail(ErrorCode::IO_ERROR, "Cannot write job metadata");
    }
    std::filesystem::rename(pending, path);
    return {};
}

Result<std::unique_ptr<FileLock>> open_owned_job(const std::filesystem::path& directory,
                                                 const job_file_names& names,
                                                 const nlohmann::json& owner,
                                                 std::set<std::string> allowed_entries,
                                                 const std::stop_token& stop) {
    const auto K_TEXT = owner.dump(2) + '\n';
    const auto K_OWNER_HASH = sha256(std::as_bytes(std::span(K_TEXT)));
    allowed_entries.insert(names.owner);
    allowed_entries.insert(names.lock);
    const auto K_ADMITTED = check_directory(directory, names, K_OWNER_HASH, allowed_entries);
    if (!K_ADMITTED) {
        return std::unexpected(K_ADMITTED.error());
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Job cancelled before ownership");
    }
    std::filesystem::create_directories(directory);
    auto lock = FileLock::acquire(directory / names.lock);
    if (!lock) {
        return std::unexpected(lock.error());
    }
    const auto K_LOCKED = check_directory(directory, names, K_OWNER_HASH, allowed_entries);
    if (!K_LOCKED) {
        return std::unexpected(K_LOCKED.error());
    }
    if (!*K_LOCKED) {
        const auto K_PUBLISHED = publish_owned_json(directory / names.owner, owner, stop);
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
    }
    return std::move(*lock);
}

}  // namespace world_explorer::job_detail
