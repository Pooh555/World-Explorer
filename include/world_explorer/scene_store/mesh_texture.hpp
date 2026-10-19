#ifndef WORLD_EXPLORER_SCENE_STORE_MESH_TEXTURE_HPP
#define WORLD_EXPLORER_SCENE_STORE_MESH_TEXTURE_HPP

#include "world_explorer/scene_store/color_projection.hpp"
#include "world_explorer/scene_store/scene.hpp"

namespace world_explorer {

// Fixed eight-texel triangle tiles. Every valid texel is independently projected;
// convex score-squared blending uses the continuous source vertex normal field.
class MeshTextureBaker {
  public:
    static Result<std::unique_ptr<MeshTextureBaker>> create(SceneChunk chunk);
    ~MeshTextureBaker();
    MeshTextureBaker(const MeshTextureBaker&) = delete;
    MeshTextureBaker& operator=(const MeshTextureBaker&) = delete;
    Result<void>
    add_view(const ColorProjectionView& view, FrameId frame, const std::stop_token& stop = {});
    Result<BakedMeshAppearance> finish(const std::stop_token& stop = {});

  private:
    struct State;
    explicit MeshTextureBaker(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_SCENE_STORE_MESH_TEXTURE_HPP
