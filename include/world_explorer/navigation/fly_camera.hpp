#ifndef WORLD_EXPLORER_NAVIGATION_FLY_CAMERA_HPP
#define WORLD_EXPLORER_NAVIGATION_FLY_CAMERA_HPP

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"

namespace world_explorer {
struct fly_input {
    double forward{}, right{}, up{};
    double mouse_x{}, mouse_y{}, wheel{};
    bool accelerate{};
    bool captured{};
    bool focused{true};
    bool text_input{};
};
struct fly_camera {
    vec3d position{0, 1.5, 4};
    double yaw{};
    double pitch{};
    double speed{2};
    double sensitivity{0.002};
    vec3d forward() const;
    vec3d right() const;
    vec3d down() const;
    void update(const fly_input& input, double elapsed_seconds);
    void fit(const bounds3d& bounds);
};
struct camera_bookmark {
    scene_id scene;
    world_epoch epoch;
    fly_camera camera;
};
result<fly_camera> restore_bookmark(const camera_bookmark& bookmark, scene_id scene,
                                    world_epoch epoch);
} // namespace world_explorer

#endif // WORLD_EXPLORER_NAVIGATION_FLY_CAMERA_HPP
