#include "../fixtures/native_completion_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::native_assets_test;

TEST_CASE("native assets adopt actual shared geometry groups materials and pivoted instances",
          "[native-assets]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedGeometryProduct, AdmittedNativeAssets>);
    NativeFixture fixture;
    auto request = fixture.request();
    const auto K_RESULT = inspect_native_assets(request);
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->synthetic_source());
    CHECK(K_RESULT->receipt_sha256() == fixture.m_receipt.receipt_sha256());
    CHECK(K_RESULT->instances().definitions().size() == 1uz);
    CHECK(K_RESULT->instances().instances().m_instances.size() == 2uz);
    CHECK(K_RESULT->sources()[0uz].m_materials.size() == 2uz);
    CHECK(native_geometry(K_RESULT->sources()[0uz]).mesh().m_groups ==
          std::vector<AssetMaterialVariantId> { { 101 }, { 102 } });
    CHECK(K_RESULT->sources()[0uz].m_materials[0uz].channels().size() == 6uz);
    CHECK(K_RESULT->sources()[0uz].m_materials[0uz].fit_inputs().size() == 2uz);
    const auto& instance = K_RESULT->instances().instances().m_instances[0uz];
    const auto& source = K_RESULT->sources()[0uz];
    for (auto vertex : native_geometry(source).mesh().m_positions) {
        CHECK(transform_point(
                  instance.m_world_from_asset,
                  transform_point(source.m_definition.m_pivot.m_asset_from_geometry, vertex)) ==
              transform_point(native_geometry(source).world_from_geometry(), vertex));
    }
    request.m_assets.clear();
    request.m_instances.m_instances.clear();
    CHECK(K_RESULT->sources().size() == 1uz);
    CHECK(K_RESULT->instances().instances().m_instances.size() == 2uz);
}

TEST_CASE("native asset metadata stores replay cancellation and current selection atomically",
          "[native-assets]") {
    NativeFixture fixture;
    const auto K_REQUEST = fixture.request();
    const auto K_ASSETS = checked(inspect_native_assets(K_REQUEST));
    std::stop_source cancellation;
    const auto K_INTERRUPTED = publish_native_assets(K_ASSETS,
                                                     fixture.m_receipt,
                                                     fixture.directory(),
                                                     { 1 },
                                                     0,
                                                     K_REQUEST.m_expected,
                                                     cancellation.get_token(),
                                                     [&](std::size_t) {
                                                         cancellation.request_stop();
                                                     });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_native_assets(
        K_ASSETS, fixture.m_receipt, fixture.directory(), { 1 }, 0, K_REQUEST.m_expected));
    CHECK(K_COMMIT.m_generation == 1);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.size() == 2uz);
    const auto K_REPLAY = checked(publish_native_assets(K_ASSETS,
                                                        fixture.m_receipt,
                                                        fixture.directory(),
                                                        { 1 },
                                                        0,
                                                        K_REQUEST.m_expected,
                                                        {},
                                                        [](std::size_t) {
                                                            FAIL("Replay must not write");
                                                        }));
    CHECK(K_REPLAY.m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    const auto K_READ = checked(read_native_assets(fixture.directory(), K_COMMIT, K_REQUEST));
    CHECK(K_READ.instances().sha256() == K_ASSETS.instances().sha256());
    auto wrong = K_COMMIT;
    ++wrong.m_generation;
    CHECK(!read_native_assets(fixture.directory(), wrong, K_REQUEST));
    wrong = K_COMMIT;
    wrong.m_manifest_sha256.assign(64uz, 'a');
    CHECK(!read_native_assets(fixture.directory(), wrong, K_REQUEST));

    auto changed = K_REQUEST;
    changed.m_instances.m_revision = { 2 };
    changed.m_instances.m_instances[1uz].m_world_from_asset.m_translation.m_x += 1;
    const auto K_NEXT = checked(inspect_native_assets(changed));
    CHECK(!publish_native_assets(
        K_NEXT, fixture.m_receipt, fixture.directory(), { 1 }, 0, K_REQUEST.m_expected));
    CHECK(!publish_native_assets(
        K_NEXT, fixture.m_receipt, fixture.directory(), { 2 }, 0, K_REQUEST.m_expected));
    const auto K_NEW = checked(publish_native_assets(
        K_NEXT, fixture.m_receipt, fixture.directory(), { 2 }, 1, K_REQUEST.m_expected));
    CHECK(K_NEW.m_generation == 2);
    CHECK(!read_native_assets(fixture.directory(), K_COMMIT, K_REQUEST));
    CHECK(read_native_assets(fixture.directory(), K_NEW, K_REQUEST));
    CHECK(read_native_assets(fixture.directory(), K_NEW, changed));
}

TEST_CASE("native assets reject stale foreign incomplete and unsupported selections",
          "[native-assets]") {
    const auto K_CASE = GENERATE(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
    NativeFixture fixture;
    auto request = fixture.request();
    auto& asset = request.m_assets[0uz];
    switch (K_CASE) {
    case 0:
        request.m_receipt.reset();
        break;
    case 1:
        asset.m_geometry.reset();
        break;
    case 2:
        asset.m_materials.clear();
        break;
    case 3:
        ++request.m_expected.m_world_epoch.m_value;
        break;
    case 4:
        ++request.m_expected.m_pose_revision.m_value;
        break;
    case 5:
        ++asset.m_definition.m_geometry.m_revision;
        break;
    case 6:
        asset.m_definition.m_materials[0uz].m_lighting = AssetLightingMode::GENERATED_PBR;
        break;
    case 7:
        asset.m_materials[1uz] = asset.m_materials[0uz];
        break;
    case 8:
        asset.m_provenance.pop_back();
        break;
    case 9:
        asset.m_definition.m_bounds.m_maximum.m_x += 1;
        rebind(request);
        break;
    case 10:
        asset.m_source_instance = { 2 };
        break;
    case 11:
        request.m_instances.m_instances[0uz].m_world_from_asset.m_translation.m_y += .01;
        break;
    case 12:
        request.m_instances.m_instances[0uz].m_world_from_asset.m_scale *= 1.01;
        break;
    case 13:
        request.m_instances.m_instances[0uz].m_visible = false;
        break;
    case 14:
        asset.m_definition.m_lods = { asset.m_definition.m_geometry };
        break;
    case 15:
        asset.m_definition.m_collider = { asset.m_definition.m_geometry,
                                         AssetColliderMode::UNVERIFIED };
        break;
    case 16:
        asset.m_definition.m_sources.pop_back();
        break;
    case 17:
        request.m_instances.m_instances[0uz].m_room = asset.m_definition.m_geometry;
        break;
    case 18:
        request.m_assets.clear();
        break;
    case 19:
        request.m_assets.resize(17uz);
        break;
    case 20:
        request.m_instances.m_instances.resize(65uz);
        break;
    case 21:
        asset.m_definition.m_units.m_status = AssetUnitStatus::DECLARED_METRIC;
        break;
    case 22:
        asset.m_definition.m_pivot.m_asset_from_geometry.m_translation.m_x =
            std::numeric_limits<double>::infinity();
        break;
    case 23: {
        MaterialFixture foreign;
        asset.m_materials[0uz] = checked(foreign.inspect());
        break;
    }
    default:
        throw std::logic_error("Unexpected native asset case");
    }
    CHECK(!inspect_native_assets(request));
    CHECK(!std::filesystem::exists(fixture.directory()));
}

TEST_CASE("native asset provenance cannot relabel actual face support or channel texels",
          "[native-assets]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    NativeFixture fixture(false, [&](Json& doc, bool geometry) {
        if (geometry && K_CASE == 0) {
            std::swap(doc["spans"][0uz]["class"], doc["spans"][1uz]["class"]);
        } else if ((geometry && K_CASE == 1) || (!geometry && K_CASE == 3)) {
            doc["spans"][0uz]["confidence"] = .9;
        } else if (!geometry && K_CASE == 2) {
            doc["spans"][0uz]["class"] = "OBSERVED";

        } else if (!geometry && K_CASE == 4) {
            doc["domain"]["elements"] = 2;
            doc["domain"]["width"] = 1;
            doc["spans"][0uz]["count"] = 2;
        }
    });
    auto request = fixture.request();
    if (K_CASE == 4) {
        for (auto& pin : request.m_assets[0uz].m_provenance) {
            if (pin.m_region.m_domain.m_kind == AssetRegionKind::MATERIAL_TEXELS) {
                pin.m_region.m_domain = { AssetRegionKind::MATERIAL_TEXELS, 2, 1, 2 };
            }
        }
        rebind(request);
    }
    CHECK(!inspect_native_assets(request));
}

TEST_CASE("native asset ancestry remains on shared manually edited materials and instances",
          "[native-assets]") {
    NativeFixture fixture(true);
    auto request = fixture.request();
    const auto K_ADMITTED = inspect_native_assets(request);
    INFO((K_ADMITTED ? "admitted" : K_ADMITTED.error().m_message));
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->instances().definitions()[0uz].definition().m_manual_ancestor);
    CHECK(K_ADMITTED->instances().instances().m_manual_ancestor);
    for (const auto& material : K_ADMITTED->sources()[0uz].m_materials) {
        CHECK(material.manual_ancestor());
        CHECK(material.channels()
                  .at(MaterialChannelRole::BASE_COLOR)
                  .m_provenance.m_manual_ancestor_elements == 4);
    }
    request.m_instances.m_instances[1uz].m_manual_ancestor = false;
    CHECK(!inspect_native_assets(request));
}

TEST_CASE(
    "native asset publication protects inputs rejects foreign output and respects cancellation",
    "[native-assets]") {
    NativeFixture fixture;
    const auto K_REQUEST = fixture.request();
    const auto K_ADMITTED = checked(inspect_native_assets(K_REQUEST));
    CHECK(!publish_native_assets(K_ADMITTED,
                                 fixture.m_receipt,
                                 fixture.m_receipt.source_directory(),
                                 { 1 },
                                 0,
                                 K_REQUEST.m_expected));
    std::filesystem::create_directory(fixture.directory());
    write(fixture.directory() / "caller.txt", "caller owned");
    CHECK(!publish_native_assets(
        K_ADMITTED, fixture.m_receipt, fixture.directory(), { 1 }, 0, K_REQUEST.m_expected));
    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK(!inspect_native_assets(K_REQUEST, cancellation.get_token()));
    auto stale = K_REQUEST.m_expected;
    ++stale.m_pose_revision.m_value;
    CHECK(!publish_native_assets(
        K_ADMITTED, fixture.m_receipt, fixture.m_output.m_root / "stale", { 1 }, 0, stale));
    MaterialFixture foreign;
    CHECK(!publish_native_assets(K_ADMITTED,
                                 foreign.m_receipt,
                                 fixture.m_output.m_root / "foreign",
                                 { 1 },
                                 0,
                                 K_REQUEST.m_expected));
    CHECK(!std::filesystem::exists(fixture.m_output.m_root / "foreign"));
}

TEST_CASE("native completed object and shell adoption requires the selected candidate and subject",
          "[native-assets]") {
    const auto K_SUBJECT =
        GENERATE(CompletionSubject::OBJECT_GEOMETRY, CompletionSubject::SHELL_GEOMETRY);
    CompletedNativeFixture fixture(K_SUBJECT);
    auto request = fixture.request();
    const auto K_ADMITTED = inspect_native_assets(request);
    INFO((K_ADMITTED ? "admitted" : K_ADMITTED.error().m_message));
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->sources()[0uz].m_completion.has_value());
    CHECK(native_geometry(K_ADMITTED->sources()[0uz]).support().size() == 10uz);
    CHECK(native_geometry(K_ADMITTED->sources()[0uz]).coverage() ==
          std::array<std::uint64_t, 5> { 0, 8, 0, 2, 0 });
    const auto K_DIRECTORY = fixture.m_output.m_root / "completed-assets";
    const auto K_COMMIT = checked(publish_native_assets(
        *K_ADMITTED, fixture.m_receipt, K_DIRECTORY, { 1 }, 0, request.m_expected));
    CHECK(read_native_assets(K_DIRECTORY, K_COMMIT, request));
    request.m_assets[0uz].m_definition.m_kind =
        K_SUBJECT == CompletionSubject::OBJECT_GEOMETRY ? AssetKind::STRUCTURAL : AssetKind::PROP;
    CHECK(!inspect_native_assets(request));
    request = fixture.request();
    request.m_assets[0uz].m_completion.reset();
    CHECK(!inspect_native_assets(request));
    request = fixture.request();
    auto capture = checked(fixture.m_base.m_base.m_base.m_views.inspect(fixture.m_receipt, false));
    auto geometry = fixture.m_base.m_base.m_base.request();
    geometry.m_selection = fixture.m_base.m_geometry_pins[1uz];
    geometry.m_receipt = fixture.m_receipt;
    geometry.m_capture = capture;
    request.m_assets[0uz].m_geometry = checked(inspect_geometry_product(geometry));
    request.m_assets[0uz].m_definition.m_geometry = geometry.m_selection->m_product;
    CHECK(!inspect_native_assets(request));
}

TEST_CASE("native assets preserve captured appearance without claiming a PBR material",
          "[native-assets]") {
    NativeFixture fixture(false, {}, AssetLightingMode::CAPTURED_APPEARANCE);
    auto request = fixture.request();
    const auto K_ADMITTED = checked(inspect_native_assets(request));
    for (const auto& material : K_ADMITTED.sources()[0uz].m_materials) {
        CHECK(material.selection().m_lighting == AssetLightingMode::CAPTURED_APPEARANCE);
        CHECK(material.channels().size() == 1uz);
        CHECK(material.channels().contains(MaterialChannelRole::CAPTURED_RGB));
        CHECK(material.fit_inputs().empty());
    }
    request.m_assets[0uz].m_definition.m_materials[0uz].m_lighting =
        AssetLightingMode::ESTIMATED_PBR;
    rebind(request);
    CHECK(!inspect_native_assets(request));
}

TEST_CASE("native adoption binds every face group and preserves distinct semantic asset identities",
          "[native-assets]") {
    NativeFixture fixture;
    auto request = fixture.request();
    auto& asset = request.m_assets[0uz];
    const auto K_REMOVED = asset.m_definition.m_materials.back().m_product;
    asset.m_definition.m_materials.pop_back();
    asset.m_materials.pop_back();
    const auto K_REGION = std::ranges::find_if(asset.m_provenance, [&](const auto& item) {
        return item.m_region.m_target == K_REMOVED;
    });
    REQUIRE(K_REGION != asset.m_provenance.end());
    std::erase(asset.m_definition.m_provenance, K_REGION->m_product);
    asset.m_provenance.erase(K_REGION);
    rebind(request);
    CHECK(!inspect_native_assets(request));

    request = fixture.request();
    auto duplicate = request.m_assets[0uz];
    duplicate.m_definition.m_asset = { 43 };
    duplicate.m_source_instance = { 3 };
    const auto K_DEFINITION = checked(admit_asset_definition(
        duplicate.m_definition, fixture.m_receipt, duplicate.m_provenance, request.m_expected));
    auto instance = request.m_instances.m_instances[0uz];
    instance.m_instance = { 3 };
    instance.m_definition = K_DEFINITION.reference();
    request.m_instances.m_instances.push_back(instance);
    request.m_assets.push_back(duplicate);
    const auto K_ADMITTED = checked(inspect_native_assets(request));
    CHECK(K_ADMITTED.instances().definitions().size() == 2uz);
    CHECK(K_ADMITTED.instances().instances().m_instances.size() == 3uz);
    CHECK(K_ADMITTED.instances().definitions()[0uz].reference().m_asset !=
          K_ADMITTED.instances().definitions()[1uz].reference().m_asset);
}

TEST_CASE("native pivot rotation preserves the original world frame only with atomic compensation",
          "[native-assets]") {
    NativeFixture fixture;
    auto request = fixture.request();
    auto& asset = request.m_assets[0uz];
    asset.m_definition.m_pivot.m_asset_from_geometry.m_rotation = { 0, 0, 0, 1 };
    asset.m_definition.m_bounds = { { -3, -5, -3 }, { -1, -2, -2.5 } };
    rebind(request);
    CHECK(!inspect_native_assets(request));
    auto& original = request.m_instances.m_instances[0uz].m_world_from_asset;
    original.m_rotation = { 0, 0, 0, -1 };
    original.m_translation = { 8, -8, 9 };
    const auto K_ADMITTED = checked(inspect_native_assets(request));
    CHECK(K_ADMITTED.instances().instances().m_instances[0uz].m_world_from_asset.m_translation ==
          Vec3d { 8, -8, 9 });
    const auto K_DIRECTORY = fixture.directory();
    const auto K_COMMIT = checked(publish_native_assets(
        K_ADMITTED, fixture.m_receipt, K_DIRECTORY, { 1 }, 0, request.m_expected));
    CHECK(read_native_assets(K_DIRECTORY, K_COMMIT, request));
}
