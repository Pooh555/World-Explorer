#pragma once

#include "world_explorer/application/commands.hpp"

namespace world_explorer {

// Own retained-cache solving/reuse, fresh CUDA depth/native adoption, or an explicit
// MESH_POSTPROCESS_PIPELINE_V1 cleanup/bake/export job over an immutable bundle.
// Solver requests select local scripts/interpreter; postprocessing uses checked
// application commands. Outputs retain separate attempts. Call off the UI thread.
// No walking claim.
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
