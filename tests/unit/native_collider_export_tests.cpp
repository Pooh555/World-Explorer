#include "../fixtures/native_collider_fixture.hpp"
#include "../fixtures/native_lod_fixture.hpp"
#include "../fixtures/native_structure_fixture.hpp"
#include "world_explorer/application/native_pbr_scene.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>
#include <limits>
#include <span>

using namespace world_explorer;
using namespace world_explorer::native_collider_test;

namespace {

template <class T> const T& required(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required collider export value missing");
    }
    return *value;
}

template <class T> T& required(std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required mutable collider export value missing");
    }
    return *value;
}

ExportPackageFile& file(std::vector<ExportPackageFile>& files, const std::string& path) {
    const auto K_FOUND = std::ranges::find(files, path, &ExportPackageFile::m_path);
    if (K_FOUND == files.end()) {
        throw std::logic_error("Collider export file missing");
    }
    return *K_FOUND;
}

Json json(const std::vector<std::byte>& bytes) {
    return Json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::vector<std::byte> bytes(const Json& doc) {
    const auto K_TEXT = doc.dump();
    const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
    return { K_BYTES.begin(), K_BYTES.end() };
}

NativeExportRequest export_request(NativeAssetsRequest input) {
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    NativeExportRequest request {
        checked(inspect_native_assets(input)), input.m_receipt, input.m_expected, {} };
    request.m_colliders = NativeExportColliderPolicy {};
    return request;
}

NativeExportRequest lod_request(native_lod_test::LodFixture& fixture, std::size_t level) {
    const auto K_ATTACHED = attach_collider(fixture.m_base);
    auto input = fixture.request();
    input.m_assets[0uz].m_collider = checked(inspect_native_collider(collider_request(
        fixture.m_base.m_base.m_base, fixture.m_base.m_receipt, K_ATTACHED.first)));
    auto request = export_request(input);
    request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, level } };
    return request;
}

NativeAssetsRequest rotated_input(const ColliderFixture& fixture, bool approximate) {
    auto input = fixture.request();
    auto& asset = input.m_assets[0uz];
    auto& pivot = asset.m_definition.m_pivot.m_asset_from_geometry;
    const auto K_LENGTH = std::sqrt(30.0);
    pivot.m_rotation = approximate ? Quatd { 1 / K_LENGTH, 2 / K_LENGTH, 3 / K_LENGTH, 4 / K_LENGTH }
                                   : Quatd { .5, .5, .5, .5 };
    const auto& q = pivot.m_rotation;
    const Quatd K_INVERSE { q.m_w, -q.m_x, -q.m_y, -q.m_z };
    const auto& base = required(asset.m_geometry);
    bool first = true;
    for (auto vertex : base.mesh().m_positions) {
        const auto K_POINT = transform_point(pivot, vertex);
        auto& bounds = asset.m_definition.m_bounds;
        if (first) {
            bounds = { K_POINT, K_POINT };
            first = false;
        } else {
            bounds.m_minimum = { std::min(bounds.m_minimum.m_x, K_POINT.m_x),
                                std::min(bounds.m_minimum.m_y, K_POINT.m_y),
                                std::min(bounds.m_minimum.m_z, K_POINT.m_z) };
            bounds.m_maximum = { std::max(bounds.m_maximum.m_x, K_POINT.m_x),
                                std::max(bounds.m_maximum.m_y, K_POINT.m_y),
                                std::max(bounds.m_maximum.m_z, K_POINT.m_z) };
        }
    }
    auto& source = input.m_instances.m_instances[0uz].m_world_from_asset;
    source.m_rotation = K_INVERSE;
    source.m_translation = base.world_from_geometry().m_translation -
                           rotate(K_INVERSE, pivot.m_translation) * source.m_scale;
    auto& copy = input.m_instances.m_instances[1uz].m_world_from_asset;
    copy.m_rotation = { .5, -.5, .5, .5 };
    copy.m_scale = 1.25;
    copy.m_translation = { 30.125, 5.25, 9.5 };
    rebind(input);
    return input;
}

SimilarityTransform placement(const Json& value) {
    const auto& q = value.at("rotation");
    const auto& t = value.at("translation");
    return {
        { q[0uz].get<double>(), q[1uz].get<double>(), q[2uz].get<double>(), q[3uz].get<double>() },
        { t[0uz].get<double>(), t[1uz].get<double>(), t[2uz].get<double>() },
        value.at("scale").get<double>() };
}

}  // namespace

TEST_CASE("native collider export retains exact inputs provenance and every shared placement",
          "[collider-export]") {
    ColliderFixture fixture;
    const auto K_REQUEST = export_request(fixture.request());
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, files));
    const auto K_MANIFEST = json(file(files, "manifest.json").m_bytes);
    const auto K_COMPANION = json(file(files, "colliders.json").m_bytes);
    CHECK(K_MANIFEST["schema_version"] == 4);
    CHECK(K_MANIFEST["profile"] == "WE_NATIVE_COLLIDER_GLTF_V1");
    CHECK(K_MANIFEST["walking_enabled"] == false);
    CHECK(K_MANIFEST["collider_companion"]["sha256"] ==
          sha256(file(files, "colliders.json").m_bytes));
    const auto& binding = K_COMPANION["assets"][0uz];
    const auto K_COLLIDER = checked(inspect_native_collider(fixture.collider()));
    CHECK(file(files, "colliders/42/root.json").m_bytes ==
          checked(fixture.m_base.m_receipt.copy_blob(fixture.m_selected.m_product.m_content_sha256,
                                                     1024uz * 1024uz)));
    CHECK(file(files, "colliders/42/base.json").m_bytes ==
          checked(fixture.m_base.m_receipt.copy_blob(
              K_COLLIDER.base().selection().m_product.m_content_sha256, 1024uz * 1024uz)));
    CHECK(std::ranges::equal(file(files, "colliders/42/triangles.bin").m_bytes,
                             K_COLLIDER.canonical_bytes()));
    CHECK(binding["cook"]["sha256"] == K_COLLIDER.digest());
    CHECK(binding["provenance"]["path"] == "provenance/20001-1.json");
    CHECK(binding["source_faces"] == Json::array({ 1, 0 }));
    CHECK(binding["instances"].size() == 2uz);
    CHECK(binding["instances"][0uz]["world_from_collider"]["translation"] ==
          Json::array({ 10, -4, 3 }));
    CHECK(binding["instances"][1uz]["world_from_collider"]["translation"] ==
          Json::array({ 28, 1, 3 }));
    CHECK(binding["maximum_world_position_error_units"] == 0);
    CHECK(K_COMPANION["geometry_scope"] == "LOD0");
    CHECK(K_COMPANION["backend_shape"] == "NOT_COOKED");
    auto summary = binding;
    summary.erase("instances");
    summary.erase("source_faces");
    CHECK(K_MANIFEST["assets"][0uz]["collider"] == summary);
    CHECK(K_PACKAGE.files().size() == files.size());
}

TEST_CASE("native collider companion stays on base geometry under explicit render LOD selection",
          "[collider-export]") {
    native_lod_test::LodFixture fixture;
    const auto K_LEVEL = GENERATE(0uz, 1uz);
    const auto K_REQUEST = lod_request(fixture, K_LEVEL);
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, files));
    const auto K_BINDING = json(file(files, "colliders.json").m_bytes)["assets"][0uz];
    const auto K_MANIFEST = json(file(files, "manifest.json").m_bytes);
    CHECK(K_BINDING["triangle_count"] == 4);
    CHECK(K_BINDING["base_geometry"] == K_MANIFEST["assets"][0uz]["base_geometry"]);
    const auto K_SCENE = checked(compile_native_pbr_scene(K_PACKAGE, K_REQUEST.m_expected));
    CHECK(required(K_SCENE->m_chunks.begin()->second->m_mesh).m_triangles.size() ==
          (K_LEVEL == 0uz ? 4uz : 2uz));
    CHECK(K_SCENE->m_provenance.at("selected_lods") == "Asset 42: LOD " + std::to_string(K_LEVEL));
    CHECK(K_SCENE->m_provenance.at("collider_status") == "UNCOOKED_GAMEPLAY_COMPANION");
    CHECK(walking_block_reason(*K_SCENE) == WalkingBlockReason::MISSING_MEASUREMENT);
    const auto K_DIRECTORY = fixture.m_base.m_output.m_root / "collider-lod.wexplore";
    REQUIRE(write_bundle(K_DIRECTORY, K_SCENE));
    CHECK(scene_content_hash(*checked(load_bundle(K_DIRECTORY))) == scene_content_hash(*K_SCENE));
#if defined(__linux__)
    if (auto output = std::getenv("WE_COLLIDER_EXPORT_PROBE_ROOT")) {
        const std::filesystem::path K_ROOT { output };
        REQUIRE(write_native_export(K_PACKAGE, K_ROOT / ("level-" + std::to_string(K_LEVEL))));
        REQUIRE(write_bundle(K_ROOT / ("level-" + std::to_string(K_LEVEL) + ".wexplore"), K_SCENE));
    }
#endif
}

TEST_CASE("native collider companion composes with original structure graph source pins",
          "[collider-export]") {
    native_structure_test::NativeStructureFixture fixture;
    const auto K_ATTACHED = attach_collider(fixture);
    auto input = fixture.assets();
    input.m_assets[0uz].m_collider = checked(inspect_native_collider(
        collider_request(fixture.m_base.m_base, fixture.m_receipt, K_ATTACHED.first)));
    const auto K_GRAPH = checked(inspect_structure_graph(
        { fixture.m_base.m_selected,
         input.m_assets[0uz].m_geometry,
         checked(fixture.m_base.m_base.m_views.inspect(fixture.m_receipt, false)),
         fixture.m_receipt,
         input.m_expected }));
    const NativeStructureSelection K_SELECTION { K_GRAPH,
                                                input.m_instances.m_instances[0uz].m_definition,
                                                { { 11 } },
                                                { { 31 } },
                                                { { 21 } },
                                                {},
                                                { { 1 }, { 2 } } };
    auto request = export_request(input);
    request.m_structure = checked(inspect_native_structure(
        { request.m_assets, fixture.m_receipt, { K_SELECTION }, input.m_expected }));
    auto files = checked(prepare_native_export(request));
    const auto K_MANIFEST = json(file(files, "manifest.json").m_bytes);
    CHECK(K_MANIFEST["structure"]["sha256"] == required(request.m_structure).sha256());
    CHECK(json(file(files, "colliders.json").m_bytes)["assets"][0uz]["triangle_count"] == 8);
    CHECK(compile_native_pbr_scene(checked(inspect_native_export(request, files)),
                                   request.m_expected));
}

TEST_CASE(
    "native collider companion validates rotated pivots and independently bounded world frames",
    "[collider-export]") {
    ColliderFixture fixture;
    const auto K_APPROXIMATE = GENERATE(false, true);
    const auto K_INPUT = rotated_input(fixture, K_APPROXIMATE);
    auto request = export_request(K_INPUT);
    auto files = checked(prepare_native_export(request));
    const auto K_BINDING = json(file(files, "colliders.json").m_bytes)["assets"][0uz];
    const auto K_ERROR = K_BINDING["maximum_world_position_error_units"].get<double>();
    for (std::size_t instance { 0uz }; instance < 2uz; ++instance) {
        const auto K_WORLD = placement(K_BINDING["instances"][instance]["world_from_collider"]);
        const auto& original = K_INPUT.m_instances.m_instances[instance].m_world_from_asset;
        for (auto vertex : required(K_INPUT.m_assets[0uz].m_collider).mesh().m_positions) {
            const auto K_ACTUAL = transform_point(K_WORLD, vertex);
            const auto K_EXPECTED = transform_point(
                original,
                transform_point(K_INPUT.m_assets[0uz].m_definition.m_pivot.m_asset_from_geometry,
                                vertex));
            const auto K_DELTA = K_ACTUAL - K_EXPECTED;
            CHECK(std::hypot(K_DELTA.m_x, K_DELTA.m_y, K_DELTA.m_z) <=
                  required(request.m_colliders).m_world_position_units);
        }
    }
    if (K_APPROXIMATE) {
        REQUIRE(K_ERROR > 0);
        request.m_colliders = NativeExportColliderPolicy { K_ERROR / 2 };
        CHECK(!prepare_native_export(request));
    } else {
        CHECK(K_ERROR == 0);
    }
#if defined(__linux__)
    if (K_APPROXIMATE) {
        if (auto output = std::getenv("WE_COLLIDER_ROTATION_PROBE_OUTPUT")) {
            request.m_colliders = NativeExportColliderPolicy {};
            REQUIRE(write_native_export(checked(inspect_native_export(request, files)), output));
        }
    }
#endif
}

TEST_CASE("native collider companion preserves noncollider definitions in a mixed assembly",
          "[collider-export]") {
    ColliderFixture fixture;
    auto input = fixture.request();
    auto other = input.m_assets[0uz];
    other.m_collider.reset();
    other.m_definition.m_collider.reset();
    other.m_definition.m_provenance.pop_back();
    other.m_provenance.pop_back();
    other.m_definition.m_kind = AssetKind::PROP;
    other.m_definition.m_pivot.m_policy = AssetPivotPolicy::ATTACHMENT_ORIGIN;
    other.m_definition.m_asset = { 43 };
    other.m_source_instance = { 3 };
    const auto K_OTHER = checked(admit_asset_definition(
        other.m_definition, fixture.m_base.m_receipt, other.m_provenance, input.m_expected));
    auto instance = input.m_instances.m_instances[0uz];
    instance.m_instance = { 3 };
    instance.m_definition = K_OTHER.reference();
    input.m_assets.push_back(other);
    input.m_instances.m_instances.push_back(instance);
    const auto K_REQUEST = export_request(input);
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_MANIFEST = json(file(files, "manifest.json").m_bytes);
    CHECK(K_MANIFEST["assets"][1uz]["collider"].is_null());
    CHECK(json(file(files, "colliders.json").m_bytes)["assets"].size() == 1uz);
    const auto K_SCENE = checked(compile_native_pbr_scene(
        checked(inspect_native_export(K_REQUEST, files)), K_REQUEST.m_expected));
    CHECK(K_SCENE->m_chunks.size() == 2uz);
}

TEST_CASE("native collider export refuses absent selection empty companions and invalid limits",
          "[collider-export]") {
    ColliderFixture fixture;
    auto request = export_request(fixture.request());
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    switch (K_CASE) {
    case 0:
        request.m_colliders.reset();
        break;
    case 1:
        request.m_colliders = NativeExportColliderPolicy { -1 };
        break;
    case 2:
        request.m_colliders = NativeExportColliderPolicy { std::numeric_limits<double>::quiet_NaN() };
        break;
    case 3:
        request.m_colliders = NativeExportColliderPolicy { 2 };
        break;
    default:
        NativeFixture plain;
        request = export_request(plain.request());
        break;
    }
    CHECK(!prepare_native_export(request));
}

TEST_CASE(
    "native collider packages refuse rehashed root cook provenance and placement substitutions",
    "[collider-export]") {
    ColliderFixture fixture;
    const auto K_REQUEST = export_request(fixture.request());
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_PATH = GENERATE("colliders/42/root.json",
                                 "colliders/42/base.json",
                                 "colliders/42/triangles.bin",
                                 "provenance/20001-1.json",
                                 "colliders.json");
    if (std::string_view(K_PATH) == "colliders.json") {
        auto companion = json(file(files, K_PATH).m_bytes);
        companion["assets"][0uz]["instances"][0uz]["world_from_collider"]["translation"][0uz] = 999;
        file(files, K_PATH).m_bytes = bytes(companion);
    } else {
        file(files, K_PATH).m_bytes.back() ^= std::byte { 1 };
    }
    auto manifest = json(file(files, "manifest.json").m_bytes);
    for (auto& row : manifest["files"]) {
        if (row["path"].get<std::string>() == K_PATH) {
            row["sha256"] = sha256(file(files, K_PATH).m_bytes);
        }
    }
    if (std::string_view(K_PATH) == "colliders.json") {
        manifest["collider_companion"]["sha256"] = sha256(file(files, K_PATH).m_bytes);
    }
    file(files, "manifest.json").m_bytes = bytes(manifest);
    CHECK(!inspect_native_export(K_REQUEST, files));
}

TEST_CASE("native collider export publication recovers and selected reload preserves source data",
          "[collider-export]") {
    ColliderFixture fixture;
    const auto K_REQUEST = export_request(fixture.request());
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, K_FILES));
    const auto K_DIRECTORY = fixture.m_base.m_output.m_root / "collider-export";
    std::stop_source cancellation;
    CHECK(!publish_native_export(K_PACKAGE,
                                 fixture.m_base.m_receipt,
                                 K_DIRECTORY,
                                 { 1 },
                                 0,
                                 K_REQUEST.m_expected,
                                 cancellation.get_token(),
                                 [&](std::size_t) {
                                     cancellation.request_stop();
                                 }));
    const auto K_COMMIT = checked(publish_native_export(
        K_PACKAGE, fixture.m_base.m_receipt, K_DIRECTORY, { 1 }, 0, K_REQUEST.m_expected));
    CHECK(
        checked(publish_native_export(
                    K_PACKAGE, fixture.m_base.m_receipt, K_DIRECTORY, { 1 }, 0, K_REQUEST.m_expected))
            .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_native_export(K_DIRECTORY, K_COMMIT, K_REQUEST, K_FILES));
    auto changed = K_REQUEST;
    changed.m_colliders = NativeExportColliderPolicy { 0 };
    CHECK(!read_native_export(K_DIRECTORY, K_COMMIT, changed, K_FILES));
    CHECK(checked(read_immutable_records(K_DIRECTORY)).m_commit.m_manifest_sha256 ==
          K_COMMIT.m_manifest_sha256);
}

TEST_CASE("native collider PBR source refuses rehashed authority and file pin changes",
          "[collider-export]") {
    ColliderFixture fixture;
    const auto K_REQUEST = export_request(fixture.request());
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_SCENE = checked(compile_native_pbr_scene(
        checked(inspect_native_export(K_REQUEST, K_FILES)), K_REQUEST.m_expected));
    auto chunk = std::make_shared<SceneChunk>(*K_SCENE->m_chunks.begin()->second);
    auto& pbr = required(required(chunk->m_mesh).m_pbr);
    auto manifest = Json::parse(pbr.m_export_manifest);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    switch (K_CASE) {
    case 0:
        manifest["walking_enabled"] = true;
        break;
    case 1:
        manifest["collider_companion"]["backend_shape"] = "READY";
        break;
    case 2:
        manifest["collider_companion"]["geometry_scope"] = "SELECTED_LOD";
        break;
    case 3:
        manifest["assets"][0uz]["collider"]["cook"]["sha256"] = std::string(64uz, '0');
        break;
    case 4:
        manifest["assets"][0uz]["collider"]["provenance"]["path"] = "../mask";
        break;
    case 5:
        manifest["assets"][0uz]["collider"]["product"]["product_id"] = "-1";
        break;
    case 6:
        manifest["collider_limits"]["world_position_units"] = -1;
        break;
    default:
        manifest["schema_version"] = 3;
        break;
    }
    pbr.m_export_manifest = manifest.dump();
    pbr.m_export_manifest_sha256 = sha256(std::as_bytes(std::span(pbr.m_export_manifest)));
    CHECK(!validate_pbr_appearance(*chunk, {}));
}
