#include "../fixtures/native_collider_fixture.hpp"
#include "world_explorer/application/native_export.hpp"
#include "world_explorer/collision/triangle_cook.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::native_collider_test;
using world_explorer::asset_component_test::tree;

namespace {

template <class T> const T& required(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required collider fixture value missing");
    }
    return *value;
}

}  // namespace

TEST_CASE("native collider owns exact remapped static geometry and deterministic cook input",
          "[native-collider]") {
    STATIC_REQUIRE(!std::is_default_constructible_v<AdmittedNativeCollider>);
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedNativeCollider, SceneSnapshot>);
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedNativeCollider,
                                          std::shared_ptr<const CanonicalCollisionMesh>>);
    ColliderFixture fixture;
    auto input = fixture.collider();
    const auto K_COLLIDER = inspect_native_collider(input);
    INFO((K_COLLIDER ? "admitted" : K_COLLIDER.error().m_message));
    REQUIRE(K_COLLIDER);
    CHECK(K_COLLIDER->mesh().m_positions.size() == 4uz);
    CHECK(K_COLLIDER->mesh().m_triangles.size() == 2uz);
    CHECK(K_COLLIDER->mesh().m_source_faces == std::vector<std::uint32_t> { 1, 0 });
    CHECK(K_COLLIDER->mesh().m_positions[0uz] == Vec3d { 2, 3, .5 });
    CHECK(K_COLLIDER->digest() == sha256(K_COLLIDER->canonical_bytes()));
    CHECK(K_COLLIDER->digest() == checked(inspect_native_collider(input)).digest());
    auto& source = fixture.m_base.m_base.m_base;
    source.source_mesh().m_vertices[0uz].m_position[0uz] = 1;
    std::filesystem::remove_all(source.m_views.m_object.m_region.m_directory.m_root / "blobs");
    CHECK(inspect_native_collider(input));
    input.m_base.reset();
    input.m_receipt.reset();
    CHECK(K_COLLIDER->mesh().m_positions[0uz] == Vec3d { 2, 3, .5 });
    CHECK(K_COLLIDER->base().mesh().m_positions[0uz] == Vec3d {});
}

TEST_CASE("native collider refuses rehashed topology policy and authority substitution",
          "[native-collider]") {
    GeometryFixture geometry;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    const auto K_STAGED = append_collider(geometry, [&](Json& root, NativeColliderSelection&) {
        switch (K_CASE) {
        case 0:
            root["purpose"] = "MEASURED_WALKING";
            break;
        case 1:
            root["body"] = "DYNAMIC_PROP";
            break;
        case 2:
            root["units"] = "METRIC_MEASURED";
            break;
        case 3:
            root["pose_revision"] = 6;
            break;
        case 4:
            root["base"]["revision"] = 2;
            break;
        case 5:
            root["schema_version"] = 2;
            break;
        case 6:
            root["vertex_sources"][1uz] = root["vertex_sources"][0uz];
            break;
        case 7:
            root["faces"][1uz]["source_face"] = root["faces"][0uz]["source_face"];
            break;
        case 8:
            std::swap(root["faces"][0uz]["vertices"][0uz], root["faces"][0uz]["vertices"][1uz]);
            break;
        case 9:
            root["faces"].erase(1uz);
            break;
        case 10:
            root["faces"][0uz]["vertices"][0uz] = 4;
            break;
        case 11:
            root["cook"]["weld_vertices"] = true;
            break;
        case 12:
            root["manual_ancestor"] = true;
            break;
        default:
            root["extra"] = nullptr;
            break;
        }
    });
    const auto K_RECEIPT = freeze_collider(geometry);
    const auto K_REQUEST = collider_request(geometry, K_RECEIPT, K_STAGED.first);
    REQUIRE(K_REQUEST.m_base);
    CHECK(!inspect_native_collider(K_REQUEST));
}

TEST_CASE(
    "native collider strict root bytes reject duplicate deep NUL oversized and truncated input",
    "[native-collider]") {
    GeometryFixture geometry;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    const auto K_STAGED = append_collider(geometry, {}, true, 20000, [&](std::string& text) {
        switch (K_CASE) {
        case 0:
            text.insert(1uz, "\"schema_version\":1,");
            break;
        case 1:
            text.insert(1uz,
                        "\"deep\":" + std::string(18uz, '[') + "0" + std::string(18uz, ']') + ",");
            break;
        case 2:
            text.push_back('\0');
            break;
        case 3:
            text.append(1024uz * 1024uz, ' ');
            break;
        default:
            text.pop_back();
            break;
        }
    });
    const auto K_RECEIPT = freeze_collider(geometry);
    CHECK(!inspect_native_collider(collider_request(geometry, K_RECEIPT, K_STAGED.first)));
}

TEST_CASE("native collider independent budgets require exact selected policy and complete base",
          "[native-collider]") {
    GeometryFixture geometry;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    const auto K_STAGED =
        append_collider(geometry, [&](Json& root, NativeColliderSelection& selection) {
            selection.m_policy.m_max_vertices = K_CASE == 1 ? 3uz : K_CASE == 3 ? 8193uz : 4uz;
            selection.m_policy.m_max_triangles = K_CASE == 2 ? 1uz : 2uz;
            selection.m_policy.m_double_sided = K_CASE == 4;
            root["cook"]["max_vertices"] = selection.m_policy.m_max_vertices;
            root["cook"]["max_triangles"] = selection.m_policy.m_max_triangles;
            root["cook"]["double_sided"] = selection.m_policy.m_double_sided;
            if (K_CASE == 5) {
                root["cook"]["max_vertices"] = 8192;
            }
        });
    const auto K_RECEIPT = freeze_collider(geometry);
    const auto K_RESULT =
        inspect_native_collider(collider_request(geometry, K_RECEIPT, K_STAGED.first));
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->selection().m_policy.m_max_vertices == 4uz);
    } else {
        CHECK(!K_RESULT);
    }
}

TEST_CASE("native collider preserves source holes and rejects a bridge across their interior",
          "[native-collider]") {
    const auto K_BRIDGE = GENERATE(false, true);
    GeometryFixture geometry(structure_graph_test::ring_geometry,
                             structure_graph_test::ring_source);
    const auto K_STAGED = append_collider(geometry, [&](Json& root, NativeColliderSelection&) {
        if (K_BRIDGE) {
            root["faces"][0uz]["vertices"] = { 3, 2, 1 };
        }
    });
    const auto K_RECEIPT = freeze_collider(geometry);
    const auto K_COLLIDER =
        inspect_native_collider(collider_request(geometry, K_RECEIPT, K_STAGED.first));
    if (K_BRIDGE) {
        CHECK(!K_COLLIDER);
    } else {
        REQUIRE(K_COLLIDER);
        double area {};
        for (const auto& triangle : K_COLLIDER->mesh().m_triangles) {
            const auto& vertices = K_COLLIDER->mesh().m_positions;
            const auto K_CROSS = cross(vertices[triangle[1uz]] - vertices[triangle[0uz]],
                                       vertices[triangle[2uz]] - vertices[triangle[0uz]]);
            area += K_CROSS.m_z / 2;
        }
        CHECK(area == 12);
        CHECK(K_COLLIDER->mesh().m_triangles.size() == 8uz);
    }
}

TEST_CASE("native collider requires original prerequisites context parent and declared ancestry",
          "[native-collider]") {
    ColliderFixture fixture;
    auto request = fixture.collider();
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    switch (K_CASE) {
    case 0:
        request.m_selection.reset();
        break;
    case 1:
        request.m_base.reset();
        break;
    case 2:
        request.m_receipt.reset();
        break;
    case 3:
        ++request.m_expected.m_pose_revision.m_value;
        break;
    default:
        request.m_selection =
            NativeColliderSelection { fixture.m_base.m_base.m_base.m_selected.m_product, {} };
        break;
    }
    CHECK(!inspect_native_collider(request));
    std::stop_source stopped;
    stopped.request_stop();
    const auto K_CANCELLED = inspect_native_collider(fixture.collider(), stopped.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    GeometryFixture manual({}, {}, {}, {}, true);
    const auto K_MANUAL = append_collider(manual);
    const auto K_RECEIPT = freeze_collider(manual);
    CHECK(checked(inspect_native_collider(collider_request(manual, K_RECEIPT, K_MANUAL.first)))
              .base()
              .manual_ancestor());
    GeometryFixture parent;
    const auto K_WRONG = append_collider(parent, {}, true, 20000, {}, true);
    const auto K_PARENT_RECEIPT = freeze_collider(parent);
    CHECK(!inspect_native_collider(collider_request(parent, K_PARENT_RECEIPT, K_WRONG.first)));
}

TEST_CASE("native collider stores root and cook atomically and refuses rehashed foreign cook bytes",
          "[native-collider]") {
    ColliderFixture fixture;
    const auto K_REQUEST = fixture.collider();
    const auto K_COLLIDER = checked(inspect_native_collider(K_REQUEST));
    const auto K_SOURCE = tree(fixture.m_base.m_receipt.source_directory());
    const auto K_DIRECTORY = fixture.m_base.m_output.m_root / "collider";
    std::stop_source stopped;
    const auto K_INTERRUPTED = publish_native_collider(K_COLLIDER,
                                                       fixture.m_base.m_receipt,
                                                       K_DIRECTORY,
                                                       { 1 },
                                                       0,
                                                       K_REQUEST.m_expected,
                                                       stopped.get_token(),
                                                       [&](std::size_t) {
                                                           stopped.request_stop();
                                                       });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(K_DIRECTORY)).m_records.empty());
    const auto K_COMMIT = checked(publish_native_collider(
        K_COLLIDER, fixture.m_base.m_receipt, K_DIRECTORY, { 1 }, 0, K_REQUEST.m_expected));
    CHECK(checked(
              publish_native_collider(
                  K_COLLIDER, fixture.m_base.m_receipt, K_DIRECTORY, { 1 }, 0, K_REQUEST.m_expected))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(checked(read_native_collider(K_DIRECTORY, K_COMMIT, K_REQUEST)).digest() ==
          K_COLLIDER.digest());
    const auto K_SAVED = checked(read_immutable_records(K_DIRECTORY));
    REQUIRE(K_SAVED.m_records.size() == 2uz);
    std::vector<ImmutableRecord> records;
    for (const auto& [K_KEY, K_BYTES] : K_SAVED.m_records) {
        records.push_back({ K_KEY, K_BYTES });
        if (K_KEY.starts_with("collider-cook-")) {
            records.back().m_bytes.back() ^= std::byte { 1 };
        }
    }
    const auto K_FOREIGN = fixture.m_base.m_output.m_root / "foreign";
    const auto K_BAD = checked(publish_immutable_records(K_FOREIGN, { 1 }, 0, records, {}));
    CHECK(!read_native_collider(K_FOREIGN, K_BAD, K_REQUEST));
    CHECK(checked(read_immutable_records(K_FOREIGN)).m_commit.m_manifest_sha256 ==
          K_BAD.m_manifest_sha256);
    CHECK(!publish_native_collider(K_COLLIDER,
                                   fixture.m_base.m_receipt,
                                   fixture.m_base.m_receipt.source_directory(),
                                   { 1 },
                                   0,
                                   K_REQUEST.m_expected));
    CHECK(tree(fixture.m_base.m_receipt.source_directory()) == K_SOURCE);
}

TEST_CASE("native collider adoption binds full remapped provenance and every instance reference",
          "[native-collider][native-assets]") {
    ColliderFixture fixture;
    const auto K_INPUT = fixture.request();
    const auto K_ASSETS = inspect_native_assets(K_INPUT);
    INFO((K_ASSETS ? "admitted" : K_ASSETS.error().m_message));
    REQUIRE(K_ASSETS);
    const auto& definition = K_ASSETS->instances().definitions()[0uz];
    REQUIRE(definition.definition().m_collider);
    const auto& binding = required(definition.definition().m_collider);
    CHECK(binding.m_product == fixture.m_selected.m_product);
    CHECK(binding.m_mode == AssetColliderMode::SYNTHETIC_GAMEPLAY);
    for (const auto& instance : K_ASSETS->instances().instances().m_instances) {
        CHECK(instance.m_definition == definition.reference());
    }
    const auto K_DIRECTORY = fixture.m_base.m_output.m_root / "native";
    const auto K_COMMIT = checked(publish_native_assets(
        *K_ASSETS, fixture.m_base.m_receipt, K_DIRECTORY, { 1 }, 0, K_INPUT.m_expected));
    CHECK(read_native_assets(K_DIRECTORY, K_COMMIT, K_INPUT));
#if defined(__linux__)
    if (auto output = std::getenv("WE_NATIVE_COLLIDER_PROBE_ROOT")) {
        const std::filesystem::path K_ROOT { output };
        REQUIRE(std::filesystem::create_directory(K_ROOT));
        CHECK(publish_asset_receipt(
            fixture.m_base.m_receipt, K_ROOT / "receipt", K_INPUT.m_expected));
        const auto K_COOK = checked(inspect_native_collider(fixture.collider()));
        const auto K_STORED = checked(publish_native_collider(
            K_COOK, fixture.m_base.m_receipt, K_ROOT / "collider", { 1 }, 0, K_INPUT.m_expected));
        CHECK(read_native_collider(K_ROOT / "collider", K_STORED, fixture.collider()));
        CHECK(publish_native_assets(
            *K_ASSETS, fixture.m_base.m_receipt, K_ROOT / "native", { 1 }, 0, K_INPUT.m_expected));
    }
#endif
}

TEST_CASE("native collider adoption rejects missing mismatched and misclassified masks",
          "[native-collider][native-assets]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    ColliderFixture fixture({}, true, 20000, [&](Json& provenance) {
        if (K_CASE == 4) {
            std::swap(provenance["spans"][0uz]["class"], provenance["spans"][1uz]["class"]);
        }
    });
    auto input = fixture.request();
    auto& asset = input.m_assets[0uz];
    switch (K_CASE) {
    case 0:
        asset.m_collider.reset();
        break;
    case 1:
        asset.m_definition.m_collider.reset();
        break;
    case 2:
        asset.m_definition.m_collider = { fixture.m_selected.m_product,
                                         AssetColliderMode::UNVERIFIED };
        break;
    case 3:
        asset.m_definition.m_kind = AssetKind::PROP;
        break;
    case 4:
        break;
    default:
        asset.m_provenance.pop_back();
        break;
    }
    CHECK(!inspect_native_assets(input));
}

TEST_CASE("native collider cook policy changes invalidate immutable product and instance identity",
          "[native-collider][native-assets]") {
    ColliderFixture first;
    ColliderFixture second([](Json& root, NativeColliderSelection& selection) {
        root["cook"]["max_vertices"] = 4;
        selection.m_policy.m_max_vertices = 4uz;
    });
    const auto K_FIRST = checked(inspect_native_collider(first.collider()));
    const auto K_SECOND = checked(inspect_native_collider(second.collider()));
    CHECK(K_FIRST.base().selection().m_product == K_SECOND.base().selection().m_product);
    CHECK(K_FIRST.mesh().m_positions == K_SECOND.mesh().m_positions);
    CHECK(K_FIRST.mesh().m_triangles == K_SECOND.mesh().m_triangles);
    CHECK(K_FIRST.digest() != K_SECOND.digest());
    const auto K_FIRST_ASSETS = checked(inspect_native_assets(first.request()));
    const auto K_SECOND_ASSETS = checked(inspect_native_assets(second.request()));
    CHECK(K_FIRST_ASSETS.instances().definitions()[0uz].reference().m_sha256 !=
          K_SECOND_ASSETS.instances().definitions()[0uz].reference().m_sha256);
    CHECK(K_FIRST_ASSETS.instances().sha256() != K_SECOND_ASSETS.instances().sha256());
    const auto K_DIRECTORY = first.m_base.m_output.m_root / "policy";
    const auto K_COMMIT = checked(publish_native_collider(
        K_FIRST, first.m_base.m_receipt, K_DIRECTORY, { 1 }, 0, first.collider().m_expected));
    CHECK(!read_native_collider(K_DIRECTORY, K_COMMIT, second.collider()));
    CHECK(!publish_native_collider(
        K_SECOND, second.m_base.m_receipt, K_DIRECTORY, { 2 }, 1, second.collider().m_expected));
    CHECK(read_native_collider(K_DIRECTORY, K_COMMIT, first.collider()));
}

TEST_CASE("native collider adoption preserves explicit export refusal in every current profile",
          "[native-collider][native-assets]") {
    ColliderFixture fixture;
    auto input = fixture.request();
    input.m_instances.m_instances[1uz].m_material_variant = { 101 };
    NativeExportRequest request {
        checked(inspect_native_assets(input)), fixture.m_base.m_receipt, input.m_expected, {} };
    const auto K_LODS = GENERATE(false, true);
    if (K_LODS) {
        request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, 0uz } };
    }
    const auto K_EXPORT = prepare_native_export(request);
    REQUIRE(!K_EXPORT);
    CHECK(K_EXPORT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
    CHECK(K_EXPORT.error().m_message ==
          "Native collider export requires explicit companion selection");
}
