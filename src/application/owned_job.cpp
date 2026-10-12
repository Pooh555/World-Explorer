#include "owned_job.hpp"
#include "job_validation.hpp"
#include "world_explorer/core/hash.hpp"
#include <fstream>

namespace world_explorer::job_detail {
namespace {
constexpr std::size_t MAX_JSON = std::size_t{64} * 1024;
result<bool> check_directory(const std::filesystem::path& root, const job_file_names& names,
                             const std::string& owner_hash, const std::set<std::string>& allowed) {
    if (!std::filesystem::exists(std::filesystem::symlink_status(root)))
        return false;
    if (std::filesystem::is_symlink(root) || !std::filesystem::is_directory(root))
        return fail(error_code::INVALID_DATA, "Worker job must be a local directory");
    const auto owner = root / names.owner;
    const bool owned = std::filesystem::exists(std::filesystem::symlink_status(owner));
    if (owned) {
        if (std::filesystem::is_symlink(owner))
            return fail(error_code::INVALID_DATA, "Job owner may not be a symlink");
        const auto checked = read_json(owner, MAX_JSON, owner_hash);
        if (!checked)
            return fail(error_code::INVALID_DATA, "Job belongs to different inputs/configuration "
                                                  "or is corrupt; select a fresh directory");
    }
    std::size_t entries{};
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().string();
        if (++entries > 16 || entry.is_symlink() || !allowed.contains(name) ||
            (!owned && name != names.lock) ||
            (name == "stages" ? !entry.is_directory() : !entry.is_regular_file()))
            return fail(
                error_code::INVALID_DATA,
                "Job output is unowned or contains unexpected entries; select a fresh directory");
    }
    return owned;
}
} // namespace
result<void> publish_owned_json(const std::filesystem::path& path, const nlohmann::json& value,
                                const std::stop_token& stop) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Job metadata publication cancelled");
    const auto text = value.dump(2) + '\n';
    if (text.size() > MAX_JSON)
        return fail(error_code::RESOURCE_EXHAUSTED, "Job metadata exceeds 64 KiB");
    if (std::filesystem::exists(std::filesystem::symlink_status(path))) {
        if (std::filesystem::is_symlink(path))
            return fail(error_code::INVALID_DATA, "Job metadata may not be a symlink");
        const auto checked = read_json(path, MAX_JSON, sha256(std::as_bytes(std::span(text))));
        if (!checked)
            return std::unexpected(checked.error());
        return {};
    }
    auto pending = path;
    pending.replace_extension(".pending");
    if (std::filesystem::exists(std::filesystem::symlink_status(pending)))
        return fail(error_code::INVALID_DATA,
                    "Incomplete job metadata retained; select a fresh directory");
    std::ofstream output(pending, std::ios::binary);
    if (!output)
        return fail(error_code::IO_ERROR, "Cannot create job metadata");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output)
        return fail(error_code::IO_ERROR, "Cannot write job metadata");
    std::filesystem::rename(pending, path);
    return {};
}
result<std::unique_ptr<file_lock>> open_owned_job(const std::filesystem::path& directory,
                                                  const job_file_names& names,
                                                  const nlohmann::json& owner,
                                                  std::set<std::string> allowed_entries,
                                                  const std::stop_token& stop) {
    const auto text = owner.dump(2) + '\n';
    const auto owner_hash = sha256(std::as_bytes(std::span(text)));
    allowed_entries.insert(names.owner);
    allowed_entries.insert(names.lock);
    const auto admitted = check_directory(directory, names, owner_hash, allowed_entries);
    if (!admitted)
        return std::unexpected(admitted.error());
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Job cancelled before ownership");
    std::filesystem::create_directories(directory);
    auto lock = file_lock::acquire(directory / names.lock);
    if (!lock)
        return std::unexpected(lock.error());
    const auto locked = check_directory(directory, names, owner_hash, allowed_entries);
    if (!locked)
        return std::unexpected(locked.error());
    if (!*locked) {
        const auto published = publish_owned_json(directory / names.owner, owner, stop);
        if (!published)
            return std::unexpected(published.error());
    }
    return std::move(*lock);
}
} // namespace world_explorer::job_detail
