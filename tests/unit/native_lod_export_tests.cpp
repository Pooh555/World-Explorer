#include "../fixtures/native_lod_fixture.hpp"
#include "../fixtures/native_structure_fixture.hpp"
#include "world_explorer/application/native_pbr_scene.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_core/pbr.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/scene_store.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>
#include <span>

using namespace world_explorer;
using namespace world_explorer::native_lod_test;

namespace {

template <class T> const T& required(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required LOD export input missing");
    }
    return *value;
}

template <class T> T& required(std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required mutable LOD input missing");
    }
    return *value;
}

ExportPackageFile& file(std::vector<ExportPackageFile>& files, const std::string& path) {
    const auto K_FOUND = std::ranges::find(files, path, &ExportPackageFile::m_path);
    if (K_FOUND == files.end()) {
        throw std::logic_error("LOD export file missing");
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

NativeExportRequest export_request(const LodFixture& fixture, std::size_t level) {
    auto input = fixture.request();
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    NativeExportRequest request {
        checked(inspect_native_assets(input)), fixture.m_base.m_receipt, input.m_expected, {} };
    request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, level } };
    return request;
}

SceneSnapshot scene(const NativeExportRequest& request) {
    const auto K_FILES = checked(prepare_native_export(request));
    return checked(compile_native_pbr_scene(checked(inspect_native_export(request, K_FILES)),
                                            request.m_expected));
}

}  // namespace

TEST_CASE(
    "native LOD export maps every admitted level and shares selected asset assembly resources",
    "[native-lod-export]") {
    LodFixture fixture;
    const auto K_LEVEL = GENERATE(0uz, 1uz);
    const auto K_REQUEST = export_request(fixture, K_LEVEL);
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, files));
    const auto K_MANIFEST = json(file(files, "manifest.json").m_bytes);
    const auto K_LODS = json(file(files, "lods.json").m_bytes);
    const auto& levels = K_LODS["assets"][0uz]["levels"];
    CHECK(K_MANIFEST["schema_version"] == 3);
    CHECK(K_MANIFEST["profile"] == "WE_NATIVE_LOD_GLTF_V1");
    CHECK(K_MANIFEST["lod_selection"]["sha256"] == sha256(file(files, "lods.json").m_bytes));
    CHECK(K_LODS["structure_geometry_scope"] == "LOD0");
    CHECK(K_LODS["assets"][0uz]["selected_level"] == K_LEVEL);
    REQUIRE(levels.size() == 2uz);
    CHECK(levels[0uz]["geometry"]["product_id"] == "11001");
    CHECK(levels[1uz]["geometry"]["product_id"] == "19000");
    CHECK(levels[1uz]["base_source_faces"] == Json::array({ { 0, 1, 2 }, { 3 } }));
    CHECK(file(files, "lods/42-1.json").m_bytes ==
          checked(fixture.m_base.m_receipt.copy_blob(fixture.m_selected.m_product.m_content_sha256,
                                                     1024uz * 1024uz)));
    CHECK(levels[1uz]["certificate"]["sha256"] == fixture.m_selected.m_product.m_content_sha256);
    CHECK(levels[1uz]["provenance"]["product_id"] == "19002");
    const auto K_ASSET = json(file(files, "assets/42/asset.gltf").m_bytes);
    const auto K_ASSEMBLY = json(file(files, "environment.gltf").m_bytes);
    CHECK(file(files, "assets/42/asset.gltf").m_bytes ==
          file(files, "assets/42/lod-" + std::to_string(K_LEVEL) + ".gltf").m_bytes);
    CHECK(K_ASSET["accessors"][0uz]["count"] == (K_LEVEL == 0uz ? 5 : 4));
    CHECK(K_ASSEMBLY["meshes"].size() == 1uz);
    CHECK(K_ASSEMBLY["materials"].size() == 2uz);
    CHECK(K_ASSEMBLY["nodes"].size() == 2uz);
    CHECK(K_ASSEMBLY["nodes"][0uz]["mesh"] == K_ASSEMBLY["nodes"][1uz]["mesh"]);
    CHECK(K_ASSEMBLY["buffers"][0uz]["uri"] ==
          "assets/42/" + K_ASSET["buffers"][0uz]["uri"].get<std::string>());
    CHECK(std::ranges::count_if(files, [](const auto& f) {
              return f.m_path.ends_with(".png");
          }) == 8);
    CHECK(K_PACKAGE.files().size() == files.size());
}

TEST_CASE("native LOD export rejects unselected malformed and stale inputs",
          "[native-lod-export]") {
    LodFixture fixture;
    auto request = export_request(fixture, 1uz);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    switch (K_CASE) {
    case 0:
        request.m_lods.reset();
        break;
    case 1:
        request.m_lods = std::vector<NativeExportLodSelection> {};
        break;
    case 2:
        request.m_lods = std::vector<NativeExportLodSelection> { { { 99 }, 0uz } };
        break;
    case 3:
        request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, 2uz } };
        break;
    case 4:
        request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, 0uz }, { { 42 }, 1uz } };
        break;
    default:
        ++request.m_expected.m_pose_revision.m_value;
        break;
    }
    CHECK(!prepare_native_export(request));
}

TEST_CASE("native LOD selection is per definition and independent of request ordering",
          "[native-lod-export]") {
    LodFixture fixture;
    auto input = fixture.request();
    auto second = input.m_assets[0uz];
    second.m_definition.m_asset = { 43 };
    second.m_source_instance = { 3 };
    const auto K_DEFINITION = checked(admit_asset_definition(
        second.m_definition, fixture.m_base.m_receipt, second.m_provenance, input.m_expected));
    auto original = input.m_instances.m_instances[0uz];
    original.m_instance = { 3 };
    original.m_definition = K_DEFINITION.reference();
    auto copy = original;
    copy.m_instance = { 4 };
    copy.m_world_from_asset.m_translation = { 50, 5, 9 };
    input.m_assets.push_back(second);
    input.m_instances.m_instances.insert(input.m_instances.m_instances.end(), { original, copy });
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    NativeExportRequest request {
        checked(inspect_native_assets(input)), fixture.m_base.m_receipt, input.m_expected, {} };
    request.m_lods = std::vector<NativeExportLodSelection> { { { 43 }, 1uz }, { { 42 }, 0uz } };
    auto first = checked(prepare_native_export(request));
    request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, 0uz }, { { 43 }, 1uz } };
    auto reordered = checked(prepare_native_export(request));
    REQUIRE(first.size() == reordered.size());
    for (auto& item : first) {
        CHECK(file(reordered, item.m_path).m_bytes == item.m_bytes);
    }
    const auto K_SCENE = checked(compile_native_pbr_scene(
        checked(inspect_native_export(request, first)), request.m_expected));
    CHECK(K_SCENE->m_chunks.size() == 2uz);
    CHECK(required(K_SCENE->m_chunks.at({ 42 })->m_mesh).m_triangles.size() == 4uz);
    CHECK(required(K_SCENE->m_chunks.at({ 43 })->m_mesh).m_triangles.size() == 2uz);
    const auto K_UPLOAD = checked(prepare_pbr_upload(*K_SCENE, {}, 0uz));
    CHECK(K_UPLOAD.m_vertices == 9uz);
    CHECK(K_UPLOAD.m_triangles == 6uz);
}

TEST_CASE(
    "native LOD export rejects rehashed companion certificate and unused resource substitution",
    "[native-lod-export]") {
    LodFixture fixture;
    const auto K_REQUEST = export_request(fixture, 1uz);
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_PATH = GENERATE("lods.json",
                                 "lods/42-1.json",
                                 "assets/42/geometry-lod0.bin",
                                 "assets/42/lod-0.gltf",
                                 "provenance/19002-1.json");
    file(files, K_PATH).m_bytes.back() ^= std::byte { 1 };
    auto manifest = json(file(files, "manifest.json").m_bytes);
    for (auto& row : manifest["files"]) {
        if (row["path"].get<std::string>() == K_PATH) {
            row["sha256"] = sha256(file(files, K_PATH).m_bytes);
        }
    }
    file(files, "manifest.json").m_bytes = bytes(manifest);
    CHECK(!inspect_native_export(K_REQUEST, files));
}

TEST_CASE("native LOD preview selects actual geometry and shares materials across copies",
          "[native-lod-export]") {
    LodFixture fixture;
    const auto K_LEVEL = GENERATE(0uz, 1uz);
    const auto K_REQUEST = export_request(fixture, K_LEVEL);
    auto files = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, files));
    const auto K_SCENE = checked(compile_native_pbr_scene(K_PACKAGE, K_REQUEST.m_expected));
    CHECK(K_SCENE->m_provenance.at("selected_lods") == "Asset 42: LOD " + std::to_string(K_LEVEL));
    const auto& mesh = required(K_SCENE->m_chunks.begin()->second->m_mesh);
    const auto& pbr = required(mesh.m_pbr);
    CHECK(mesh.m_vertices.size() == (K_LEVEL == 0uz ? 5uz : 4uz));
    CHECK(mesh.m_triangles.size() == (K_LEVEL == 0uz ? 4uz : 2uz));
    CHECK(pbr.m_instances.size() == 2uz);
    CHECK(pbr.m_definition ==
          required(K_REQUEST.m_assets).instances().definitions()[0uz].reference());
    const auto K_UPLOAD = checked(prepare_pbr_upload(*K_SCENE, {}, 0uz));
    CHECK(K_UPLOAD.m_vertices == mesh.m_vertices.size());
    CHECK(K_UPLOAD.m_triangles == mesh.m_triangles.size());
    REQUIRE(write_bundle(fixture.m_base.m_output.m_root / "lod.wexplore", K_SCENE));
    CHECK(scene_content_hash(*checked(load_bundle(
              fixture.m_base.m_output.m_root / "lod.wexplore"))) == scene_content_hash(*K_SCENE));
#if defined(__linux__)
    if (auto output = std::getenv("WE_LOD_EXPORT_PROBE_ROOT")) {
        const std::filesystem::path K_ROOT { output };
        REQUIRE(write_native_export(K_PACKAGE, K_ROOT / ("level-" + std::to_string(K_LEVEL))));
        REQUIRE(write_bundle(K_ROOT / ("level-" + std::to_string(K_LEVEL) + ".wexplore"), K_SCENE));
    }
#endif
}

TEST_CASE("native LOD selection replaces geometry through ordinary scene transactions",
          "[native-lod-export]") {
    LodFixture fixture;
    const auto K_FIRST = scene(export_request(fixture, 0uz));
    const auto K_NEXT = scene(export_request(fixture, 1uz));
    CHECK(scene_content_hash(*K_FIRST) != scene_content_hash(*K_NEXT));
    auto replacement = std::make_shared<SceneChunk>(*K_NEXT->m_chunks.begin()->second);
    replacement->m_generation = 2;
    SceneStore store(K_FIRST->m_id);
    REQUIRE(store.reset(K_FIRST));
    const SceneTransaction K_CHANGE {
        K_FIRST->m_id,
        K_FIRST->m_epoch,
        K_FIRST->m_revision,
        { 2 },
        { 91 },
        { { SceneOperationKind::REPLACE, replacement->m_id, replacement, {}, {} } } };
    REQUIRE(store.commit(K_CHANGE));
    CHECK(store.commit(K_CHANGE));
    CHECK(required(store.acquire_snapshot()->m_chunks.begin()->second->m_mesh).m_triangles.size() ==
          2uz);
}

TEST_CASE("native LOD publication recovers and reloads only the independently selected package",
          "[native-lod-export]") {
    LodFixture fixture;
    const auto K_REQUEST = export_request(fixture, 1uz);
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, K_FILES));
    const auto K_DIRECTORY = fixture.m_base.m_output.m_root / "lod-export";
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
    CHECK(!read_native_export(K_DIRECTORY, K_COMMIT, export_request(fixture, 0uz), K_FILES));
    CHECK(checked(read_immutable_records(K_DIRECTORY)).m_commit.m_manifest_sha256 ==
          K_COMMIT.m_manifest_sha256);
}

TEST_CASE("native LOD export bounds conversion of unused base geometry independently",
          "[native-lod-export]") {
    LodFixture fixture(
        [](Json& doc, Json&) {
            doc["faces"][0uz]["weights"] = { 65, 63, 128 };
        },
        19001,
        [](Json& doc, GeometryProductSelection&) {
            doc["mesh"]["positions"][4uz][0uz] = .4921875;
            doc["mesh"]["uv"][4uz][0uz] = .24609375;
            for (std::size_t face { 0uz }; face < 3uz; ++face) {
                doc["support"][face]["anchors"][2uz]["weights"] = { .25390625, .24609375, .5 };
            }
        });
    auto input = fixture.request();
    auto& definition = input.m_assets[0uz].m_definition;
    definition.m_pivot.m_asset_from_geometry.m_translation.m_x = 499900;
    definition.m_bounds.m_minimum.m_x = 499900;
    definition.m_bounds.m_maximum.m_x = 499902;
    input.m_instances.m_instances[0uz].m_world_from_asset.m_translation.m_x = -999790;
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    rebind(input);
    NativeExportRequest request {
        checked(inspect_native_assets(input)), fixture.m_base.m_receipt, input.m_expected, {} };
    request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, 1uz } };
    request.m_policy.m_position_units = 0;
    CHECK(!prepare_native_export(request));
    request.m_policy.m_position_units = .1;
    auto files = checked(prepare_native_export(request));
    const auto K_LODS = json(file(files, "lods.json").m_bytes);
    CHECK(K_LODS["assets"][0uz]["levels"][0uz]["errors"]["position_units"] == .015625);
    CHECK(K_LODS["assets"][0uz]["levels"][1uz]["errors"]["position_units"] == 0);
}

TEST_CASE("native LOD runtime rejects rehashed selection and companion pin substitution",
          "[native-lod-export]") {
    LodFixture fixture;
    const auto K_SCENE = scene(export_request(fixture, 1uz));
    auto chunk = std::make_shared<SceneChunk>(*K_SCENE->m_chunks.begin()->second);
    auto& pbr = required(required(chunk->m_mesh).m_pbr);
    auto manifest = Json::parse(pbr.m_export_manifest);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    switch (K_CASE) {
    case 0:
        manifest["lod_selection"]["scope"] = "AUTOMATIC";
        break;
    case 1:
        manifest["lod_selection"]["structure_geometry_scope"] = "SELECTED_LOD";
        break;
    case 2:
        manifest["assets"][0uz]["level"] = 9;
        break;
    case 3:
        manifest["assets"][0uz]["certificate"]["path"] = "../certificate.json";
        break;
    case 4:
        manifest["assets"][0uz]["certificate"]["sha256"] = std::string(64uz, '0');
        break;
    case 5: {
        auto& inventory = manifest["files"];
        const auto K_FOUND = std::ranges::find_if(inventory, [](const auto& row) {
            return row.at("path").template get<std::string>() == "lods.json";
        });
        REQUIRE(K_FOUND != inventory.end());
        inventory.erase(K_FOUND);
        break;
    }
    case 6:
        manifest["schema_version"] = 2;
        break;
    default:
        auto doc = Json::parse(pbr.m_asset_document);
        doc["extras"]["lod_level"] = 0;
        pbr.m_asset_document = doc.dump();
        for (auto& row : manifest["files"]) {
            if (row["path"].get<std::string>() == "assets/42/asset.gltf") {
                row["sha256"] = sha256(std::as_bytes(std::span(pbr.m_asset_document)));
            }
        }
        break;
    }
    pbr.m_export_manifest = manifest.dump();
    pbr.m_export_manifest_sha256 = sha256(std::as_bytes(std::span(pbr.m_export_manifest)));
    CHECK(!validate_pbr_appearance(*chunk, {}));
}

TEST_CASE("native LOD profile keeps structural graphs bound to base geometry",
          "[native-lod-export]") {
    native_structure_test::NativeStructureFixture fixture;
    const auto K_INPUT = fixture.request();
    const auto K_STRUCTURE = checked(inspect_native_structure(K_INPUT));
    NativeExportRequest request {
        K_INPUT.m_assets, fixture.m_receipt, K_INPUT.m_expected, {}, K_STRUCTURE };
    request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, 0uz } };
    auto files = checked(prepare_native_export(request));
    const auto K_MANIFEST = json(file(files, "manifest.json").m_bytes);
    CHECK(K_MANIFEST["schema_version"] == 3);
    CHECK(K_MANIFEST["structure"]["sha256"] == K_STRUCTURE.sha256());
    CHECK(K_MANIFEST["lod_selection"]["structure_geometry_scope"] == "LOD0");
    CHECK(compile_native_pbr_scene(checked(inspect_native_export(request, files)),
                                   request.m_expected));
}
