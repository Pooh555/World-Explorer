#pragma once

#include <cstdint>

namespace world_explorer {

enum class MaterialChannelRole : std::uint8_t {
    CAPTURED_RGB,
    BASE_COLOR,
    METALLIC,
    ROUGHNESS,
    NORMAL,
    OCCLUSION,
    EMISSION
};

}  // namespace world_explorer
