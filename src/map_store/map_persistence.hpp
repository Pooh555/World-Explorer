#pragma once

#include "world_explorer/map_store/map_store.hpp"

#include <span>

namespace world_explorer {

class MapPersistence {
  public:
    ~MapPersistence();
    static Result<std::unique_ptr<MapStore>> open(PersistentMapConfig config,
                                                  const std::stop_token& stop);
    Result<void> publish(const MapState& state,
                         const std::deque<MapStore::Remembered>& receipts,
                         const std::stop_token& stop);
    Result<void> install_pose_asset(std::span<const std::byte> bytes,
                                    const std::string& hash,
                                    const std::stop_token& stop);

    bool requires_reopen() const noexcept { return uncertain_; }

  private:
    explicit MapPersistence(PersistentMapConfig config);
    Result<void> lock();
    Result<void> admit_asset(std::size_t bytes);
    Result<void> reload(MapStore& store, const std::stop_token& stop);
    Result<std::vector<std::byte>> encode(const MapState& state,
                                          const std::deque<MapStore::Remembered>& receipts,
                                          const std::stop_token& stop);
    PersistentMapConfig config_;
    std::string manifest_hash_;
    bool uncertain_ {};
#ifdef _WIN32
    void* writer_handle_ {};
#else
    int writer_descriptor_ { -1 };
#endif
};

}  // namespace world_explorer
