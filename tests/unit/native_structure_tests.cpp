#include "../fixtures/native_structure_fixture.hpp"
#include "world_explorer/application/native_pbr_scene.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>

using namespace world_explorer;
using namespace world_explorer::native_structure_test;
using world_explorer::asset_component_test::tree;

namespace {

template <class T> const T& required(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required structured fixture value missing");
    }
    return *value;
}

const ExportPackageFile& file(const std::vector<ExportPackageFile>& files,
                              const std::string& path) {
    const auto K_FOUND = std::ranges::find(files, path, &ExportPackageFile::m_path);
    if (K_FOUND == files.end()) {
        throw std::logic_error("Structured file missing");
    }
    return *K_FOUND;
}

Json json(const std::vector<std::byte>& bytes) {
    return Json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

NativeExportRequest export_request(const NativeStructureFixture& fixture) {
    const auto K_INPUT = fixture.request();
    const auto K_STRUCTURE = checked(inspect_native_structure(K_INPUT));
    return { K_INPUT.m_assets, fixture.m_receipt, K_INPUT.m_expected, {}, K_STRUCTURE };
}

}  // namespace

TEST_CASE("native structure binds real patch opening room and per-copy graph frames",
          "[native-structure]") {
    NativeStructureFixture fixture;
    auto input = fixture.request();
    const auto K_BEFORE = tree(fixture.m_receipt.source_directory());
    const auto K_RESULT = checked(inspect_native_structure(input));
    const auto K_DOC = Json::parse(K_RESULT.canonical_json());
    CHECK(K_DOC["structures"][0uz]["surface_ids"] == Json::array({ 11 }));
    CHECK(K_DOC["structures"][0uz]["opening_ids"] == Json::array({ 31 }));
    CHECK(K_DOC["structures"][0uz]["room_ids"] == Json::array({ 21 }));
    CHECK(K_DOC["identity_scope"] == "SCENE_INSTANCE_GRAPH_ELEMENT");
    CHECK(K_DOC["structures"][0uz]["instances"].size() == 2uz);
    CHECK(K_DOC["structures"][0uz]["instances"][0uz]["world_from_graph"]["translation"] ==
          Json::array({ 10.0, -4.0, 3.0 }));
    CHECK(K_DOC["structures"][0uz]["instances"][1uz]["world_from_graph"]["translation"] ==
          Json::array({ 28.0, 1.0, 3.0 }));
    const auto& graph = required(K_RESULT.structures()[0uz].m_graph);
    CHECK(graph.surfaces()[0uz].m_faces.size() == 8uz);
    CHECK(graph.surfaces()[0uz].m_coverage == std::array<std::uint64_t, 5> { 0, 8, 0, 0, 0 });
    CHECK(graph.openings()[0uz].m_class == AssetEvidenceClass::INFERRED);
    CHECK(!K_RESULT.manual_ancestor());
    CHECK(K_RESULT.assets().instances().sha256() == required(input.m_assets).instances().sha256());
    input.m_structures.clear();
    input.m_assets.reset();
    CHECK(K_RESULT.structures().size() == 1uz);
    CHECK(graph.openings()[0uz].m_id == StructureOpeningId { 31 });
    CHECK(tree(fixture.m_receipt.source_directory()) == K_BEFORE);
}

TEST_CASE("native structure requires complete identities actual geometry and matching context",
          "[native-structure]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    NativeStructureFixture fixture;
    auto input = fixture.request();
    auto& binding = input.m_structures[0uz];
    switch (K_CASE) {
    case 0:
        input.m_assets.reset();
        break;
    case 1:
        input.m_receipt.reset();
        break;
    case 2:
        binding.m_graph.reset();
        break;
    case 3:
        binding.m_surfaces.clear();
        break;
    case 4:
        binding.m_openings[0uz] = { 32 };
        break;
    case 5:
        binding.m_rooms.push_back({ 22 });
        break;
    case 6:
        binding.m_instances.pop_back();
        break;
    case 7:
        binding.m_instances[0uz] = { 100 };
        break;
    case 8:
        binding.m_definition.m_sha256.assign(64uz, 'a');
        break;
    case 9:
        input.m_structures.push_back(binding);
        break;
    case 10:
        ++input.m_expected.m_pose_revision.m_value;
        break;
    case 11:
        ++input.m_expected.m_world_epoch.m_value;
        break;
    case 12:
        binding.m_relations.push_back({ StructureRelationKind::SUPPORT, { 11 }, { 12 } });
        break;
    case 13:
        binding.m_graph = checked(fixture.m_base.inspect());
        break;  // older receipt
    case 14:
        input.m_structures.resize(17uz);
        break;
    case 15:
        binding.m_surfaces.push_back(binding.m_surfaces[0uz]);
        break;
    default:
        throw std::logic_error("Unexpected structure mutation");
    }
    CHECK(!inspect_native_structure(input));
    CHECK(!std::filesystem::exists(fixture.directory()));
}

TEST_CASE("native structure retains support identities manual ancestry and deterministic ordering",
          "[native-structure]") {
    NativeStructureFixture fixture({ .m_relations = true, .m_manual = true });
    auto input = fixture.request();
    const auto K_RESULT = checked(inspect_native_structure(input));
    CHECK(K_RESULT.manual_ancestor());
    CHECK(!K_RESULT.assets().instances().instances().m_manual_ancestor);
    CHECK(K_RESULT.structures()[0uz].m_relations ==
          std::vector<NativeStructureRelation> { { StructureRelationKind::SUPPORT, { 11 }, { 12 } } });
    const auto& graph = required(K_RESULT.structures()[0uz].m_graph);
    CHECK(graph.relations()[0uz].m_actual_residual_units == 0);
    CHECK(graph.rooms()[0uz].m_boundaries.size() == 2uz);
    std::ranges::reverse(input.m_structures[0uz].m_surfaces);
    std::ranges::reverse(input.m_structures[0uz].m_instances);
    CHECK(checked(inspect_native_structure(input)).sha256() == K_RESULT.sha256());
    const auto K_EXPORT = export_request(fixture);
    const auto K_FILES = checked(prepare_native_export(K_EXPORT));
    CHECK(json(file(K_FILES, "manifest.json").m_bytes)["manual_ancestor"] == true);
    const auto K_PACKAGE = checked(inspect_native_export(K_EXPORT, K_FILES));
    const auto K_SCENE = checked(compile_native_pbr_scene(K_PACKAGE, input.m_expected));
    CHECK(required(required(K_SCENE->m_chunks.begin()->second->m_mesh).m_pbr).m_manual_ancestor);
}

TEST_CASE(
    "native structure publication preserves committed snapshots through movement and cancellation",
    "[native-structure]") {
    NativeStructureFixture fixture;
    const auto K_INPUT = fixture.request();
    const auto K_INITIAL = checked(inspect_native_structure(K_INPUT));
    const auto K_FIRST = checked(publish_native_structure(
        K_INITIAL, fixture.m_receipt, fixture.directory(), { 1 }, 0, K_INPUT.m_expected));
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.size() == 4uz);
    CHECK(read_native_structure(fixture.directory(), K_FIRST, K_INPUT));
    auto source = fixture.assets();
    auto policy = K_INPUT;
    auto selection = fixture.m_base.m_selected;
    selection.m_max_plane_residual_units = .05;
    const auto K_CAPTURE = checked(fixture.m_base.m_base.m_views.inspect(fixture.m_receipt, false));
    policy.m_structures[0uz].m_graph =
        checked(inspect_structure_graph({ selection,
                                         source.m_assets[0uz].m_geometry,
                                         K_CAPTURE,
                                         fixture.m_receipt,
                                         source.m_expected }));
    const auto K_POLICY = checked(inspect_native_structure(policy));
    CHECK(K_POLICY.assets().instances().sha256() == K_INITIAL.assets().instances().sha256());
    CHECK(K_POLICY.sha256() != K_INITIAL.sha256());
    CHECK(!publish_native_structure(
        K_POLICY, fixture.m_receipt, fixture.directory(), { 99 }, 1, K_INPUT.m_expected));
    CHECK(read_native_structure(fixture.directory(), K_FIRST, K_INPUT));
    source.m_instances.m_instances[1uz].m_world_from_asset.m_translation.m_x += 3;
    auto changed = K_INPUT;
    changed.m_assets = checked(inspect_native_assets(source));
    const auto K_SAME_REVISION = checked(inspect_native_structure(changed));
    CHECK(!publish_native_structure(
        K_SAME_REVISION, fixture.m_receipt, fixture.directory(), { 2 }, 1, K_INPUT.m_expected));
    CHECK(read_native_structure(fixture.directory(), K_FIRST, K_INPUT));
    source.m_instances.m_revision = { 2 };
    changed.m_assets = checked(inspect_native_assets(source));
    const auto K_NEXT = checked(inspect_native_structure(changed));
    std::stop_source cancel;
    const auto K_INTERRUPTED = publish_native_structure(K_NEXT,
                                                        fixture.m_receipt,
                                                        fixture.directory(),
                                                        { 3 },
                                                        1,
                                                        changed.m_expected,
                                                        cancel.get_token(),
                                                        [&](std::size_t) {
                                                            cancel.request_stop();
                                                        });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::CANCELLED);
    CHECK(read_native_structure(fixture.directory(), K_FIRST, K_INPUT));
    const auto K_COMMIT = checked(publish_native_structure(
        K_NEXT, fixture.m_receipt, fixture.directory(), { 3 }, 1, changed.m_expected));
    CHECK(K_COMMIT.m_generation == 2);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.size() == 6uz);
    CHECK(read_native_structure(fixture.directory(), K_COMMIT, changed));
    CHECK(read_native_structure(fixture.directory(), K_COMMIT, K_INPUT));
    CHECK(!read_native_structure(fixture.directory(), K_FIRST, K_INPUT));
    CHECK(checked(publish_native_structure(
                      K_NEXT, fixture.m_receipt, fixture.directory(), { 3 }, 1, changed.m_expected))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(!publish_native_structure(K_INITIAL,
                                    fixture.m_receipt,
                                    fixture.m_receipt.source_directory(),
                                    { 1 },
                                    0,
                                    changed.m_expected));
    std::stop_source stopped;
    stopped.request_stop();
    CHECK(!inspect_native_structure(K_INPUT, stopped.get_token()));
}

TEST_CASE("native structured export retains exact roots maps and ordinary PBR resources",
          "[native-structure]") {
    NativeStructureFixture fixture;
    const auto K_REQUEST = export_request(fixture);
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, K_FILES));
    auto legacy = K_REQUEST;
    legacy.m_structure.reset();
    const auto K_LEGACY = checked(prepare_native_export(legacy));
    for (const auto& resource : K_LEGACY) {
        if (resource.m_path != "manifest.json" && resource.m_path != "environment.gltf") {
            CHECK(file(K_FILES, resource.m_path).m_bytes == resource.m_bytes);
        }
    }
    const auto K_MANIFEST = json(file(K_FILES, "manifest.json").m_bytes);
    CHECK(K_MANIFEST["schema_version"] == 2);
    CHECK(K_MANIFEST["profile"] == "WE_NATIVE_STRUCTURED_GLTF_V1");
    CHECK(K_MANIFEST["structure"]["sha256"] == required(K_REQUEST.m_structure).sha256());
    CHECK(sha256(file(K_FILES, "structure/bindings.json").m_bytes) ==
          required(K_REQUEST.m_structure).sha256());
    CHECK(file(K_FILES, "structure/13000-1.json").m_bytes ==
          checked(fixture.m_receipt.copy_blob(fixture.m_base.m_selected.m_product.m_content_sha256,
                                              256uz * 1024uz)));
    const auto K_SCENE = checked(compile_native_pbr_scene(K_PACKAGE, K_REQUEST.m_expected));
    CHECK(K_SCENE->m_provenance.at("profile") == "WE_NATIVE_STRUCTURED_GLTF_V1");
    const auto& pbr = required(required(K_SCENE->m_chunks.begin()->second->m_mesh).m_pbr);
    CHECK(pbr.m_export_manifest.find(required(K_REQUEST.m_structure).sha256()) !=
          std::string::npos);
    const auto K_DIRECTORY = fixture.m_output.m_root / "structured.wexplore";
    REQUIRE(write_bundle(K_DIRECTORY, K_SCENE));
    CHECK(scene_content_hash(*checked(load_bundle(K_DIRECTORY))) == scene_content_hash(*K_SCENE));
#if defined(__linux__)
    if (auto path = std::getenv("WE_STRUCTURE_SCENE_PROBE_OUTPUT")) {
        REQUIRE(write_bundle(path, K_SCENE));
    }
    if (auto path = std::getenv("WE_STRUCTURE_EXPORT_PROBE_OUTPUT")) {
        REQUIRE(write_native_export(K_PACKAGE, path));
    }
#endif
}

TEST_CASE("native structured package rejects rehashed metadata substitution and stale sources",
          "[native-structure]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    NativeStructureFixture fixture;
    auto request = export_request(fixture);
    auto files = checked(prepare_native_export(request));
    if (K_CASE == 0) {
        auto found = std::ranges::find(
            files, std::string("structure/bindings.json"), &ExportPackageFile::m_path);
        REQUIRE(found != files.end());
        found->m_bytes.push_back(std::byte { 32 });
        auto manifest_file =
            std::ranges::find(files, std::string("manifest.json"), &ExportPackageFile::m_path);
        REQUIRE(manifest_file != files.end());
        auto manifest = json(manifest_file->m_bytes);
        manifest["structure"]["sha256"] = sha256(found->m_bytes);
        for (auto& row : manifest["files"]) {
            if (row["path"].get<std::string>() == "structure/bindings.json") {
                row["sha256"] = sha256(found->m_bytes);
                row["bytes"] = found->m_bytes.size();
            }
        }
        const auto K_TEXT = manifest.dump();
        const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
        manifest_file->m_bytes.assign(K_BYTES.begin(), K_BYTES.end());
    } else if (K_CASE == 1) {
        files.pop_back();
    } else if (K_CASE == 2) {
        auto source = fixture.assets();
        source.m_instances.m_revision = { 2 };
        request.m_assets = checked(inspect_native_assets(source));
    } else {
        auto source = fixture.assets();
        source.m_assets[0uz].m_definition.m_kind = AssetKind::PROP;
        source.m_assets[0uz].m_definition.m_pivot.m_policy = AssetPivotPolicy::ATTACHMENT_ORIGIN;
        rebind(source);
        auto structured = fixture.request();
        structured.m_assets = checked(inspect_native_assets(source));
        structured.m_structures[0uz].m_definition =
            source.m_instances.m_instances[0uz].m_definition;
        CHECK(!inspect_native_structure(structured));
        return;
    }
    CHECK(!inspect_native_export(request, files));
}

TEST_CASE("native structure qualifies repeated graph identities by distinct semantic instances",
          "[native-structure]") {
    NativeStructureFixture fixture;
    auto source = fixture.assets();
    auto duplicate = source.m_assets[0uz];
    duplicate.m_definition.m_asset = { 43 };
    duplicate.m_source_instance = { 3 };
    const auto K_DEFINITION = checked(admit_asset_definition(
        duplicate.m_definition, fixture.m_receipt, duplicate.m_provenance, source.m_expected));
    auto copy = source.m_instances.m_instances[0uz];
    copy.m_instance = { 3 };
    copy.m_definition = K_DEFINITION.reference();
    source.m_instances.m_instances.push_back(copy);
    source.m_assets.push_back(duplicate);
    auto input = fixture.request();
    input.m_assets = checked(inspect_native_assets(source));
    auto binding = input.m_structures[0uz];
    binding.m_definition = K_DEFINITION.reference();
    binding.m_instances = { { 3 } };
    input.m_structures.push_back(binding);
    const auto K_RESULT = checked(inspect_native_structure(input));
    CHECK(K_RESULT.structures().size() == 2uz);
    CHECK(K_RESULT.records().size() == 2uz);  // one shared exact graph plus binding
    CHECK(K_RESULT.assets().instances().definitions().size() == 2uz);
    std::ranges::reverse(input.m_structures);
    CHECK(checked(inspect_native_structure(input)).sha256() == K_RESULT.sha256());
    input.m_structures.pop_back();
    CHECK(!inspect_native_structure(input));
}

TEST_CASE("native structured bundle rejects rehashed profile and binding pin substitutions",
          "[native-structure]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    NativeStructureFixture fixture;
    const auto K_REQUEST = export_request(fixture);
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, K_FILES));
    auto scene = std::make_shared<SceneState>(
        *checked(compile_native_pbr_scene(K_PACKAGE, K_REQUEST.m_expected)));
    auto chunk = std::make_shared<SceneChunk>(*scene->m_chunks.begin()->second);
    if (!chunk->m_mesh) {
        throw std::logic_error("Structured geometry missing");
    }
    auto& mesh = chunk->m_mesh.value();
    if (!mesh.m_pbr) {
        throw std::logic_error("Structured appearance missing");
    }
    auto& pbr = mesh.m_pbr.value();
    auto doc = Json::parse(pbr.m_export_manifest);
    if (K_CASE == 0) {
        doc.erase("structure");
    } else if (K_CASE == 1) {
        doc["schema_version"] = 1;
    } else if (K_CASE == 2) {
        doc["structure"]["sha256"] = std::string(64uz, '0');
    } else if (K_CASE == 3) {
        doc["structure"]["path"] = "other.json";
    } else {
        doc["structure"]["scope"] = "PHYSICAL_SUPPORT";
    }
    pbr.m_export_manifest = doc.dump();
    pbr.m_export_manifest_sha256 = hash(pbr.m_export_manifest);
    scene->m_chunks[chunk->m_id] = chunk;
    CHECK(!validate_scene(*scene));
}

TEST_CASE("native structure composes noncommuting rotations without moving the source shell",
          "[native-structure]") {
    NativeStructureFixture fixture;
    auto source = fixture.assets();
    auto& definition = source.m_assets[0uz].m_definition;
    definition.m_pivot.m_asset_from_geometry.m_rotation = { 0, 1, 0, 0 };
    definition.m_bounds = { { -1, -6, -3 }, { 3, -2, -3 } };
    rebind(source);
    source.m_instances.m_instances[0uz].m_world_from_asset = { { 0, -1, 0, 0 }, { 12, -8, -3 }, 2 };
    source.m_instances.m_instances[1uz].m_world_from_asset.m_rotation = { 0, 0, 1, 0 };
    auto input = fixture.request();
    input.m_assets = checked(inspect_native_assets(source));
    input.m_structures[0uz].m_definition = source.m_instances.m_instances[0uz].m_definition;
    const auto K_RESULT = checked(inspect_native_structure(input));
    const auto K_DOC = Json::parse(K_RESULT.canonical_json());
    const auto& frames = K_DOC["structures"][0uz]["instances"];
    CHECK(frames[0uz]["world_from_graph"]["rotation"] == Json::array({ 1.0, 0.0, 0.0, 0.0 }));
    CHECK(frames[0uz]["world_from_graph"]["translation"] == Json::array({ 10.0, -4.0, 3.0 }));
    CHECK(frames[1uz]["world_from_graph"]["rotation"] == Json::array({ 0.0, 0.0, 0.0, -1.0 }));
    CHECK(frames[1uz]["world_from_graph"]["translation"] == Json::array({ 32.0, 1.0, 15.0 }));
}
