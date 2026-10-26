#include "../fixtures/structure_graph_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::structure_graph_test;

TEST_CASE("structure graph admits actual oriented planar hole and explicit exterior adjacency",
          "[structure-graph]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedStructureGraph, SceneSnapshot>);
    StructureFixture fixture;
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    REQUIRE(K_RESULT->surfaces().size() == 1uz);
    const auto& surface = K_RESULT->surfaces()[0uz];
    CHECK(surface.m_faces.size() == 8uz);
    CHECK(surface.m_loops.size() == 2uz);
    CHECK(surface.m_coverage == std::array<std::uint64_t, 5> { 0, 8, 0, 0, 0 });
    REQUIRE(surface.m_plane);
    if (!surface.m_plane) {
        throw std::logic_error("Missing admitted structure plane");
    }
    CHECK(surface.m_plane->m_actual_residual_units == 0);
    CHECK(!surface.m_thickness_units);
    REQUIRE(K_RESULT->openings().size() == 1uz);
    CHECK(K_RESULT->openings()[0uz].m_class == AssetEvidenceClass::INFERRED);
    CHECK(K_RESULT->openings()[0uz].m_first_room == StructureRoomId { 21 });
    CHECK(!K_RESULT->openings()[0uz].m_second_room);
    CHECK(K_RESULT->rooms()[0uz].m_contains[0uz].m_actual_residual_units == 0);
    CHECK(K_RESULT->geometry_product() == fixture.m_geometry.selection().m_product);
    CHECK(K_RESULT->receipt_sha256() == fixture.m_receipt.receipt_sha256());
    CHECK(K_RESULT->synthetic_source());
    CHECK(!K_RESULT->manual_ancestor());
    fixture.m_record.clear();
    fixture.m_base.source_mesh().m_vertices.clear();
    CHECK(K_RESULT->surfaces()[0uz].m_loops[1uz] == std::vector<std::uint32_t> { 4, 7, 6, 5 });
}

TEST_CASE("structure graph distinguishes mesh patches thickness declarations and manual ancestry",
          "[structure-graph]") {
    const bool K_MESH = GENERATE(true, false);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        auto& surface = doc["surfaces"][0uz];
        surface["thickness"] = { { "source", K_MESH ? "DECLARED_GENERATED" : "DECLARED_INFERRED" },
                                { "value_units", .2 } };
        if (K_MESH) {
            surface["patch"] = "MESH";
            surface["plane"] = nullptr;
            doc["openings"] = Json::array();
        } else {
            doc["rooms"].push_back({ { "id", 22 }, { "boundaries", { 11 } }, { "contains", Json::array() } });
            doc["openings"][0uz]["rooms"] = { 21, 22 };
        }
    };
    StructureFixture fixture(changes, true);
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->manual_ancestor());
    CHECK(K_RESULT->surfaces()[0uz].m_plane.has_value() == !K_MESH);
    CHECK(K_RESULT->surfaces()[0uz].m_thickness_units == .2);
    if (!K_MESH) {
        CHECK(K_RESULT->openings()[0uz].m_second_room == StructureRoomId { 22 });
    }
}

TEST_CASE("structure graph rejects rehashed incomplete reversed duplicated and foreign boundaries",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        auto& surface = doc["surfaces"][0uz];
        switch (K_CASE) {
        case 0:
            surface["loops"][0uz] = { 0, 3, 2, 1 };
            break;
        case 1:
            surface["loops"].erase(1uz);
            break;
        case 2:
            surface["loops"][1uz] = { 4, 5, 6, 7 };
            break;
        case 3:
            surface["loops"][0uz] = { 0, 1, 2, 3, 0 };
            break;
        case 4:
            surface["loops"][0uz] = { 0, 1, 3, 2 };
            break;
        case 5:
            surface["loops"][1uz][0uz] = 999;
            break;
        case 6:
            surface["faces"].push_back(0);
            break;
        case 7:
            surface["faces"][0uz] = 99;
            break;
        case 8:
            surface["coverage"] = { 0, 0, 8, 0, 0 };
            break;
        case 9:
            surface["faces"] = { 0, 4 };
            surface["coverage"] = { 0, 2, 0, 0, 0 };
            break;
        case 10:
            doc["surfaces"].push_back(surface);
            break;
        case 11:
            surface["faces"] = Json::array();
            break;
        default:
            throw std::logic_error("Unexpected structure boundary case");
        }
    };
    StructureFixture fixture(changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("structure graph refuses invalid plane residuals thickness and profile claims",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        auto& surface = doc["surfaces"][0uz];
        switch (K_CASE) {
        case 0:
            surface["plane"]["normal"] = { 0, 0, -1 };
            break;
        case 1:
            surface["plane"]["normal"] = { 0, 0, 2 };
            break;
        case 2:
            surface["plane"]["offset_units"] = .1;
            break;
        case 3:
            surface["plane"]["residual_bound_units"] = .2;
            break;
        case 4:
            surface["thickness"]["value_units"] = .2;
            break;
        case 5:
            surface["thickness"] = { { "source", "MEASURED" }, { "value_units", .2 } };
            break;
        case 6:
            surface["thickness"] = { { "source", "DECLARED_GENERATED" }, { "value_units", 0 } };
            break;
        case 7:
            surface["patch"] = "CLOSED_ROOM";
            break;
        case 8:
            doc["scope"] = "COMPLETE_ROOM";
            break;
        case 9:
            doc["frame"] = "METRIC_WORLD";
            break;
        case 10:
            doc["manual_ancestor"] = true;
            break;
        case 11:
            surface["plane"]["gravity"] = true;
            break;
        case 12:
            surface["id"] = 0;
            break;
        default:
            throw std::logic_error("Unexpected structure plane case");
        }
    };
    StructureFixture fixture(changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("structure openings require exact distinct planar holes and consistent room links",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        auto& opening = doc["openings"][0uz];
        switch (K_CASE) {
        case 0:
            opening["loop"] = 0;
            break;
        case 1:
            opening["surface_id"] = 99;
            break;
        case 2:
            opening["rooms"] = { 21, 21 };
            break;
        case 3:
            opening["rooms"] = { 22, nullptr };
            break;
        case 4:
            opening["class"] = "OBSERVED";
            break;
        case 5:
            opening["class"] = "UNKNOWN";
            break;
        case 6:
            opening["confidence"] = 1.1;
            break;
        case 7:
            doc["openings"].push_back(opening);
            break;
        case 8:
            doc["openings"] = Json::array();
            break;
        case 9:
            doc["rooms"].push_back({ { "id", 22 }, { "boundaries", { 11 } }, { "contains", Json::array() } });
            break;
        case 10:
            doc["surfaces"][0uz]["patch"] = "MESH";
            doc["surfaces"][0uz]["plane"] = nullptr;
            break;
        case 11:
            opening["rooms"] = { nullptr, 21 };
            break;
        default:
            throw std::logic_error("Unexpected structure opening case");
        }
    };
    StructureFixture fixture(changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("structure relations validate native anchors independent gap bounds and directed support",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        split_patch(doc);
        if (K_CASE == 1) {
            doc["relations"][0uz]["kind"] = "CONTACT";
        }
        if (K_CASE == 2) {
            doc["relations"][0uz]["source_vertex"] = 0;
        }
        if (K_CASE == 3) {
            doc["relations"][0uz]["source_vertex"] = 2;
        }
        if (K_CASE == 4) {
            doc["relations"].push_back(doc["relations"][0uz]);
        }
        if (K_CASE == 5) {
            auto reverse = doc["relations"][0uz];
            reverse["source"] = 12;
            reverse["target"] = 11;
            doc["relations"].push_back(reverse);
        }
        if (K_CASE == 6) {
            doc["relations"][0uz]["residual_bound_units"] = .2;
        }
    };
    StructureFixture fixture(changes);
    const auto K_RESULT = fixture.inspect();
    if (K_CASE >= 2) {
        CHECK(!K_RESULT);
        return;
    }
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->relations()[0uz].m_actual_residual_units == 0);
    CHECK(K_RESULT->surfaces().size() == 2uz);
}

TEST_CASE(
    "structure room containment is checked against native boundary AABB with explicit residual",
    "[structure-graph]") {
    const bool K_OUTSIDE = GENERATE(true, false);
    StructureFixtureMutations changes;
    changes.m_record = [&](Json& doc, auto&) {
        split_patch(doc);
        doc["relations"] = Json::array();
        doc["rooms"][0uz]["boundaries"] = { K_OUTSIDE ? 11 : 12 };
        doc["rooms"][0uz]["contains"] =
            Json::array({ { { "surface_id", K_OUTSIDE ? 12 : 11 }, { "residual_bound_units", 0 } } });
    };
    StructureFixture fixture(changes);
    CHECK(fixture.inspect().has_value() == !K_OUTSIDE);
}

TEST_CASE("structure independent handles policy and receipt pins cannot be substituted",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    StructureFixture fixture;
    auto request = fixture.request();
    if (!request.m_selection) {
        throw std::logic_error("Missing structure selection");
    }
    switch (K_CASE) {
    case 0:
        request.m_selection.reset();
        break;
    case 1:
        request.m_geometry.reset();
        break;
    case 2:
        request.m_capture.reset();
        break;
    case 3:
        request.m_receipt.reset();
        break;
    case 4:
        request.m_expected.m_world_epoch = { 4 };
        break;
    case 5:
        request.m_geometry = checked(fixture.m_base.inspect());
        break;
    case 6:
        request.m_capture = fixture.m_base.m_capture;
        break;
    case 7:
        request.m_selection->m_max_plane_residual_units = std::numeric_limits<double>::quiet_NaN();
        break;
    case 8:
        request.m_selection->m_product.m_revision = 2;
        break;
    default:
        throw std::logic_error("Unexpected structure handle case");
    }
    CHECK(!inspect_structure_graph(request));
}

TEST_CASE(
    "structure exact receipt dependencies reject evaluation input aliases and missing geometry",
    "[structure-graph]") {
    const bool K_EVALUATION = GENERATE(true, false);
    StructureFixtureMutations changes;
    changes.m_graph = [&](auto& graph, const auto& selected) {
        const auto K_EVAL = graph.m_products[0uz].m_product;
        for (auto& node : graph.m_products) {
            if (node.m_product == selected.m_product) {
                if (K_EVALUATION) {
                    node.m_dependencies.push_back(K_EVAL);
                } else {
                    node.m_dependencies.clear();
                }
            }
        }
    };
    StructureFixture fixture(changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("structure strict bytes reject oversized duplicate NUL and deep JSON",
          "[structure-graph]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    StructureFixtureMutations changes;
    changes.m_bytes = [&](std::string& text) {
        if (K_CASE == 0) {
            text.resize(256uz * 1024uz, ' ');
        }
        if (K_CASE == 1) {
            text.resize(256uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 2) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 3) {
            text.push_back('\0');
        }
        if (K_CASE == 4) {
            text = std::string(17uz, '[') + "0" + std::string(17uz, ']');
        }
    };
    StructureFixture fixture(changes);
    const auto K_RESULT = fixture.inspect();
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        const auto K_COMMIT = checked(publish_structure_graph(*K_RESULT,
                                                              fixture.m_receipt,
                                                              fixture.directory(),
                                                              { 241 },
                                                              0,
                                                              fixture.m_receipt.graph().m_context));
        CHECK(read_structure_graph(fixture.directory(), K_COMMIT, fixture.request()));
    } else {
        CHECK(!K_RESULT);
    }
}

TEST_CASE("structure immutable cancellation replay and selected reload preserve original inputs",
          "[structure-graph]") {
    StructureFixture fixture;
    const auto K_STRUCTURE = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_structure_graph(K_STRUCTURE,
                                                  fixture.m_receipt,
                                                  fixture.directory(),
                                                  { 242 },
                                                  0,
                                                  K_CONTEXT,
                                                  stop.get_token(),
                                                  [&](std::size_t) {
                                                      stop.request_stop();
                                                  });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_structure_graph(
        K_STRUCTURE, fixture.m_receipt, fixture.directory(), { 242 }, 0, K_CONTEXT));
    CHECK(checked(publish_structure_graph(
                      K_STRUCTURE, fixture.m_receipt, fixture.directory(), { 242 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_structure_graph(fixture.directory(), K_COMMIT, fixture.request()));
    auto stale = K_COMMIT;
    ++stale.m_generation;
    CHECK(!read_structure_graph(fixture.directory(), stale, fixture.request()));
    CHECK(!publish_structure_graph(
        K_STRUCTURE, fixture.m_receipt, fixture.directory(), { 243 }, 0, K_CONTEXT));
    CHECK(!publish_structure_graph(
        K_STRUCTURE, fixture.m_receipt, fixture.m_receipt.source_directory(), { 242 }, 0, K_CONTEXT));
}

TEST_CASE("structure values survive source lifetime and pre cancellation refuses admission",
          "[structure-graph]") {
    const auto K_STRUCTURE = [] {
        StructureFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        return checked(fixture.inspect());
    }();
    CHECK(K_STRUCTURE.surfaces()[0uz].m_loops.size() == 2uz);
    CHECK(!inspect_structure_graph({}));
}
