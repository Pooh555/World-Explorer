#pragma once

#include "world_explorer/application/evaluation_corpus.hpp"

namespace world_explorer {

// Read-only coordinator command. The hash-pinned request independently selects
// receipts, capture/annotation/evidence pins and current registry commits.
// No pin is discovered from the corpus being inspected. No workers or writes.
Result<AdmittedAssetCorpus> inspect_asset_corpus_request(const std::filesystem::path& request,
                                                         const std::string& expected_sha256,
                                                         const std::stop_token& stop = {});

}  // namespace world_explorer
