#ifndef WORLD_EXPLORER_APPLICATION_SCENES_HPP
#define WORLD_EXPLORER_APPLICATION_SCENES_HPP
#include "world_explorer/scene_store/bundle.hpp"
namespace world_explorer {
// Synthetic diagnostic geometry is never presented as a reconstructed capture.
scene_snapshot make_diagnostic_scene();
result<void> compile_splat_scene(const std::filesystem::path& ply,
                                 const std::filesystem::path& destination);
bounds3d world_bounds(const scene_state& scene);
} // namespace world_explorer

#endif // WORLD_EXPLORER_APPLICATION_SCENES_HPP
