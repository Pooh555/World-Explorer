#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace world_explorer {

class Sha256Builder {
  public:
    Sha256Builder();
    ~Sha256Builder();
    Sha256Builder(const Sha256Builder&) = delete;
    Sha256Builder& operator=(const Sha256Builder&) = delete;
    void update(std::span<const std::byte> bytes);
    std::string finish();

  private:
    struct Implementation;
    std::unique_ptr<Implementation> m_impl;
};

std::string sha256(std::span<const std::byte> bytes);
Result<std::string> sha256_file(const std::filesystem::path& path,
                                std::size_t max_bytes,
                                const std::stop_token& stop = {});

}  // namespace world_explorer
