#pragma once

#include "world_explorer/core/file_lock.hpp"

#include <nlohmann/json.hpp>

#include <set>
#include <stop_token>

namespace world_explorer::job_detail {

struct job_file_names {
    std::string owner, lock;
};

// Admission is checked before creating a lock, then checked again under it.
// Only an empty directory or an exact immutable owner may be admitted.
Result<std::unique_ptr<FileLock>> open_owned_job(const std::filesystem::path& directory,
                                                 const job_file_names& names,
                                                 const nlohmann::json& owner,
                                                 std::set<std::string> allowed_entries,
                                                 const std::stop_token& stop);
// Called with the lifetime writer lock. Existing bytes must match exactly.
Result<void> publish_owned_json(const std::filesystem::path& path,
                                const nlohmann::json& value,
                                const std::stop_token& stop);

}  // namespace world_explorer::job_detail
