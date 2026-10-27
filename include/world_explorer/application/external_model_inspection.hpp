#pragma once

#include "world_explorer/asset_catalog/external_models.hpp"

namespace world_explorer {

// Read-only <=2 MiB hash-pinned request, two independently pinned small receipts
// (their original <=64 MiB budgets), exact context/host/model/review/byte limits.
// Relative native paths are based at the request, with no dot/dot-dot segments.
// Returns labeled diagnostic JSON; no payload ownership, installation, workers,
// acquisition, rights approval or qualification. Progress is forwarded to the
// two-pass observer; caller callback effects are not rolled back.
Result<std::string> inspect_asset_external_models_request(
    const std::filesystem::path& request,
    const std::string& expected_sha256,
    const std::stop_token& stop = {},
    const std::function<void(const AssetExternalModelProgress&)>& progress = {});

}  // namespace world_explorer
