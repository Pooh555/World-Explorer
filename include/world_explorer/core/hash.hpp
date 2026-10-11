#ifndef WORLD_EXPLORER_CORE_HASH_HPP
#define WORLD_EXPLORER_CORE_HASH_HPP
#include "world_explorer/core/result.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
namespace world_explorer {
class sha256_builder {
  public:
    sha256_builder();
    ~sha256_builder();
    sha256_builder(const sha256_builder&) = delete;
    sha256_builder& operator=(const sha256_builder&) = delete;
    void update(std::span<const std::byte> bytes);
    std::string finish();

  private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
};
std::string sha256(std::span<const std::byte> bytes);
result<std::string> sha256_file(const std::filesystem::path& path, std::size_t max_bytes);
} // namespace world_explorer

#endif // WORLD_EXPLORER_CORE_HASH_HPP
