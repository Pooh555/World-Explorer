#pragma once

#include "world_explorer/asset_catalog/asset_definition.hpp"

#include <span>

namespace world_explorer {

struct PbrMaterial {
    AssetMaterialVariantId m_variant;
    AssetRevisionRef m_product;
    AssetLightingMode m_lighting { AssetLightingMode::CAPTURED_APPEARANCE };
    std::uint32_t m_width {}, m_height {};
    bool m_linear_filter {}, m_repeat {};
    // Encoded RGBA8, in base-color/ORM/tangent-normal/emission order. Color
    // and emission use sRGB; all other components are linear numeric data.
    std::array<std::vector<std::byte>, 4> m_images;
    std::array<std::string, 4> m_png_sha256;
};

struct PbrInstance {
    AssetInstanceId m_instance;
    SimilarityTransform m_chunk_from_asset;
};

struct PbrAppearance {
    AssetDefinitionRef m_definition;
    WorldEpoch m_epoch;
    MapRevisionId m_pose_revision;
    std::string m_export_manifest, m_export_manifest_sha256, m_geometry_sha256;
    std::string m_asset_document, m_scene_document;
    bool m_synthetic {}, m_manual_ancestor {};
    std::vector<std::array<float, 2>> m_uv;
    std::vector<std::array<float, 4>> m_tangents;
    std::vector<std::uint32_t> m_triangle_materials;
    std::vector<PbrMaterial> m_materials;
    std::vector<PbrInstance> m_instances;
};

// Bounded additive binary tail, independent of the legacy mesh/atlas encodings.
// <=16 MiB, <=8192 vertices/faces, <=4 materials /256-square maps, <=64 instances.
Result<std::vector<std::byte>> encode_pbr_appearance(const PbrAppearance& appearance);
Result<PbrAppearance> decode_pbr_appearance(std::span<const std::byte> bytes);

struct SceneChunk;
struct SceneLimits;
Result<std::size_t> validate_pbr_appearance(const SceneChunk& chunk, const SceneLimits& limits);

}  // namespace world_explorer
