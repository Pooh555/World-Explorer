#ifndef WORLD_EXPLORER_CORE_ASSET_FILES_HPP
#define WORLD_EXPLORER_CORE_ASSET_FILES_HPP
#include "world_explorer/core/result.hpp"
#include <cstddef>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <vector>
namespace world_explorer::asset_files {
result<std::vector<std::byte>> read_bytes(const std::filesystem::path& path, std::size_t maximum,
                                          const std::stop_token& stop);
result<std::string> publish_asset(const std::filesystem::path& destination,
                                  std::span<const std::byte> bytes, const std::stop_token& stop);
result<void> flush_directory_entry(const std::filesystem::path& directory);
// replaced is true immediately after the atomic rename, even if its subsequent
// directory flush fails. Such an outcome requires reopening the owning store.
result<void> replace_manifest(const std::filesystem::path& destination,
                              std::span<const std::byte> bytes, const std::stop_token& stop,
                              bool& replaced);
} // namespace world_explorer::asset_files
#endif
