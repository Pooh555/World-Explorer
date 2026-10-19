#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>
#include <string>

namespace world_explorer {

struct BakedGlbExport {
    std::filesystem::path m_source, m_destination;
};

Result<void> export_baked_glb_bundle(const BakedGlbExport& request,
                                     const std::stop_token& stop = {});
Result<void> import_baked_glb(const std::filesystem::path& file,
                              const std::string& expected_sha256,
                              const std::filesystem::path& destination,
                              const std::stop_token& stop = {});

}  // namespace world_explorer
