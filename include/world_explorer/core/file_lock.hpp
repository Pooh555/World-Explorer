#ifndef WORLD_EXPLORER_CORE_FILE_LOCK_HPP
#define WORLD_EXPLORER_CORE_FILE_LOCK_HPP
#include "world_explorer/core/result.hpp"
#include <filesystem>
#include <memory>

namespace world_explorer {
// Nonblocking exclusive local writer lock. The regular lock file remains after
// release; the operating system releases ownership on process exit. No handles
// are inherited by children. Does not defend against external directory mutation.
class file_lock {
  public:
    static result<std::unique_ptr<file_lock>> acquire(const std::filesystem::path& path);
    ~file_lock();
    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;

  private:
    struct implementation;
    explicit file_lock(std::unique_ptr<implementation> state);
    std::unique_ptr<implementation> state_;
};
} // namespace world_explorer
#endif
