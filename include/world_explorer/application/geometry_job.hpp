#pragma once

#include "world_explorer/application/commands.hpp"

namespace world_explorer {

// Own retained-cache solving/reuse or explicit fresh CUDA depth and native adoption.
// The pinned request selects local scripts/interpreter; output contains separate
// depth/cache/solver/native attempts. Call off the UI thread. No walking claim.
Result<std::filesystem::path> run_geometry_job(const std::filesystem::path& request,
                                               const std::string& expected_sha256,
                                               const std::filesystem::path& destination,
                                               const std::stop_token& stop = {},
                                               const LogCallback& log = {});

// Adopt one externally pinned terminal retained-cache job, convert in an isolated
// process, admit against current poses and publish a new native scene bundle.
// Incomplete native attempts remain immutable; replay checks the committed bundle.
// Call off the UI thread. Cancellation before bundle publication retains partial
// output; publication is a commit boundary. No metric geometry or walking claim.
Result<std::filesystem::path> adopt_geometry_job(const std::filesystem::path& request,
                                                 const std::string& expected_sha256,
                                                 const std::filesystem::path& destination,
                                                 const std::stop_token& stop = {},
                                                 const LogCallback& log = {});

}  // namespace world_explorer
