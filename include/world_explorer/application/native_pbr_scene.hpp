#pragma once

#include "world_explorer/application/native_export.hpp"

namespace world_explorer {

// Converts a private admitted portable package into the native immutable PBR
// scene profile. No third-party import or reconstruction/relighting approval.
Result<SceneSnapshot> compile_native_pbr_scene(const AdmittedNativeExport& package,
                                               const AssetDependencyContext& expected,
                                               const std::stop_token& stop = {});

}  // namespace world_explorer
