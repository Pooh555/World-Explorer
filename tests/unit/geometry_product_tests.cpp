#include "../fixtures/geometry_product_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::geometry_product_test;

TEST_CASE(
    "geometry products own native high and runtime arrays with exact support and world mapping",
    "[geometry-product]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedGeometryProduct, SceneSnapshot>);
    GeometryFixture fixture;
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->mesh().m_positions.size() == 4uz);
    CHECK(K_RESULT->high_mesh().m_triangles.size() == 2uz);
    CHECK(K_RESULT->coverage() == std::array<std::uint64_t, 5> { 0, 1, 1, 0, 0 });
    CHECK(K_RESULT->support()[0uz].m_deviation_units == 0);
    CHECK(!K_RESULT->support()[0uz].m_uncertainty_units);
    CHECK(K_RESULT->synthetic_source());
    CHECK(!K_RESULT->closed_surface());
    CHECK(!K_RESULT->manual_ancestor());
    CHECK(transform_point(K_RESULT->world_from_geometry(), K_RESULT->mesh().m_positions[3uz]) ==
          Vec3d { 14, 2, 4 });
    fixture.source_mesh().m_vertices.clear();
    fixture.m_record.clear();
    fixture.m_selected.m_views.clear();
    CHECK(K_RESULT->high_mesh().m_vertices.size() == 4uz);
    CHECK(K_RESULT->mesh().m_positions.size() == 4uz);
}

TEST_CASE("geometry product support distinguishes absent UV uncertainty generated unknown and "
          "manual ancestry",
          "[geometry-product]") {
    const auto K_CLASS = GENERATE("UNKNOWN", "GENERATED", "MANUALLY_EDITED");
    const bool K_MANUAL = std::string_view(K_CLASS) == "MANUALLY_EDITED";
    GeometryFixture fixture(
        [&](Json& doc, auto&) {
            doc["mesh"]["uv"] = nullptr;
            doc["mesh"]["tangents"] = nullptr;
            for (auto& face : doc["support"]) {
                face["class"] = K_CLASS;
                face["anchors"] = nullptr;
                face["views"] = Json::array();
                face["uncertainty_units"] = nullptr;
            }
        },
        {},
        {},
        {},
        K_MANUAL);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->mesh().m_uv.empty());
    CHECK(K_RESULT->mesh().m_tangents.empty());
    CHECK(K_RESULT->manual_ancestor() == K_MANUAL);
    CHECK(K_RESULT->coverage()[K_MANUAL                                   ? 4uz
                               : std::string_view(K_CLASS) == "GENERATED" ? 3uz
                                                                          : 0uz] == 2);
}

TEST_CASE("geometry product rejects rehashed malformed mesh attributes indices topology and policy",
          "[geometry-product]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    GeometryFixture fixture([&](Json& doc, auto&) {
        auto& mesh = doc["mesh"];
        switch (K_CASE) {
        case 0:
            mesh["triangles"][0uz][0uz] = 999;
            break;
        case 1:
            mesh["triangles"][0uz] = { 0, 0, 2 };
            break;
        case 2:
            mesh["triangles"][1uz] = { 2, 1, 0 };
            break;
        case 3:
            mesh["normals"][0uz] = { 0, 0, -1 };
            break;
        case 4:
            mesh["normals"][0uz] = { 0, 0, 2 };
            break;
        case 5:
            mesh["positions"][1uz] = { 0, 0, 0 };
            break;
        case 6:
            mesh["uv"][0uz] = { -1, 0 };
            break;
        case 7:
            mesh["uv"][1uz] = mesh["uv"][0uz];
            break;
        case 8:
            mesh["tangents"][0uz] = { 0, 0, 1, 1 };
            break;
        case 9:
            mesh["tangents"][0uz][3uz] = 0;
            break;
        case 10:
            mesh["tangents"][0uz] = { -1, 0, 0, 1 };
            break;
        case 11:
            mesh["tangents"][0uz][3uz] = -1;
            break;
        case 12:
            mesh["groups"][0uz] = 0;
            break;
        case 13:
            mesh["groups"] = { 101 };
            break;
        case 14:
            mesh["tangents"] = nullptr;
            break;
        case 15:
            doc["surface"] = "CLOSED_MANIFOLD";
            break;
        case 16:
            doc["frame"] = "WORLD";
            break;
        case 17:
            doc["mesh"]["positions"][0uz][0uz] = 1e308;
            break;
        case 18:
            doc["schema_version"] = 2;
            break;
        case 19:
            doc["threshold_override"] = true;
            break;
        default:
            throw std::logic_error("Invalid generated test case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("geometry product refuses fabricated observed support stale source anchors and missing "
          "uncertainty semantics",
          "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    GeometryFixture fixture([&](Json& doc, auto&) {
        auto& face = doc["support"][0uz];
        switch (K_CASE) {
        case 0:
            face["anchors"] = nullptr;
            break;
        case 1:
            face["views"] = Json::array();
            break;
        case 2:
            face["anchors"][0uz]["triangle"] = 99;
            break;
        case 3:
            face["anchors"][0uz]["weights"] = { -.1, .1, 1 };
            break;
        case 4:
            face["anchors"][0uz]["weights"] = { .2, .2, .2 };
            break;
        case 5:
            face["anchors"][0uz]["triangle"] = 1;
            break;
        case 6:
            face["views"][0uz]["capture_id"] = 7;
            face["views"][0uz]["frame_id"] = 10;
            break;
        case 7:
            face["views"].push_back(face["views"][0uz]);
            break;
        case 8:
            face["uncertainty_units"] = -1;
            break;
        case 9:
            face["class"] = "GENERATED";
            break;
        case 10:
            face["class"] = "UNKNOWN";
            break;
        case 11:
            face["class"] = "MANUALLY_EDITED";
            break;
        case 12:
            face["class"] = "observed";
            break;
        case 13:
            doc["support"].erase(1uz);
            break;
        case 14:
            doc["mesh"]["positions"][0uz][2uz] = .01;
            break;
        default:
            throw std::logic_error("Invalid generated test case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("geometry product inferred correspondences enforce independent distance policy without "
          "observed relabeling",
          "[geometry-product]") {
    const double K_OFFSET = GENERATE(.125, .5);
    GeometryFixture fixture([&](Json& doc, auto&) {
        doc["support"][0uz]["class"] = "INFERRED";
        for (auto& point : doc["mesh"]["positions"]) {
            point[2uz] = point[2uz].get<double>() + K_OFFSET;
        }
    });
    const auto K_RESULT = fixture.inspect();
    if (K_OFFSET > .25) {
        CHECK(!K_RESULT);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->coverage()[1uz] == 0);
        CHECK(K_RESULT->coverage()[2uz] == 2);
        CHECK(K_RESULT->support()[0uz].m_deviation_units == K_OFFSET);
    }
}

TEST_CASE(
    "geometry products reject stale native scene chunk context role and missing source provenance",
    "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    GeometryFixture fixture;
    auto request = fixture.request();
    if (!request.m_selection) {
        throw std::logic_error("Missing fixture selection");
    }
    switch (K_CASE) {
    case 0:
        request.m_selection->m_native.m_scene_sha256 = hash("foreign");
        break;
    case 1:
        request.m_selection->m_native.m_generation++;
        break;
    case 2:
        request.m_selection->m_native.m_chunk_sha256 = hash("foreign chunk");
        break;
    case 3:
        request.m_expected.m_pose_revision = { 99 };
        break;
    case 4:
        request.m_selection->m_views = { fixture.m_capture.evaluation()[0uz].m_inputs.m_view };
        break;
    case 5:
        fixture.source_mesh().m_observed.reset();
        break;
    case 6:
        fixture.m_scene->m_scale = ScaleStatus::METRIC_MEASURED;
        break;
    case 7:
        fixture.m_chunk->m_geometry_valid = false;
        break;
    case 8:
        fixture.m_chunk->m_source_frames = { { 10 } };
        break;
    case 9:
        fixture.m_scene->m_provenance.clear();
        break;
    default:
        throw std::logic_error("Invalid generated test case");
    }
    CHECK(!inspect_geometry_product(request));
}

TEST_CASE("geometry products validate freshly rehashed native source pose and metadata bindings",
          "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    GeometryFixture fixture({}, [&](auto& scene, auto& chunk) {
        switch (K_CASE) {
        case 0:
            chunk.m_source_frames = { { 10 } };
            break;
        case 1:
            chunk.m_mesh->m_observed->m_context.m_pose_sha256 = hash("foreign poses");
            break;
        case 2:
            chunk.m_mesh->m_observed.reset();
            break;
        case 3:
            chunk.m_mesh->m_observed->m_manifest_json = "bad checksum";
            break;
        case 4:
            scene.m_provenance["source_kind"] = "unknown";
            break;
        default:
            throw std::logic_error("Invalid generated test case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("geometry product dependencies and evaluation aliases are rejected without reusing "
          "capture labels",
          "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    GeometryFixture fixture({}, {}, [&](auto& graph, const auto& selected) {
        auto& root = graph.m_products.back();
        auto& source = graph.m_products[graph.m_products.size() - 2uz];
        if (K_CASE == 0) {
            root.m_dependencies.pop_back();
        }
        if (K_CASE == 1) {
            source.m_dependencies.clear();
        }
        if (K_CASE == 2) {
            root.m_recipe_sha256 = graph.m_products[0uz].m_recipe_sha256;
        }
        if (K_CASE == 3) {
            const auto K_CAPTURE =
                std::ranges::find(graph.m_products, AssetProductId { 6004 }, [](const auto& node) {
                    return node.m_product.m_product;
                });
            REQUIRE(K_CAPTURE != graph.m_products.end());
            root.m_dependencies.push_back(K_CAPTURE->m_product);
        }
        (void)selected;
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("geometry product request and parser budgets refuse excess malformed bytes and "
          "independent pins",
          "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    GeometryFixture fixture({},
                            {},
                            {},
                            { [&](std::string& text) {
                                 if (K_CASE == 0) {
                                     text.insert(1uz, "\"schema_version\":1,");
                                 }
                                 if (K_CASE == 1) {
                                     text.push_back('\0');
                                 }
                                 if (K_CASE == 2) {
                                     text = std::string(18uz, '[') + "0" + std::string(18uz, ']');
                                 }
                                 if (K_CASE == 3) {
                                     text.resize(4uz * 1024uz * 1024uz + 1uz, ' ');
                                 }
                             },
                             {} });
    auto request = fixture.request();
    if (!request.m_selection) {
        throw std::logic_error("Missing fixture selection");
    }
    if (K_CASE == 4) {
        request.m_selection->m_views.resize(17uz, request.m_selection->m_views[0uz]);
    }
    if (K_CASE == 5) {
        request.m_selection->m_max_deviation_units = std::numeric_limits<double>::infinity();
    }
    if (K_CASE == 6) {
        request.m_capture.reset();
    }
    if (K_CASE == 7) {
        request.m_receipt.reset();
    }
    CHECK(!inspect_geometry_product(request));
}

TEST_CASE("geometry products atomically publish both owned records cancel replay and independently "
          "reload",
          "[geometry-product]") {
    GeometryFixture fixture;
    const auto K_PRODUCT = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_geometry_product(K_PRODUCT,
                                                   fixture.m_receipt,
                                                   fixture.directory(),
                                                   { 211 },
                                                   0,
                                                   K_CONTEXT,
                                                   stop.get_token(),
                                                   [&](std::size_t count) {
                                                       if (count == 1uz) {
                                                           stop.request_stop();
                                                       }
                                                   });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    const auto K_SNAPSHOT = checked(read_immutable_records(fixture.directory()));
    CHECK(K_SNAPSHOT.m_records.empty());
    const auto K_COMMIT = checked(publish_geometry_product(
        K_PRODUCT, fixture.m_receipt, fixture.directory(), { 211 }, 0, K_CONTEXT));
    CHECK(checked(publish_geometry_product(
                      K_PRODUCT, fixture.m_receipt, fixture.directory(), { 211 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_geometry_product(fixture.directory(), K_COMMIT, fixture.request()));
    auto stale = K_COMMIT;
    stale.m_generation++;
    CHECK(!read_geometry_product(fixture.directory(), stale, fixture.request()));
    CHECK(!publish_geometry_product(
        K_PRODUCT, fixture.m_receipt, fixture.directory(), { 212 }, 0, K_CONTEXT));
    CHECK(!publish_geometry_product(
        K_PRODUCT, fixture.m_receipt, fixture.m_receipt.source_directory(), { 211 }, 0, K_CONTEXT));
    auto wrong = fixture.request();
    if (!wrong.m_selection) {
        throw std::logic_error("Missing fixture selection");
    }
    wrong.m_selection->m_source.m_content_sha256 = hash("not committed");
    CHECK(!read_geometry_product(fixture.directory(), K_COMMIT, wrong));
}

TEST_CASE("geometry product pre cancellation owns data after source lifetime and rejects missing "
          "prerequisites",
          "[geometry-product]") {
    const auto K_PRODUCT = [] {
        GeometryFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        return checked(fixture.inspect());
    }();
    CHECK(K_PRODUCT.mesh().m_triangles.size() == 2uz);
    CHECK(K_PRODUCT.high_mesh().m_vertices.size() == 4uz);
    const auto K_MISSING = inspect_geometry_product({});
    REQUIRE(!K_MISSING);
    CHECK(K_MISSING.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(K_MISSING.error().m_message.find("Independent geometry selection") != std::string::npos);
}

TEST_CASE("geometry products check closed edge pairing and connected vertex fans",
          "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    GeometryFixture fixture([&](Json& doc, auto&) {
        auto& mesh = doc["mesh"];
        mesh["uv"] = nullptr;
        mesh["tangents"] = nullptr;
        if (K_CASE <= 1) {
            doc["surface"] = "CLOSED_MANIFOLD";
            mesh["positions"] = { { 1, 1, 1 }, { -1, -1, 1 }, { -1, 1, -1 }, { 1, -1, -1 } };
            const auto K_N = 1 / std::sqrt(3.0);
            mesh["normals"] = {
                { K_N, K_N, K_N }, { -K_N, -K_N, K_N }, { -K_N, K_N, -K_N }, { K_N, -K_N, -K_N } };
            mesh["triangles"] = { { 0, 2, 1 }, { 0, 1, 3 }, { 0, 3, 2 }, { 1, 2, 3 } };
            if (K_CASE == 1) {
                mesh["triangles"].erase(3uz);
            }
        } else {
            mesh["positions"] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 0, -1, 0 } };
            mesh["triangles"] = { { 0, 1, 2 }, { 0, 3, 4 } };
            if (K_CASE == 3) {
                mesh["positions"].push_back({ 0, 0, 0 });
                mesh["triangles"][1uz] = { 5, 3, 4 };
            }
            mesh["normals"] = std::vector<Json>(mesh["positions"].size(), Json { 0, 0, 1 });
        }
        mesh["groups"] = std::vector<Json>(mesh["triangles"].size(), 101);
        doc["support"] = std::vector<Json>(mesh["triangles"].size(),
                                           Json { { "class", "GENERATED" },
                                                 { "anchors", nullptr },
                                                 { "views", Json::array() },
                                                 { "uncertainty_units", nullptr } });
    });
    const auto K_RESULT = fixture.inspect();
    if (K_CASE == 1 || K_CASE == 2) {
        CHECK(!K_RESULT);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->closed_surface() == (K_CASE == 0));
        CHECK(K_RESULT->coverage()[3uz] == (K_CASE == 0 ? 4 : 2));
    }
}

TEST_CASE(
    "geometry products prevent finite local coordinates from overflowing the source world frame",
    "[geometry-product]") {
    GeometryFixture fixture([](Json& doc, auto&) {
        auto& mesh = doc["mesh"];
        mesh["positions"] = { { 1e308, 0, 0 }, { 1e308, 1, 0 }, { 1e308, 0, 1 } };
        mesh["normals"] = { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 } };
        mesh["uv"] = nullptr;
        mesh["tangents"] = nullptr;
        mesh["triangles"] = { { 0, 1, 2 } };
        mesh["groups"] = { 101 };
        doc["support"] = Json::array({ { { "class", "GENERATED" },
                                       { "anchors", nullptr },
                                       { "views", Json::array() },
                                       { "uncertainty_units", nullptr } } });
    });
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_message == "Geometry world transform overflow");
}

TEST_CASE(
    "geometry product observed coverage requires one source triangle for the entire output face",
    "[geometry-product]") {
    const bool K_OBSERVED = GENERATE(false, true);
    GeometryFixture fixture([&](Json& doc, auto&) {
        auto& face = doc["support"][0uz];
        face["class"] = K_OBSERVED ? "OBSERVED" : "INFERRED";
        face["anchors"][1uz] = { { "triangle", 1 }, { "weights", { 1, 0, 0 } } };
        face["anchors"][2uz] = { { "triangle", 1 }, { "weights", { 0, 0, 1 } } };
    });
    const auto K_RESULT = fixture.inspect();
    if (K_OBSERVED) {
        CHECK(!K_RESULT);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->coverage()[1uz] == 0);
        CHECK(K_RESULT->coverage()[2uz] == 2);
    }
}

TEST_CASE("geometry product array and native metadata limits precede owned copying",
          "[geometry-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    GeometryFixture fixture(
        [&](Json& doc, auto&) {
            if (K_CASE == 0) {
                doc["mesh"]["positions"] = std::vector<Json>(8193uz, Json { 0, 0, 0 });
            }
            if (K_CASE == 1) {
                doc["mesh"]["triangles"] = std::vector<Json>(8193uz, Json { 0, 1, 2 });
            }
            if (K_CASE == 2) {
                doc["support"] = std::vector<Json>(8193uz, nullptr);
            }
            if (K_CASE == 3) {
                doc["support"][0uz]["views"] = std::vector<Json>(17uz, nullptr);
            }
        },
        [&](auto& scene, auto& chunk) {
            if (K_CASE == 4) {
                if (!chunk.m_mesh || !chunk.m_mesh->m_observed) {
                    throw std::logic_error("Missing fixture metadata");
                }
                chunk.m_mesh->m_observed->m_manifest_json.resize(4uz * 1024uz * 1024uz + 1uz, ' ');
            }
            if (K_CASE == 5) {
                scene.m_provenance["oversized"] = std::string(4097uz, 'x');
            }
        });
    const auto K_RESULT = fixture.inspect();
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("geometry product exact byte limits remain publishable through the immutable registry",
          "[geometry-product]") {
    GeometryFixture fixture({},
                            {},
                            {},
                            { [](std::string& text) {
                                 text.resize(4uz * 1024uz * 1024uz, ' ');
                             },
                             [](std::string& text) {
                                 text.resize(16uz * 1024uz, ' ');
                             } });
    const auto K_PRODUCT = checked(fixture.inspect());
    const auto K_COMMIT = checked(publish_geometry_product(K_PRODUCT,
                                                           fixture.m_receipt,
                                                           fixture.directory(),
                                                           { 221 },
                                                           0,
                                                           fixture.m_receipt.graph().m_context));
    const auto K_RESULT = read_geometry_product(fixture.directory(), K_COMMIT, fixture.request());
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->mesh().m_triangles.size() == 2uz);
}
