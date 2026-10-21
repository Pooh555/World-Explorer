#pragma once

#include "world_explorer/collision/triangle_cook.hpp"

#include <memory>
#include <optional>

namespace world_explorer {

// A Jolt mesh shape made from canonical synthetic triangles. This owns no scene
// transaction and grants no walking permission; Jolt stays behind this interface.
class JoltStaticCollision {
    struct AdmissionToken { };
    struct Impl;

  public:
    explicit JoltStaticCollision(AdmissionToken);
    ~JoltStaticCollision();

    JoltStaticCollision(const JoltStaticCollision&) = delete;
    JoltStaticCollision& operator=(const JoltStaticCollision&) = delete;

    const std::string& canonical_digest() const;
    std::size_t triangle_count() const;
    Result<void> validate_source(const SceneState& current, const std::stop_token& stop = {}) const;

    // Returns the first normalized segment fraction, if any. Diagnostic only.
    Result<std::optional<double>> ray_cast(const SceneState& current,
                                           Vec3d start,
                                           Vec3d end,
                                           const std::stop_token& stop = {}) const;

  private:
    friend class SyntheticCapsuleController;
    friend Result<std::shared_ptr<const JoltStaticCollision>> build_jolt_static_collision(
        std::shared_ptr<const CanonicalCollisionMesh>, const SceneState&, const std::stop_token&);

    std::shared_ptr<const CanonicalCollisionMesh> m_cook;
    std::unique_ptr<Impl> m_impl;
};

Result<std::shared_ptr<const JoltStaticCollision>>
build_jolt_static_collision(std::shared_ptr<const CanonicalCollisionMesh> cook,
                            const SceneState& current,
                            const std::stop_token& stop = {});

}  // namespace world_explorer
