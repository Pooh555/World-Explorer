#ifndef WORLD_EXPLORER_APPLICATION_FUSION_SCENE_HPP
#define WORLD_EXPLORER_APPLICATION_FUSION_SCENE_HPP

#include "world_explorer/application/fusion_assets.hpp"
#include "world_explorer/scene_store/scene.hpp"

namespace world_explorer {

// Admits original fusion bytes against selected canonical poses, then retains raw
// triangles, full coverage metadata and immutable source identity in a snapshot.
Result<SceneSnapshot> load_fusion_scene(const FusionAdmissionRequest& request,
                                        const std::stop_token& stop = {});
// Bounded externally pinned request; paths resolve relative to its directory.
Result<SceneSnapshot> load_fusion_scene_request(const std::filesystem::path& request,
                                                const std::string& expected_sha256,
                                                const std::stop_token& stop = {});

// Admits the source/proof before publishing a portable native snapshot to a new
// directory. Cancellation is checked before the atomic bundle publication phase.
Result<void> compile_fusion_scene(const std::filesystem::path& request,
                                  const std::string& expected_sha256,
                                  const std::filesystem::path& destination,
                                  const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
