#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <memory>

namespace world_explorer {

// Nonblocking exclusive local writer lock. The regular lock file remains after
// release; the operating system releases ownership on process exit. No handles
// are inherited by children. Does not defend against external directory mutation.
class FileLock {
  public:
    static Result<std::unique_ptr<FileLock>> acquire(const std::filesystem::path& path);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

  private:
    struct Implementation;
    explicit FileLock(std::unique_ptr<Implementation> state);
    std::unique_ptr<Implementation> m_state;
};

}  // namespace world_explorer
