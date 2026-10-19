#pragma once

#include "world_explorer/application/fusion_assets.hpp"

namespace world_explorer {

// Exclusive derived output directory. Parent assets stay in place. Publish the
// certificate/report/request first, then the immutable manifest atomically last.
// Failure retains diagnostics; late publish/flush errors require reopening the
// manifest to determine publication state. No caller deletion or overwrite.
Result<std::string> export_fusion_cleanup(const FusionAdmissionRequest& request,
                                          const std::filesystem::path& destination,
                                          const std::stop_token& stop = {});

}  // namespace world_explorer
