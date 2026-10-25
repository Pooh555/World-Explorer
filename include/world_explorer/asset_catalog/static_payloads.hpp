#pragma once

#include "world_explorer/asset_catalog/asset_definition.hpp"

namespace world_explorer {

struct StaticAssetMesh {
    std::vector<Vec3d> m_positions, m_normals;
    std::vector<std::array<double, 2>> m_uv;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
    AssetMaterialVariantId m_variant;
    bool operator==(const StaticAssetMesh&) const = default;
};

struct ConstantPbrMaterial {
    Vec3d m_base_color, m_emission;
    double m_metallic {}, m_roughness {}, m_occlusion {};
    bool operator==(const ConstantPbrMaterial&) const = default;
};

struct StaticAssetPayloads {
    StaticAssetMesh m_mesh;
    ConstantPbrMaterial m_material;
};

// First explicit generated static-surface payload profile: <=512 vertices/faces,
// one generated constant metallic/roughness material, arbitrary units, no LOD or
// collider. Checks actual mesh/channel bytes and exact face/one-texel provenance.
// No measured correspondence, closed-manifold, renderer or relighting approval.
Result<StaticAssetPayloads>
inspect_static_asset_payloads(const AdmittedAssetDefinition& definition,
                              const AdmittedAssetReceipt& receipt,
                              const std::vector<AssetProvenanceSelection>& provenance,
                              const AssetDependencyContext& expected,
                              const std::stop_token& stop = {});

}  // namespace world_explorer
