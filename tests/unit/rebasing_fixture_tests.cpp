#include "../gpu/rebasing_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <limits>

using namespace world_explorer;

TEST_CASE("synthetic floating origin preserves local float payload after joint world translation") {
    const auto K_BASELINE = rebasing_fixture({});
    REQUIRE(K_BASELINE);
    REQUIRE(validate_scene(**K_BASELINE));
    for (const auto K_NAME : { "zero", "positive", "negative" }) {
        const auto K_OFFSET = rebasing_offset(K_NAME);
        REQUIRE(K_OFFSET);
        const auto K_TRANSLATED = rebasing_fixture(*K_OFFSET);
        REQUIRE(K_TRANSLATED);
        REQUIRE(validate_scene(**K_TRANSLATED));
        const Vec3d K_BASE_CAMERA { 0, 0, 6 };
        const auto K_CAMERA = translated_diagnostic_position(K_BASE_CAMERA, *K_OFFSET);
        REQUIRE(K_CAMERA);
        for (const auto& [K_ID, K_CHUNK] : (*K_BASELINE)->m_chunks) {
            const auto& shifted = (*K_TRANSLATED)->m_chunks.at(K_ID);
            for (std::size_t index { 0uz }; index < K_CHUNK->m_splats.size(); ++index) {
                const auto& mean = K_CHUNK->m_splats[index].m_mean;
                const Vec3d K_POINT { mean[0], mean[1], mean[2] };
                const auto K_EXPECTED =
                    transform_point(K_CHUNK->m_world_from_chunk, K_POINT) - K_BASE_CAMERA;
                const auto K_ACTUAL =
                    transform_point(shifted->m_world_from_chunk, K_POINT) - *K_CAMERA;
                REQUIRE(K_ACTUAL == K_EXPECTED);
                for (auto pair : { std::pair { K_ACTUAL.m_x, K_EXPECTED.m_x },
                                  { K_ACTUAL.m_y, K_EXPECTED.m_y },
                                  { K_ACTUAL.m_z, K_EXPECTED.m_z } }) {
                    REQUIRE(std::bit_cast<std::uint32_t>(static_cast<float>(pair.first)) ==
                            std::bit_cast<std::uint32_t>(static_cast<float>(pair.second)));
                }
            }
        }
        const auto K_MOVED = translated_diagnostic_position({ -.25, .125, 6.5 }, *K_OFFSET);
        REQUIRE(K_MOVED);
        REQUIRE(*K_MOVED - *K_CAMERA == Vec3d { -.25, .125, .5 });
        if (K_NAME != std::string_view { "zero" }) {
            // Control: narrowing absolute positions first destroys this displacement.
            REQUIRE(static_cast<float>(K_MOVED->m_x) - static_cast<float>(K_CAMERA->m_x) != -.25F);
        }
    }
}

TEST_CASE("synthetic floating origin rejects nonfinite and precision losing fixture translations") {
    REQUIRE(!rebasing_offset("unknown"));
    for (const auto K_INVALID : { std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::max() }) {
        REQUIRE(!rebasing_fixture({ K_INVALID, 0, 0 }));
        if (!std::isfinite(K_INVALID)) {
            REQUIRE(!translated_diagnostic_position({ K_INVALID, 0, 0 }, {}));
        }
    }
    REQUIRE(!rebasing_fixture({ 0x1p60, 0, 0 }));
    REQUIRE(!translated_diagnostic_position({ .25, 0, 0 }, { 0x1p60, 0, 0 }));
}
