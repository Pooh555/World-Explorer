#pragma once

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"

namespace world_explorer {

struct FlyInput {
    double m_forward {}, m_right {}, m_up {};
    double m_mouse_x {}, m_mouse_y {}, m_wheel {};
    bool m_accelerate {};
    bool m_captured {};
    bool m_focused { true };
    bool m_text_input {};
};

struct FlyCamera {
    Vec3d m_position { 0, 1.5, 4 };
    double m_yaw {};
    double m_pitch {};
    double m_speed { 2 };
    double m_sensitivity { 0.002 };
    // Presentation/navigation reference; this is not measured gravity.
    Vec3d m_up_axis { 0, 1, 0 };
    Vec3d reference_right() const;
    Vec3d reference_forward() const;
    Vec3d forward() const;
    Vec3d right() const;
    Vec3d down() const;
    void update(const FlyInput& input, double elapsed_seconds);
    void fit(const Bounds3d& bounds);
};

struct CameraBookmark {
    SceneId m_scene;
    WorldEpoch m_epoch;
    FlyCamera m_camera;
};

Result<FlyCamera> restore_bookmark(const CameraBookmark& bookmark, SceneId scene, WorldEpoch epoch);

}  // namespace world_explorer
