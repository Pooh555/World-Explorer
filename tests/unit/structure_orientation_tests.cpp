#include "../fixtures/structure_graph_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;
using namespace world_explorer::structure_graph_test;

namespace {

Vec3d oriented(Vec3d point, int axis, bool negative) {
    const double K_SIGN = negative ? -1 : 1;
    switch (axis) {
    case 0:
        return { K_SIGN * point.m_z, point.m_x, K_SIGN * point.m_y };
    case 1:
        return { K_SIGN * point.m_y, K_SIGN * point.m_z, point.m_x };
    case 2:
        return { point.m_x, K_SIGN * point.m_y, K_SIGN * point.m_z };
    default:
        throw std::logic_error("Unexpected structure orientation axis");
    }
}

Json coordinates(Vec3d point) {
    return { point.m_x, point.m_y, point.m_z };
}

}  // namespace

TEST_CASE("structure planar openings retain winding on all six signed dominant axes",
          "[structure-graph]") {
    const auto K_AXIS = GENERATE(0, 1, 2);
    const bool K_NEGATIVE = GENERATE(true, false);
    const auto K_NORMAL = oriented({ 0, 0, 1 }, K_AXIS, K_NEGATIVE);
    const auto K_TANGENT = oriented({ 1, 0, 0 }, K_AXIS, K_NEGATIVE);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        doc["surfaces"][0uz]["plane"]["normal"] = coordinates(K_NORMAL);
    };
    StructureFixture fixture(
        changes,
        false,
        [&](Json& doc, auto&) {
            for (auto& row : doc["mesh"]["positions"]) {
                row = coordinates(oriented({ row[0uz].template get<double>(),
                                            row[1uz].template get<double>(),
                                            row[2uz].template get<double>() },
                                           K_AXIS,
                                           K_NEGATIVE));
            }
            doc["mesh"]["normals"] = std::vector<Json>(8uz, coordinates(K_NORMAL));
            doc["mesh"]["tangents"] =
                std::vector<Json>(8uz, Json { K_TANGENT.m_x, K_TANGENT.m_y, K_TANGENT.m_z, 1 });
        },
        [&](auto&, SceneChunk& chunk) {
            if (!chunk.m_mesh) {
                throw std::logic_error("Missing native orientation fixture mesh");
            }
            for (auto& vertex : chunk.m_mesh->m_vertices) {
                const auto K_POINT = oriented(
                    { vertex.m_position[0uz], vertex.m_position[1uz], vertex.m_position[2uz] },
                    K_AXIS,
                    K_NEGATIVE);
                vertex.m_position = { static_cast<float>(K_POINT.m_x),
                                     static_cast<float>(K_POINT.m_y),
                                     static_cast<float>(K_POINT.m_z) };
                vertex.m_normal = { static_cast<float>(K_NORMAL.m_x),
                                   static_cast<float>(K_NORMAL.m_y),
                                   static_cast<float>(K_NORMAL.m_z) };
            }
            chunk.m_bounds = { { -4, -4, -4 }, { 4, 4, 4 } };
        });
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->openings()[0uz].m_loop == 1);
    CHECK(K_RESULT->surfaces()[0uz].m_loops[0uz] == std::vector<std::uint32_t> { 0, 1, 2, 3 });
}

TEST_CASE("structure plane residual is recomputed and unknown opening confidence stays absent",
          "[structure-graph]") {
    StructureFixtureMutations changes;
    changes.m_record = [](Json& doc, auto&) {
        doc["surfaces"][0uz]["plane"]["offset_units"] = .1;
        doc["surfaces"][0uz]["plane"]["residual_bound_units"] = .1;
        doc["openings"][0uz]["class"] = "UNKNOWN";
        doc["openings"][0uz]["confidence"] = nullptr;
    };
    StructureFixture fixture(changes);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    const auto& plane = K_RESULT->surfaces()[0uz].m_plane;
    if (!plane) {
        throw std::logic_error("Missing admitted plane");
    }
    CHECK(plane->m_actual_residual_units == .1);
    CHECK(!K_RESULT->openings()[0uz].m_confidence);
}

TEST_CASE("structure room IDs memberships references and containment residuals are bounded",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        auto& room = doc["rooms"][0uz];
        switch (K_CASE) {
        case 0:
            room["id"] = 0;
            break;
        case 1:
            room["boundaries"] = { 11, 11 };
            break;
        case 2:
            room["boundaries"] = { 99 };
            break;
        case 3:
            room["contains"].push_back(room["contains"][0uz]);
            break;
        case 4:
            room["contains"][0uz]["residual_bound_units"] = -.1;
            break;
        case 5:
            doc["rooms"].push_back(room);
            break;
        case 6:
            doc["rooms"].push_back({ { "id", 22 }, { "boundaries", { 11 } }, { "contains", Json::array() } });
            doc["rooms"].push_back({ { "id", 23 }, { "boundaries", { 11 } }, { "contains", Json::array() } });
            break;
        default:
            throw std::logic_error("Unexpected structure room case");
        }
    };
    StructureFixture fixture(changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("structure count budgets refuse excess before typed ownership", "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        if (K_CASE == 0) {
            doc["surfaces"] = std::vector<Json>(33uz, doc["surfaces"][0uz]);
        }
        if (K_CASE == 1) {
            doc["rooms"] = std::vector<Json>(33uz, doc["rooms"][0uz]);
        }
        if (K_CASE == 2) {
            doc["openings"] = std::vector<Json>(65uz, doc["openings"][0uz]);
        }
        if (K_CASE == 3) {
            doc["relations"] = std::vector<Json>(129uz, Json::object());
        }
        if (K_CASE == 4) {
            doc["surfaces"][0uz]["loops"] =
                std::vector<Json>(17uz, doc["surfaces"][0uz]["loops"][0uz]);
        }
    };
    StructureFixture fixture(changes);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("structure mesh patch admits native nonplanar geometry without planar void claims",
          "[structure-graph]") {
    StructureFixtureMutations changes;
    changes.m_record = [](Json& doc, auto&) {
        doc["surfaces"][0uz]["patch"] = "MESH";
        doc["surfaces"][0uz]["plane"] = nullptr;
        doc["openings"] = Json::array();
    };
    StructureFixture fixture(
        changes,
        false,
        [](Json& doc, auto&) {
            doc["mesh"]["positions"][3uz][2uz] = .125;
        },
        [](auto&, SceneChunk& chunk) {
            if (!chunk.m_mesh) {
                throw std::logic_error("Missing native nonplanar mesh");
            }
            chunk.m_mesh->m_vertices[3uz].m_position[2uz] = .125F;
        });
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(!K_RESULT->surfaces()[0uz].m_plane);
    CHECK(K_RESULT->openings().empty());
}
