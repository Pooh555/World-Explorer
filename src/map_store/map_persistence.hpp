#ifndef WORLD_EXPLORER_MAP_STORE_MAP_PERSISTENCE_HPP
#define WORLD_EXPLORER_MAP_STORE_MAP_PERSISTENCE_HPP
#include "world_explorer/map_store/map_store.hpp"
#include <span>
namespace world_explorer {
class map_persistence {
  public:
    ~map_persistence();
    static result<std::unique_ptr<map_store>> open(persistent_map_config config,
                                                   const std::stop_token& stop);
    result<void> publish(const map_state& state, const std::deque<map_store::remembered>& receipts,
                         const std::stop_token& stop);
    result<void> install_pose_asset(std::span<const std::byte> bytes, const std::string& hash,
                                    const std::stop_token& stop);
    bool requires_reopen() const noexcept { return uncertain_; }

  private:
    explicit map_persistence(persistent_map_config config);
    result<void> lock();
    result<void> admit_asset(std::size_t bytes);
    result<void> reload(map_store& store, const std::stop_token& stop);
    result<std::vector<std::byte>> encode(const map_state& state,
                                          const std::deque<map_store::remembered>& receipts,
                                          const std::stop_token& stop);
    persistent_map_config config_;
    std::string manifest_hash_;
    bool uncertain_{};
#ifdef _WIN32
    void* writer_handle_{};
#else
    int writer_descriptor_{-1};
#endif
};
} // namespace world_explorer
#endif
