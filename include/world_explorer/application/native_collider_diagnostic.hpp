#pragma once

#include "world_explorer/application/native_export.hpp"
#include "world_explorer/collision/native_static_shape.hpp"

namespace world_explorer {

struct NativeColliderQuery {
    std::uint64_t m_query_id {};
    AssetInstanceId m_instance;
    Vec3d m_start, m_end;
};

struct NativeColliderDiagnosticPolicy {
    NativeShapePolicy m_shape;
    double m_world_position_units { 1e-8 };
};

struct NativeColliderDiagnosticOutput {
    std::string m_request_json, m_report_json;
};

// Current private assets + their private schema-4 package; <=2 collider
// definitions/64 instances/256 unique queries. Serializes the replay path
// relative to the future request's parent, without accessing it. Diagnostic
// output grants no scene/body/support or qualification authority.
Result<NativeColliderDiagnosticOutput>
inspect_native_colliders(const AdmittedNativeAssets& assets,
                         const AdmittedNativeExport& package,
                         const AssetDependencyContext& expected,
                         const std::vector<NativeColliderQuery>& queries,
                         const std::filesystem::path& replay_package_directory,
                         const NativeColliderDiagnosticPolicy& policy = {},
                         const std::stop_token& stop = {});

// Hash-pinned <=1 MiB frozen selection replay, not live scene authority or
// re-admission of original capture/rights. Reads a closed <=64-file/8 MiB package;
// no writes, worker execution, body installation or measured walking.
Result<std::string> inspect_native_colliders_request(const std::filesystem::path& request,
                                                     const std::string& expected_sha256,
                                                     const std::stop_token& stop = {});

}  // namespace world_explorer
