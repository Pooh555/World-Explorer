#include "../gpu/rebasing_fixture.hpp"
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

using namespace world_explorer;

TEST_CASE("synthetic floating origin preserves local float payload after joint world translation") {
    const auto baseline = rebasing_fixture({});
    REQUIRE(baseline);
    REQUIRE(validate_scene(**baseline));
    for (const auto name : {"zero", "positive", "negative"}) {
        const auto offset = rebasing_offset(name);
        REQUIRE(offset);
        const auto translated = rebasing_fixture(*offset);
        REQUIRE(translated);
        REQUIRE(validate_scene(**translated));
        const vec3d base_camera{0, 0, 6};
        const auto camera = translated_diagnostic_position(base_camera, *offset);
        REQUIRE(camera);
        for (const auto& [id, chunk] : (*baseline)->chunks) {
            const auto& shifted = (*translated)->chunks.at(id);
            for (std::size_t index = 0; index < chunk->splats.size(); ++index) {
                const auto& mean = chunk->splats[index].mean;
                const vec3d point{mean[0], mean[1], mean[2]};
                const auto expected = transform_point(chunk->world_from_chunk, point) - base_camera;
                const auto actual = transform_point(shifted->world_from_chunk, point) - *camera;
                REQUIRE(actual == expected);
                for (auto pair : {std::pair{actual.x, expected.x},
                                  {actual.y, expected.y},
                                  {actual.z, expected.z}})
                    REQUIRE(std::bit_cast<std::uint32_t>(static_cast<float>(pair.first)) ==
                            std::bit_cast<std::uint32_t>(static_cast<float>(pair.second)));
            }
        }
        const auto moved = translated_diagnostic_position({-.25, .125, 6.5}, *offset);
        REQUIRE(moved);
        REQUIRE(*moved - *camera == vec3d{-.25, .125, .5});
        if (name != std::string_view{"zero"}) {
            // Control: narrowing absolute positions first destroys this displacement.
            REQUIRE(static_cast<float>(moved->x) - static_cast<float>(camera->x) != -.25F);
        }
    }
}

TEST_CASE("synthetic floating origin rejects nonfinite and precision losing fixture translations") {
    REQUIRE(!rebasing_offset("unknown"));
    for (const auto invalid :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::max()}) {
        REQUIRE(!rebasing_fixture({invalid, 0, 0}));
        if (!std::isfinite(invalid))
            REQUIRE(!translated_diagnostic_position({invalid, 0, 0}, {}));
    }
    REQUIRE(!rebasing_fixture({0x1p60, 0, 0}));
    REQUIRE(!translated_diagnostic_position({.25, 0, 0}, {0x1p60, 0, 0}));
}
