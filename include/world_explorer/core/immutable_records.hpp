#pragma once

#include "world_explorer/core/result.hpp"
#include "world_explorer/core/types.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <stop_token>
#include <vector>

namespace world_explorer {

struct ImmutableRecord {
    std::string m_key;
    std::vector<std::byte> m_bytes;
};

struct RecordCommit {
    std::uint64_t m_generation {};
    std::string m_manifest_sha256;
};

struct RecordStoreSnapshot {
    RecordCommit m_commit;
    std::map<std::string, std::vector<std::byte>> m_records;
};

// Opaque byte primitive; domain readers/writers must validate their own schemas.
// Closed owned root, nonblocking writer lock, immutable intent/blob publication,
// atomic manifest last. At most 65 records / 8 MiB per batch, 4 MiB per record;
// 4096 committed keys, 1024 operations, 64 MiB committed bytes / physical blobs, 16 MiB intents.
// Exact completed retries return the current validated store observation.
Result<RecordCommit>
publish_immutable_records(const std::filesystem::path& directory,
                          OperationId operation,
                          std::uint64_t base_generation,
                          const std::vector<ImmutableRecord>& records,
                          const std::vector<std::filesystem::path>& protected_roots,
                          const std::stop_token& stop = {},
                          const std::function<void(std::size_t)>& observed = {});
Result<RecordStoreSnapshot> read_immutable_records(const std::filesystem::path& directory,
                                                   const std::stop_token& stop = {});

}  // namespace world_explorer
