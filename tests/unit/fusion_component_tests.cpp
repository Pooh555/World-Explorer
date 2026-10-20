#include "world_explorer/scene_store/fusion_components.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>

using namespace world_explorer;

namespace {

ObservedFusionAsset components_fixture() {
    ObservedFusionAsset asset;
    asset.m_voxel_size = 1;
    for (std::int32_t x { 0 }; x < 2; ++x) {
        for (std::int32_t y { 0 }; y < 2; ++y) {
            for (std::int32_t z { 0 }; z < 2; ++z) {
                asset.m_voxels.push_back({ { x, y, z }, 0, .5F, 2, 6, 1, .125, .25F });
            }
        }
    }
    for (const auto& position : std::array<std::array<float, 3>, 8> { { { .25F, .25F, .25F },
                                                                      { .75F, .25F, .25F },
                                                                      { .25F, .75F, .25F },
                                                                      { 2.25F, .25F, .25F },
                                                                      { 2.75F, .25F, .25F },
                                                                      { 2.25F, .75F, .25F },
                                                                      { .5F, .25F, .25F },
                                                                      { 9, 9, 9 } } }) {
        asset.m_vertices.push_back({ position, { 0, 0, 1 } });
    }
    asset.m_triangles = { { 0, 1, 2 }, { 3, 4, 5 }, { 0, 6, 1 } };
    return asset;
}

}  // namespace

TEST_CASE("fusion component audit preserves complete geometry and support denominators",
          "[fusion-components]") {
    const auto K_ASSET = components_fixture();
    const auto K_ANALYSIS = analyze_fusion_components(K_ASSET);
    REQUIRE(K_ANALYSIS);
    REQUIRE(K_ANALYSIS->m_components.size() == 2uz);
    CHECK(K_ANALYSIS->m_unused_vertices == 1uz);
    const auto& total = K_ANALYSIS->m_total;
    CHECK(total.m_vertices == 7uz);
    CHECK(total.m_triangles == 3uz);
    CHECK(total.m_zero_area_triangles == 1uz);
    CHECK(total.m_area == .25);
    CHECK(total.m_area_with_unknown_corners == .125);
    CHECK(total.m_area_with_unbacked_vertices == .125);
    CHECK(total.m_triangles_with_unknown_corners == 1uz);
    CHECK(total.m_triangles_with_unbacked_vertices == 1uz);
    CHECK(total.m_support.m_corner_coverage[0] == 3uz);
    CHECK(total.m_support.m_corner_coverage[8] == 4uz);
    CHECK(total.m_support.m_weight_sum == 16);
    CHECK(total.m_support.m_weight_min == .5);
    CHECK(total.m_support.m_weight_max == .5);
    CHECK(total.m_support.m_observations == 64uz);
    CHECK(total.m_support.m_support_sum == 192uz);
    CHECK(total.m_support.m_front_count == 32uz);
    CHECK(total.m_support.m_sigma_corners == 32uz);
    CHECK(total.m_support.m_sigma2_sum == 4);
    CHECK(total.m_support.m_sigma_weight == 8);
    const auto& first = K_ANALYSIS->m_components[0];
    CHECK(first.m_first_triangle == 0uz);
    CHECK(first.m_vertices == 4uz);
    CHECK(first.m_triangles == 2uz);
    CHECK(first.m_area == .125);
    CHECK(K_ANALYSIS->m_components[1].m_first_triangle == 1uz);
    CHECK(K_ANALYSIS->m_components[1].m_support.m_weight_min == 0);
    CHECK(K_ASSET.m_vertices.size() == 8uz);
    CHECK(K_ASSET.m_triangles.size() == 3uz);
    CHECK(K_ASSET.m_voxels.size() == 8uz);
}

TEST_CASE("fusion components retain complete sequence observations and original backend support",
          "[fusion-components]") {
    auto asset = components_fixture();
    asset.m_admitted_sequence_frames = 63uz;
    for (auto& voxel : asset.m_voxels) {
        voxel.m_observation_count = 63;
        voxel.m_support_sum = 63000000;
    }
    const auto K_RESULT = analyze_fusion_components(asset);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_total.m_support.m_observations == 32uz * 63uz);
    CHECK(K_RESULT->m_total.m_support.m_support_sum == 32uz * 63000000uz);
    SECTION("observations cannot exceed the admitted inventory") {
        asset.m_voxels[0].m_observation_count = 64;
    }
    SECTION("backend support retains the native per-observation bound") {
        asset.m_voxels[0].m_support_sum = 63000001;
    }
    SECTION("sequence inventory stays bounded") {
        asset.m_admitted_sequence_frames = 121uz;
    }
    const auto K_REJECTED = analyze_fusion_components(asset);
    REQUIRE(K_REJECTED.has_value() == false);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
}

TEST_CASE("fusion components do not weld positions and do connect shared vertex indices",
          "[fusion-components]") {
    auto asset = components_fixture();
    for (auto index = 0uz; index < 3uz; ++index) {
        asset.m_vertices[index + 3uz] = asset.m_vertices[index];
    }
    const auto K_SEPARATE = analyze_fusion_components(asset);
    REQUIRE(K_SEPARATE);
    REQUIRE(K_SEPARATE->m_components.size() == 2uz);
    CHECK(K_SEPARATE->m_total.m_support.m_corner_coverage[8] == 7uz);
    asset.m_triangles[1][0] = 0;
    const auto K_JOINED = analyze_fusion_components(asset);
    REQUIRE(K_JOINED);
    CHECK(K_JOINED->m_components.size() == 1uz);
    CHECK(K_JOINED->m_unused_vertices == 2uz);
}

TEST_CASE("fusion support stencils retain partial unknown corners and negative floor semantics",
          "[fusion-components]") {
    auto asset = components_fixture();
    const auto K_NEGATIVE = GENERATE(false, true);
    if (K_NEGATIVE) {
        for (auto& vertex : asset.m_vertices) {
            vertex.m_position[0] -= 1;
        }
        for (auto& voxel : asset.m_voxels) {
            --voxel.m_lattice[0];
        }
    }
    asset.m_voxels.pop_back();
    for (auto& voxel : asset.m_voxels) {
        voxel.m_sigma_weight = 0;
        voxel.m_sigma2_sum = 0;
    }
    const auto K_ANALYSIS = analyze_fusion_components(asset);
    REQUIRE(K_ANALYSIS);
    CHECK(K_ANALYSIS->m_total.m_support.m_corner_coverage[7] == 4uz);
    CHECK(K_ANALYSIS->m_total.m_support.m_corner_coverage[0] == 3uz);
    CHECK(K_ANALYSIS->m_total.m_triangles_with_unknown_corners == 3uz);
    CHECK(K_ANALYSIS->m_total.m_triangles_with_unbacked_vertices == 1uz);
    CHECK(K_ANALYSIS->m_total.m_support.m_sigma_corners == 0uz);
}

TEST_CASE("fusion component audit enforces exact working count and cancellation limits",
          "[fusion-components]") {
    const auto K_ASSET = components_fixture();
    FusionComponentLimits limits;
    limits.m_asset.m_max_vertices = 8uz;
    limits.m_asset.m_max_triangles = 3uz;
    limits.m_asset.m_max_voxels = 8uz;
    limits.m_max_components = 2uz;
    limits.m_working_bytes = { 80uz + 2uz * sizeof(FusionMeshComponent) };
    REQUIRE(analyze_fusion_components(K_ASSET, limits));
    const auto K_LIMIT = GENERATE(0, 1, 2, 3, 4);
    if (K_LIMIT == 0) {
        --limits.m_asset.m_max_vertices;
    }
    if (K_LIMIT == 1) {
        --limits.m_asset.m_max_triangles;
    }
    if (K_LIMIT == 2) {
        --limits.m_asset.m_max_voxels;
    }
    if (K_LIMIT == 3) {
        --limits.m_max_components;
    }
    if (K_LIMIT == 4) {
        --limits.m_working_bytes.m_bytes;
    }
    const auto K_REJECTED = analyze_fusion_components(K_ASSET, limits);
    REQUIRE(K_REJECTED.has_value() == false);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    std::stop_source cancel;
    cancel.request_stop();
    const auto K_CANCELLED = analyze_fusion_components(K_ASSET, {}, cancel.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("fusion component audit rejects malformed geometry and evidence", "[fusion-components]") {
    auto asset = components_fixture();
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    switch (K_MUTATION) {
    case 0:
        asset.m_triangles[0][0] = 8;
        break;
    case 1:
        asset.m_triangles[0][0] = 1;
        break;
    case 2:
        asset.m_vertices[0].m_position[0] = std::numeric_limits<float>::quiet_NaN();
        break;
    case 3:
        asset.m_vertices[7].m_position[0] = 1048578;
        break;
    case 4:
        asset.m_voxel_size = 0;
        break;
    case 5:
        std::swap(asset.m_voxels[0], asset.m_voxels[1]);
        break;
    case 6:
        asset.m_voxels[0].m_weight = 0;
        break;
    case 7:
        asset.m_voxels[0].m_observation_count = 19;
        break;
    case 8:
        asset.m_voxels[0].m_support_sum = 37;
        break;
    case 9:
        asset.m_voxels[0].m_sigma_weight = 1;
        break;
    case 10:
        asset.m_voxels[0].m_sigma2_sum = std::numeric_limits<double>::infinity();
        break;
    default:
        FAIL("Unknown component mutation");
    }
    const auto K_REJECTED = analyze_fusion_components(asset);
    REQUIRE(K_REJECTED.has_value() == false);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
}

TEST_CASE("empty fusion component audit keeps empty denominators", "[fusion-components]") {
    ObservedFusionAsset asset;
    asset.m_voxel_size = 1;
    const auto K_ANALYSIS = analyze_fusion_components(asset);
    REQUIRE(K_ANALYSIS);
    CHECK(K_ANALYSIS->m_components.empty());
    CHECK(K_ANALYSIS->m_total.m_vertices == 0uz);
    CHECK(K_ANALYSIS->m_total.m_support.m_weight_min == 0);
}
