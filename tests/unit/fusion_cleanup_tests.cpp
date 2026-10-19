#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/fusion_cleanup.hpp"
#include "world_explorer/scene_store/fusion_components.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cfenv>
#include <limits>
#include <numeric>

using namespace world_explorer;

namespace {

using Triangle = std::array<std::uint32_t, 3>;

ObservedFusionAsset fan_fixture() {
    ObservedFusionAsset asset;
    asset.m_asset_sha256 = std::string(64uz, 'f');
    asset.m_voxel_size = 1;
    for (std::int32_t x { 0 }; x < 3; ++x) {
        for (std::int32_t y { 0 }; y < 3; ++y) {
            for (std::int32_t z { -1 }; z < 2; ++z) {
                asset.m_voxels.push_back({ { x, y, z }, 0, 2, 3, 6, 1, 0, 0 });
            }
        }
    }
    for (const auto& p : std::array<std::array<float, 3>, 5> { { { .55F, .55F, .3F },
                                                               { .25F, .25F, .25F },
                                                               { 1.25F, .25F, .25F },
                                                               { .25F, 1.25F, .25F },
                                                               { .5F, .5F, -.5F } } }) {
        asset.m_vertices.push_back({ p, { 0, 0, 1 } });
    }
    asset.m_triangles = { { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 1 }, { 1, 4, 2 }, { 2, 4, 3 }, { 3, 4, 1 } };
    return asset;
}

Result<FusionCleanup> replay(const ObservedFusionAsset& asset, const std::string& text) {
    const auto K_BYTES = std::as_bytes(std::span(text));
    return decode_fusion_cleanup(K_BYTES, sha256(K_BYTES), asset);
}

ObservedFusionAsset edge_fixture() {
    auto asset = fan_fixture();
    asset.m_vertices.clear();
    for (const auto& p : std::array<std::array<float, 3>, 6> { { { .5F, .5F, .3F },
                                                               { .55F, .5F, .25F },
                                                               { .5F, 1.25F, .25F },
                                                               { 0, .5F, .25F },
                                                               { .5F, 0, .25F },
                                                               { .5F, .5F, -.5F } } }) {
        asset.m_vertices.push_back({ p, { 0, 0, 1 } });
    }
    asset.m_triangles = {
        { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 4 }, { 0, 4, 1 }, { 5, 2, 1 }, { 5, 3, 2 }, { 5, 4, 3 }, { 5, 1, 4 } };
    return asset;
}

}  // namespace

TEST_CASE("short-edge cleanup maps complete source triangles and collapsed edges into the output",
          "[cleanup]") {
    const auto K_ASSET = edge_fixture();
    const auto K_RESULT = simplify_fusion_mesh(K_ASSET);
    REQUIRE(K_RESULT);
    REQUIRE(K_RESULT->m_patches.size() == 1uz);
    CHECK(K_RESULT->m_patches[0].m_vertex == 0U);
    CHECK(K_RESULT->m_patches[0].m_target == 1U);
    CHECK(K_RESULT->m_vertices.size() == 5uz);
    CHECK(K_RESULT->m_triangles.size() == 6uz);
    CHECK(K_RESULT->m_distance_upper_bound > .07);
    CHECK(K_RESULT->m_distance_upper_bound < .071);
    const auto K_REPLAY = replay(K_ASSET, K_RESULT->m_document);
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_geometry_sha256 == K_RESULT->m_geometry_sha256);
    auto forged = nlohmann::json::parse(K_RESULT->m_document);
    forged["patches"][0]["target"] = 2U;
    CHECK(replay(K_ASSET, forged.dump()).has_value() == false);
    forged = nlohmann::json::parse(K_RESULT->m_document);
    forged["patches"][0]["barycentric_uv"][0] = .1;
    CHECK(replay(K_ASSET, forged.dump()).has_value() == false);
}

TEST_CASE("cleanup protects a pinched manifold link and bounds input counts", "[cleanup]") {
    auto asset = edge_fixture();
    // A separate closed tetrahedron shares only vertex 1: all edge incidences
    // remain two, but its disconnected vertex link must protect the first patch.
    for (const auto& p : std::array<std::array<float, 3>, 3> { { { 2, 0, 0 }, { 2, 1, 0 }, { 2, 0, 1 } } }) {
        asset.m_vertices.push_back({ p, { 0, 0, 1 } });
    }
    for (const auto& t : std::array<Triangle, 4> { { { 1, 6, 7 }, { 1, 7, 8 }, { 1, 8, 6 }, { 6, 8, 7 } } }) {
        asset.m_triangles.push_back(t);
    }
    const auto K_RESULT = simplify_fusion_mesh(asset);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_patches.empty());
    asset.m_vertices.resize(500001uz);
    const auto K_LIMITED = simplify_fusion_mesh(asset);
    REQUIRE(K_LIMITED.has_value() == false);
    CHECK(K_LIMITED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("fan cleanup certifies a full supported disk and preserves its component", "[cleanup]") {
    const auto K_ASSET = fan_fixture();
    const auto K_RESULT = simplify_fusion_mesh(K_ASSET);
    REQUIRE(K_RESULT);
    REQUIRE(K_RESULT->m_patches.size() == 1uz);
    CHECK(K_RESULT->m_vertices.size() == 4uz);
    CHECK(K_RESULT->m_triangles.size() == 4uz);
    CHECK(K_RESULT->m_output_to_source_vertices == std::vector<std::uint32_t> { 1, 2, 3, 4 });
    CHECK(K_RESULT->m_patches[0].m_ring == std::vector<std::uint32_t> { 1, 2, 3 });
    CHECK(K_RESULT->m_distance_upper_bound > .05);
    CHECK(K_RESULT->m_distance_upper_bound < .050001);
    CHECK(std::accumulate(K_RESULT->m_decisions.begin(), K_RESULT->m_decisions.end(), 0uz) == 5uz);
    CHECK(K_RESULT->m_decisions[6] == 1uz);
    for (auto index = 0uz; index < 4uz; ++index) {
        CHECK(K_RESULT->m_vertices[index].m_position == K_ASSET.m_vertices[index + 1uz].m_position);
        CHECK(K_RESULT->m_vertices[index].m_normal == K_ASSET.m_vertices[index + 1uz].m_normal);
    }
    auto after = K_ASSET;
    after.m_vertices = K_RESULT->m_vertices;
    after.m_triangles = K_RESULT->m_triangles;
    const auto K_BEFORE = analyze_fusion_components(K_ASSET);
    const auto K_AFTER = analyze_fusion_components(after);
    REQUIRE(K_BEFORE);
    REQUIRE(K_AFTER);
    CHECK(K_BEFORE->m_components.size() == 1uz);
    CHECK(K_AFTER->m_components.size() == 1uz);
    CHECK(K_AFTER->m_total.m_support.m_observations == 96uz);
    const auto K_REPLAY = replay(K_ASSET, K_RESULT->m_document);
    REQUIRE(K_REPLAY);
    CHECK(K_REPLAY->m_geometry_sha256 == K_RESULT->m_geometry_sha256);
    CHECK(K_REPLAY->m_document == K_RESULT->m_document);
    CHECK(K_REPLAY->m_triangles == K_RESULT->m_triangles);
    CHECK(K_ASSET.m_vertices.size() == 5uz);
    CHECK(K_ASSET.m_triangles.size() == 6uz);
}

TEST_CASE("fan cleanup protects boundary unknown weak support winding and excessive errors",
          "[cleanup]") {
    auto asset = fan_fixture();
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6);
    if (K_MUTATION == 0) {
        asset.m_triangles.pop_back();
    }
    if (K_MUTATION == 1) {
        asset.m_voxels.clear();
    }
    if (K_MUTATION == 2) {
        for (auto& voxel : asset.m_voxels) {
            voxel.m_observation_count = 1;
            voxel.m_weight = 1;
        }
    }
    if (K_MUTATION == 3) {
        for (auto& voxel : asset.m_voxels) {
            voxel.m_support_sum = 5;
        }
    }
    if (K_MUTATION == 4) {
        std::swap(asset.m_triangles[1][1], asset.m_triangles[1][2]);
    }
    if (K_MUTATION == 5) {
        asset.m_vertices[0].m_position[2] = .6F;
    }
    if (K_MUTATION == 6) {
        asset.m_vertices[0].m_position[0] = 2.5F;
    }
    const auto K_RESULT = simplify_fusion_mesh(asset);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_patches.empty());
    CHECK(K_RESULT->m_vertices.size() == asset.m_vertices.size());
    CHECK(K_RESULT->m_triangles == asset.m_triangles);
    CHECK(K_RESULT->m_distance_upper_bound == 0);
}

TEST_CASE("fan cleanup refuses to flatten a tetrahedron onto an existing face", "[cleanup]") {
    auto asset = fan_fixture();
    asset.m_triangles = { { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 1 }, { 1, 3, 2 } };
    const auto K_RESULT = simplify_fusion_mesh(asset);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->m_patches.empty());
    CHECK(K_RESULT->m_triangles == asset.m_triangles);
    CHECK(K_RESULT->m_vertices.size() == 4uz);  // Unreferenced source vertex is not a surface.
}

TEST_CASE("fan cleanup rejects rehashed forged and ambiguous certificates", "[cleanup]") {
    const auto K_ASSET = fan_fixture();
    const auto K_RESULT = simplify_fusion_mesh(K_ASSET);
    REQUIRE(K_RESULT);
    auto doc = nlohmann::json::parse(K_RESULT->m_document);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    switch (K_MUTATION) {
    case 0:
        doc["parent_asset_sha256"] = std::string(64uz, 'a');
        break;
    case 1:
        doc["error_bound"] = 1;
        break;
    case 2:
        doc["patches"][0]["vertex"] = -1;
        break;
    case 3:
        doc["patches"][0]["vertex"] = 999U;
        break;
    case 4:
        doc["patches"][0]["triangles"][0] = 3U;
        break;
    case 5:
        doc["patches"][0]["ring"][0] = 4U;
        break;
    case 6:
        doc["patches"][0]["barycentric_uv"][0] = -1;
        break;
    case 7:
        doc["patches"][0]["distance_upper_bound"] = 0;
        break;
    case 8:
        doc["patches"].push_back(doc["patches"][0]);
        break;
    case 9:
        doc["mesh_sha256"] = std::string(64uz, '0');
        break;
    case 10:
        doc["output_triangles"] = 4.0;
        break;
    case 11:
        doc["unknown"] = true;
        break;
    case 12:
        doc = nullptr;
        break;
    default:
        FAIL("Unknown cleanup mutation");
    }
    CHECK(replay(K_ASSET, doc.dump()).has_value() == false);
}

TEST_CASE("fan cleanup bounds JSON decoding cancellation and floating mode", "[cleanup]") {
    const auto K_ASSET = fan_fixture();
    const auto K_RESULT = simplify_fusion_mesh(K_ASSET);
    REQUIRE(K_RESULT);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3);
    auto text = K_RESULT->m_document;
    if (K_MUTATION == 0) {
        text += std::string(1uz, '\0') + "ignored";
    }
    if (K_MUTATION == 1) {
        text.insert(1uz, "\"schema_version\":1,");
    }
    if (K_MUTATION == 2) {
        text = std::string(8uz, '[') + "0" + std::string(8uz, ']');
    }
    if (K_MUTATION == 3) {
        text.resize(4uz * 1024uz * 1024uz + 1uz, ' ');
    }
    CHECK(replay(K_ASSET, text).has_value() == false);
    std::stop_source cancel;
    cancel.request_stop();
    const auto K_CANCELLED = simplify_fusion_mesh(K_ASSET, cancel.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);

    struct RoundingGuard {
        int m_old { std::fegetround() };

        ~RoundingGuard() {
            std::fesetround(m_old);
        }
    } guard;

    REQUIRE(std::fesetround(FE_DOWNWARD) == 0);
    CHECK(simplify_fusion_mesh(K_ASSET).has_value() == false);
}
