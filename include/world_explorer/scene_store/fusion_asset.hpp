#pragma once

#include "world_explorer/core/result.hpp"
#include "world_explorer/core/types.hpp"

#include <array>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

struct FusionContext {
    std::string m_manifest_sha256, m_receipt_sha256, m_recipe_sha256, m_pose_sha256;
    WorldEpoch m_epoch;
    MapRevisionId m_pose_revision;
};

struct ObservedVoxel {
    std::array<std::int32_t, 3> m_lattice;
    float m_tsdf {}, m_weight {};
    std::uint32_t m_observation_count {}, m_support_sum {}, m_front_count {};
    double m_sigma2_sum {};
    float m_sigma_weight {};
};

struct FusionVertex {
    std::array<float, 3> m_position, m_normal;
};

struct ObservedFusionAsset {
    FusionContext m_context;
    std::string m_asset_sha256;
    double m_extent {}, m_voxel_size {}, m_truncation {};
    std::size_t m_unknown_allocated_voxels {};
    // Exact source documents, preserving coverage and uncertainty denominators.
    std::string m_manifest_json, m_receipt_json;
    std::vector<ObservedVoxel> m_voxels;
    std::vector<FusionVertex> m_vertices;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
};

struct FusionAssetLimits {
    ByteBudget m_encoded_bytes { 256uz * 1024uz * 1024uz };
    std::size_t m_max_voxels { 16777216uz };
    std::size_t m_max_vertices { 4000000uz };
    std::size_t m_max_triangles { 8000000uz };
};

// Uncompressed project-owned V1. Pins are supplied independently of the file.
// Validates structure/context, never geometry quality, free space or collision.
Result<ObservedFusionAsset> decode_fusion_asset(std::span<const std::byte> bytes,
                                                const std::string& expected_sha256,
                                                const FusionContext& expected,
                                                FusionAssetLimits limits = {},
                                                const std::stop_token& stop = {});

}  // namespace world_explorer
