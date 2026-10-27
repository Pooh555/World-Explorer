#include "../fixtures/native_lod_fixture.hpp"
#include "world_explorer/application/native_export.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <type_traits>

#if defined(__linux__)
#include <cstdlib>
#endif

using namespace world_explorer;
using namespace world_explorer::native_lod_test;

TEST_CASE("native LOD certifies complete affine fan and retained material face", "[native-lod]") {
    STATIC_REQUIRE(!std::is_default_constructible_v<AdmittedNativeLod>);
    LodFixture fixture;
    auto request = fixture.lod_request();
    const auto K_LOD = inspect_native_lod(request);
    INFO((K_LOD ? "admitted" : K_LOD.error().m_message));
    REQUIRE(K_LOD);
    CHECK(K_LOD->base().mesh().m_positions.size() == 5uz);
    CHECK(K_LOD->candidate().mesh().m_positions.size() == 4uz);
    CHECK(K_LOD->base().mesh().m_triangles.size() == 4uz);
    CHECK(K_LOD->candidate().mesh().m_triangles.size() == 2uz);
    CHECK(K_LOD->source_faces() == std::vector<std::vector<std::uint32_t>> { { 0, 1, 2 }, { 3 } });
    CHECK(K_LOD->candidate().synthetic_source());
    // These handles own admitted bytes; filesystem deletion cannot alter them.
    std::filesystem::remove_all(
        fixture.m_base.m_base.m_base.m_views.m_object.m_region.m_directory.m_root / "blobs");
    CHECK(inspect_native_lod(request));
    request.m_base.reset();
    request.m_candidate.reset();
    request.m_receipt.reset();
    CHECK(K_LOD->candidate().mesh().m_uv[2uz] == std::array<double, 2> { 0, 1 });
}

TEST_CASE("native LOD certificate identity invalidates definition and every instance reference",
          "[native-lod][native-assets]") {
    LodFixture first;
    LodFixture second({}, 19003);
    const auto K_FIRST_REQUEST = first.request();
    const auto K_SECOND_REQUEST = second.request();
    const auto K_FIRST = checked(inspect_native_assets(K_FIRST_REQUEST));
    const auto K_SECOND = checked(inspect_native_assets(K_SECOND_REQUEST));
    const auto& a = K_FIRST.instances().definitions()[0uz];
    const auto& b = K_SECOND.instances().definitions()[0uz];
    CHECK(a.definition().m_geometry == b.definition().m_geometry);
    CHECK(a.definition().m_lods == b.definition().m_lods);
    CHECK(a.definition().m_materials[0uz].m_product == b.definition().m_materials[0uz].m_product);
    CHECK(a.reference().m_sha256 != b.reference().m_sha256);
    CHECK(K_FIRST.instances().sha256() != K_SECOND.instances().sha256());
    for (const auto& instance : K_SECOND.instances().instances().m_instances) {
        CHECK(instance.m_definition == b.reference());
    }
    const auto K_COMMIT = checked(publish_native_assets(K_FIRST,
                                                        first.m_base.m_receipt,
                                                        first.m_base.directory(),
                                                        { 1 },
                                                        0,
                                                        K_FIRST_REQUEST.m_expected));
    CHECK(!read_native_assets(first.m_base.directory(), K_COMMIT, K_SECOND_REQUEST));
    CHECK(read_native_assets(first.m_base.directory(), K_COMMIT, K_FIRST_REQUEST));
}

TEST_CASE("native LOD rejects rehashed incomplete or false whole surface certificates",
          "[native-lod]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    INFO(K_CASE);
    LodFixture fixture([&](Json& doc, Json& candidate) {
        switch (K_CASE) {
        case 0:
            doc["faces"][1uz]["source_faces"] = { 2 };
            break;
        case 1:
            doc["faces"][0uz]["source_faces"] = { 0, 1 };
            break;
        case 2:
            doc["faces"][0uz]["center"] = 0;
            break;
        case 3:
            doc["faces"][0uz]["weights"] = { 63, 65, 128 };
            break;
        case 4:
            doc["faces"][0uz]["weights"] = { 0, 128, 128 };
            break;
        case 5:
            doc["faces"][0uz]["weights"] = { 64, 64, 127 };
            break;
        case 6:
            doc["vertex_sources"] = { 0, 1, 2, 2 };
            break;
        case 7:
            for (auto& uv : candidate["mesh"]["uv"]) {
                for (auto& component : uv) {
                    component = component.get<double>() * .5;
                }
            }
            break;
        case 8:
            candidate["mesh"]["groups"] = { 102, 102 };
            break;
        case 9:
            candidate["support"][0uz]["class"] = "INFERRED";
            candidate["support"][0uz]["uncertainty_units"] = .1;
            break;
        default:
            // A valid single native triangle drops an entire original face.
            for (auto name : { "positions", "normals", "uv", "tangents" }) {
                candidate["mesh"][name].erase(3uz);
            }
            candidate["mesh"]["triangles"] = Json::array({ { 0, 1, 2 } });
            candidate["mesh"]["groups"] = { 101 };
            candidate["support"].erase(1uz);
            doc["vertex_sources"] = { 0, 1, 2 };
            doc["faces"].erase(1uz);
            break;
        }
    });
    const auto K_REQUEST = fixture.lod_request();
    // Both source and candidate passed actual geometry/correspondence admission.
    REQUIRE(K_REQUEST.m_base);
    REQUIRE(K_REQUEST.m_candidate);
    const auto K_RESULT = inspect_native_lod(K_REQUEST);
    REQUIRE(!K_RESULT);
}

TEST_CASE("native LOD independently selected prerequisites context and cancellation refuse",
          "[native-lod]") {
    LodFixture fixture;
    auto request = fixture.lod_request();
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    switch (K_CASE) {
    case 0:
        request.m_selection.reset();
        break;
    case 1:
        request.m_base.reset();
        break;
    case 2:
        request.m_candidate.reset();
        break;
    case 3:
        request.m_receipt.reset();
        break;
    case 4:
        ++request.m_expected.m_pose_revision.m_value;
        break;
    case 5:
        request.m_selection = NativeLodSelection { fixture.m_selected.m_product, .1 };
        break;
    case 6:
        request.m_selection = NativeLodSelection { fixture.m_selected.m_product,
                                                  std::numeric_limits<double>::quiet_NaN() };
        break;
    case 7:
        request.m_selection =
            NativeLodSelection { fixture.m_base.m_base.m_base.m_selected.m_product, 0 };
        break;
    default:
        request.m_candidate = request.m_base;
        break;
    }
    CHECK(!inspect_native_lod(request));
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_CANCELLED = inspect_native_lod(fixture.lod_request(), cancellation.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("native LOD exact arithmetic refuses off grid and nonaffine center UVs", "[native-lod]") {
    const auto K_OFF_GRID = GENERATE(false, true);
    LodFixture fixture({}, 19001, [&](Json& doc, GeometryProductSelection&) {
        doc["mesh"]["uv"][4uz][0uz] = .25 + (K_OFF_GRID ? std::ldexp(1.0, -22) : .03125);
    });
    const auto K_REQUEST = fixture.lod_request();
    REQUIRE(K_REQUEST.m_base);
    REQUIRE(K_REQUEST.m_candidate);
    const auto K_RESULT = inspect_native_lod(K_REQUEST);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code ==
          (K_OFF_GRID ? ErrorCode::UNSUPPORTED_OPERATION : ErrorCode::INVALID_DATA));
}

TEST_CASE("native LOD rehashed root policy schema and selected source remain strict",
          "[native-lod]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    LodFixture fixture([&](Json& doc, Json&) {
        switch (K_CASE) {
        case 0:
            doc["schema_version"] = 2;
            break;
        case 1:
            doc["kind"] = "UNQUALIFIED_VERTEX_SUBSET";
            break;
        case 2:
            doc["pose_revision"] = 6;
            break;
        case 3:
            doc["base"]["revision"] = 2;
            break;
        case 4:
            doc["max_deviation_units"] = .1;
            break;
        case 5:
            doc["manual_ancestor"] = true;
            break;
        case 6:
            doc["extra"] = nullptr;
            break;
        default:
            doc["faces"][0uz]["weights"][0uz] = -1;
            break;
        }
    });
    CHECK(!inspect_native_lod(fixture.lod_request()));
}

TEST_CASE("native LOD assets bind complete provenance owned reload and preserve export refusal",
          "[native-lod][native-assets]") {
    LodFixture fixture;
    auto request = fixture.request();
    const auto K_ASSETS = inspect_native_assets(request);
    INFO((K_ASSETS ? "admitted" : K_ASSETS.error().m_message));
    REQUIRE(K_ASSETS);
    CHECK(K_ASSETS->sources()[0uz].m_lods.size() == 1uz);
    const auto K_COMMIT = checked(publish_native_assets(*K_ASSETS,
                                                        fixture.m_base.m_receipt,
                                                        fixture.m_base.directory(),
                                                        { 1 },
                                                        0,
                                                        request.m_expected));
    const auto K_RETRY = checked(publish_native_assets(*K_ASSETS,
                                                       fixture.m_base.m_receipt,
                                                       fixture.m_base.directory(),
                                                       { 1 },
                                                       0,
                                                       request.m_expected));
    CHECK(K_COMMIT.m_manifest_sha256 == K_RETRY.m_manifest_sha256);
    CHECK(read_native_assets(fixture.m_base.directory(), K_COMMIT, request));
    const auto K_BYTES = checked(read_immutable_records(fixture.m_base.directory()));
    const auto K_EXPORT =
        prepare_native_export({ *K_ASSETS, fixture.m_base.m_receipt, request.m_expected, {} });
    REQUIRE(!K_EXPORT);
    CHECK(K_EXPORT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
#if defined(__linux__)
    if (auto output = std::getenv("WE_NATIVE_LOD_PROBE_OUTPUT")) {
        const std::filesystem::path K_ROOT { output };
        REQUIRE(std::filesystem::create_directory(K_ROOT));
        const auto K_RECEIPT = checked(publish_asset_receipt(
            fixture.m_base.m_receipt, K_ROOT / "receipt", request.m_expected));
        const auto K_SAVED = checked(publish_native_assets(
            *K_ASSETS, fixture.m_base.m_receipt, K_ROOT / "native", { 1 }, 0, request.m_expected));
        auto reopened = request;
        reopened.m_receipt = checked(read_asset_receipt(K_RECEIPT, request.m_expected));
        CHECK(read_native_assets(K_ROOT / "native", K_SAVED, reopened));
    }
#endif
    request.m_assets[0uz].m_lods.clear();
    CHECK(!inspect_native_assets(request));
    CHECK(!read_native_assets(fixture.m_base.directory(), K_COMMIT, request));
    CHECK(checked(read_immutable_records(fixture.m_base.directory())).m_commit.m_manifest_sha256 ==
          K_BYTES.m_commit.m_manifest_sha256);
}

TEST_CASE("native LOD asset adoption rejects unbound certificate or face provenance",
          "[native-lod][native-assets]") {
    LodFixture fixture;
    auto request = fixture.request();
    auto& asset = request.m_assets[0uz];
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    switch (K_CASE) {
    case 0:
        asset.m_definition.m_sources.pop_back();
        break;
    case 1:
        asset.m_provenance.pop_back();
        break;
    case 2:
        asset.m_definition.m_lods[0uz] = asset.m_definition.m_geometry;
        break;
    default:
        asset.m_lods.push_back(asset.m_lods[0uz]);
        asset.m_definition.m_lods.push_back(asset.m_definition.m_lods[0uz]);
        break;
    }
    CHECK(!inspect_native_assets(request));
}
