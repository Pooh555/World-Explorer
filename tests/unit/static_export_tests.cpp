#include "../fixtures/static_export_fixture.hpp"
#include "world_explorer/core/asset_files.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cstdlib>
#include <limits>
#include <span>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::static_export_test;

TEST_CASE(
    "static export produces shared mesh material resources with authoritative source sidecars",
    "[static-export]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedExportManifest, AdmittedAssetInstances>);
    StaticExportFixture fixture;
    auto files = checked(prepare_static_export(fixture.request()));
    const auto K_ADMITTED = inspect_static_export(fixture.request(), files);
    INFO((K_ADMITTED ? "admitted" : K_ADMITTED.error().m_message));
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->source_instances_sha256() == fixture.m_scene.sha256());
    CHECK(K_ADMITTED->receipt_sha256() == fixture.m_receipt.receipt_sha256());
    const auto K_MANIFEST = document(file(files, "manifest.json"));
    CHECK(K_MANIFEST["qualification"] == "UNQUALIFIED_EXPORT");
    CHECK(K_MANIFEST["importer"]["state"] == "NOT_RUN");
    const auto K_GLB = glb_document(file(files, "scene.glb").m_bytes);
    CHECK(K_GLB["meshes"].size() == 1uz);
    CHECK(K_GLB["materials"].size() == 1uz);
    REQUIRE(K_GLB["nodes"].size() == 2uz);
    CHECK(K_GLB["nodes"][0uz]["mesh"] == K_GLB["nodes"][1uz]["mesh"]);
    CHECK(K_GLB["materials"][0uz]["pbrMetallicRoughness"]["baseColorFactor"] ==
          Json { .25, .5, .75, 1 });
    CHECK(K_GLB["materials"][0uz]["emissiveFactor"] == Json { 0, 0, 0 });
    CHECK(!K_GLB.contains("extensionsRequired"));
    CHECK(!K_GLB.contains("images"));
    CHECK(document(file(files, "rights/42.json"))["opaque_payload_exported"] == false);
    CHECK(K_MANIFEST["assets"][0uz]["definition"]["asset_id"] == "42");
    CHECK(file(files, "scene.glb").m_bytes.size() == word(file(files, "scene.glb").m_bytes, 8uz));
    files.clear();
    CHECK(!K_ADMITTED->files().empty());
}

TEST_CASE(
    "static export pivot and world transforms preserve converted geometry and quaternion order",
    "[static-export]") {
    StaticExportOptions options;
    options.m_definition = [](auto& value) {
        value.m_kind = AssetKind::STRUCTURAL;
        value.m_pivot.m_policy = AssetPivotPolicy::STRUCTURAL_FRAME;
        value.m_pivot.m_asset_from_geometry = { { 0, 0, 0, 1 }, { 2, 3, 4 }, 1 };
    };
    options.m_instances = [](auto& value) {
        value.m_instances[0uz].m_world_from_asset = { { 0, 1, 0, 0 }, { 10, 20, 30 }, 2 };
    };
    StaticExportFixture fixture(options);
    auto files = checked(prepare_static_export(fixture.request()));
    const auto& bytes = file(files, "scene.glb").m_bytes;
    const auto K_DOC = glb_document(bytes);
    const auto K_BIN = 28uz + word(bytes, 12uz);
    CHECK(std::bit_cast<float>(word(bytes, K_BIN)) == 2.F);
    CHECK(std::bit_cast<float>(word(bytes, K_BIN + 4uz)) == 3.F);
    CHECK(std::bit_cast<float>(word(bytes, K_BIN + 8uz)) == 4.F);
    CHECK(K_DOC["nodes"][0uz]["rotation"] == Json { 1, 0, 0, 0 });
    CHECK(K_DOC["nodes"][0uz]["translation"] == Json { 10, 20, 30 });
    CHECK(document(file(files, "manifest.json"))["conversion"]["max_position_error_units"] == 0);
    CHECK(inspect_static_export(fixture.request(), files));
}

TEST_CASE("static export individual and assembled resources retain identical canonical geometry "
          "and material hashes",
          "[static-export]") {
    StaticExportFixture one({ .m_instances_per_asset = 1uz });
    StaticExportFixture two;
    auto first = checked(prepare_static_export(one.request())),
         second = checked(prepare_static_export(two.request()));
    const auto K_ONE = document(file(first, "manifest.json"))["assets"][0uz];
    const auto K_TWO = document(file(second, "manifest.json"))["assets"][0uz];
    CHECK(K_ONE["geometry_bytes_sha256"] == K_TWO["geometry_bytes_sha256"]);
    CHECK(K_ONE["material_json_sha256"] == K_TWO["material_json_sha256"]);
    auto selected = two.request();
    std::ranges::reverse(selected.m_source.m_definitions);
    std::ranges::reverse(selected.m_source.m_definitions[0uz].m_provenance);
    std::ranges::reverse(second);
    CHECK(inspect_static_export(selected, second));
}

TEST_CASE("static export keeps distinct definition identity while sharing each definition across "
          "its nodes",
          "[static-export]") {
    StaticExportFixture fixture({ .m_assets = 2uz, .m_instances_per_asset = 2uz });
    auto files = checked(prepare_static_export(fixture.request()));
    const auto K_DOC = glb_document(file(files, "scene.glb").m_bytes);
    CHECK(K_DOC["meshes"].size() == 2uz);
    CHECK(K_DOC["nodes"].size() == 4uz);
    CHECK(K_DOC["nodes"][0uz]["mesh"] == 0);
    CHECK(K_DOC["nodes"][1uz]["mesh"] == 0);
    CHECK(K_DOC["nodes"][2uz]["mesh"] == 1);
    CHECK(inspect_static_export(fixture.request(), files));
}

TEST_CASE("static export reports float32 geometry loss and enforces an independent position policy",
          "[static-export]") {
    StaticExportOptions options;
    options.m_payload = [](Json& geometry, auto&) {
        geometry["positions"][1uz][0uz] = .1;
    };
    StaticExportFixture fixture(options);
    auto request = fixture.request();
    auto files = checked(prepare_static_export(request));
    CHECK(document(file(files, "manifest.json"))["conversion"]["max_position_error_units"]
              .get<double>() > 0);
    request.m_source.m_max_position_error_units = 0;
    CHECK(!prepare_static_export(request));
}

TEST_CASE("static export refuses unsupported static channels and hidden instance semantics",
          "[static-export]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4);
    StaticExportOptions options;
    options.m_payload = [&](Json& geometry, Json& material) {
        if (K_CASE == 4) {
            geometry["positions"][1uz][0uz] = 2e6;
        }
        if (K_CASE == 0) {
            material["occlusion"] = .5;
        }
        if (K_CASE == 1) {
            material["emission"] = { 2, 0, 0 };
        }
    };
    options.m_instances = [&](auto& value) {
        if (K_CASE == 2) {
            value.m_instances[0uz].m_visible = false;
        }
        if (K_CASE == 3) {
            value.m_instances[0uz].m_world_from_asset.m_rotation = { 1.000001, 0, 0, 0 };
        }
    };
    StaticExportFixture fixture(options);
    const auto K_RESULT = prepare_static_export(fixture.request());
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}

TEST_CASE(
    "static export rejects altered canonical GLB manifest sidecars missing files and unsafe paths",
    "[static-export]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    StaticExportFixture fixture;
    auto files = checked(prepare_static_export(fixture.request()));
    switch (K_CASE) {
    case 0:
        file(files, "scene.glb").m_bytes.back() = std::byte { 255 };
        break;
    case 1:
        file(files, "manifest.json").m_bytes.push_back(std::byte { ' ' });
        break;
    case 2:
        files.erase(files.begin());
        break;
    case 3:
        files.push_back(files[0uz]);
        break;
    case 4:
        files[0uz].m_path = "../outside";
        break;
    case 5:
        file(files, "scene.json").m_bytes.clear();
        break;
    case 6:
        file(files, "catalog.json").m_bytes.push_back(std::byte { ' ' });
        break;
    case 7:
        files.push_back({ "extra.bin", { std::byte { 1 } } });
        break;
    default:
        throw std::logic_error("Unexpected export corruption case");
    }
    CHECK(!inspect_static_export(fixture.request(), files));
}

TEST_CASE("static export independent source audit and provenance selections are mandatory",
          "[static-export]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    StaticExportFixture fixture;
    auto request = fixture.request();
    switch (K_CASE) {
    case 0:
        request.m_source.m_scene.reset();
        break;
    case 1:
        request.m_source.m_receipt.reset();
        break;
    case 2:
        request.m_validator.reset();
        break;
    case 3:
        request.m_importer.reset();
        break;
    case 4:
        request.m_source.m_definitions.clear();
        break;
    case 5:
        request.m_source.m_definitions[0uz].m_provenance.pop_back();
        break;
    case 6:
        request.m_source.m_expected.m_pose_revision = { 6 };
        break;
    case 7:
        request.m_source.m_max_normal_error = std::numeric_limits<double>::quiet_NaN();
        break;
    case 8:
        request.m_source.m_definitions[0uz].m_definition.m_revision = 2;
        break;
    default:
        throw std::logic_error("Unexpected export source case");
    }
    CHECK(!prepare_static_export(request));
}

TEST_CASE("static export reported validator observations pin raw report identity and counts "
          "without qualification",
          "[static-export]") {
    const bool K_FAILURE = GENERATE(true, false);
    StaticExportFixture fixture;
    auto request = fixture.request();
    auto original = checked(prepare_static_export(request));
    request.m_validator = validator_audit(
        file(original, "scene.glb").m_bytes, "synthetic-fixture", K_FAILURE ? 1 : 0);
    auto files = checked(prepare_static_export(request));
    const auto K_AUDIT = document(file(files, "audit-validator.json"));
    CHECK(K_AUDIT["state"] == (K_FAILURE ? "REPORTED_FAIL" : "REPORTED_PASS"));
    CHECK(K_AUDIT["execution_authenticity"] == "UNVERIFIED");
    CHECK(inspect_static_export(request, files));
}

TEST_CASE("static export rejects false validator report counts hashes outcome and tool identity",
          "[static-export]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    StaticExportFixture fixture;
    auto request = fixture.request();
    auto original = checked(prepare_static_export(request));
    auto audit = validator_audit(file(original, "scene.glb").m_bytes);
    if (!audit.m_tool) {
        throw std::logic_error("Missing test validator tool");
    }
    switch (K_CASE) {
    case 0:
        audit.m_errors = 1;
        break;
    case 1:
        audit.m_report_sha256 = hash("foreign report");
        break;
    case 2:
        audit.m_tool->m_version = "different";
        break;
    case 3:
        audit.m_tool->m_name = "UNSUPPORTED";
        break;
    case 4:
        audit.m_state = ExportAuditState::NOT_RUN;
        break;
    case 5:
        audit.m_payload_sha256 = hash("foreign GLB");
        break;
    default:
        throw std::logic_error("Unexpected validator audit case");
    }
    request.m_validator = audit;
    CHECK(!prepare_static_export(request));
}

TEST_CASE("static export importer declarations require reported resource sharing and payload hash",
          "[static-export]") {
    const bool K_WRONG = GENERATE(true, false);
    StaticExportFixture fixture;
    auto request = fixture.request();
    auto original = checked(prepare_static_export(request));
    const auto K_TEXT = Json { { "schema_version", 1 },
                              { "kind", "GODOT_IMPORT_OBSERVATION" },
                              { "glb_sha256", sha256(file(original, "scene.glb").m_bytes) },
                              { "errors", 0 },
                              { "warnings", 0 },
                              { "mesh_count", 1 },
                              { "node_count", 2 },
                              { "node_mesh_indices", K_WRONG ? Json { 0, 1 } : Json { 0, 0 } } }
                            .dump();
    const auto K_BYTES = std::as_bytes(std::span(K_TEXT));
    request.m_importer = ExportAudit { ExportAuditState::REPORTED_PASS,
                                      ExportToolIdentity { "GODOT",
                                                          "synthetic-fixture",
                                                          hash("synthetic Godot executable"),
                                                          hash("synthetic Godot package") },
                                      hash(K_TEXT),
                                      { K_BYTES.begin(), K_BYTES.end() },
                                      0,
                                      0,
                                      sha256(file(original, "scene.glb").m_bytes) };
    const auto K_RESULT = prepare_static_export(request);
    CHECK(K_RESULT.has_value() == !K_WRONG);
}

TEST_CASE(
    "static export immutable batch cancellation replay and independent reload require every file",
    "[static-export]") {
    StaticExportFixture fixture;
    auto files = checked(prepare_static_export(fixture.request()));
    const auto K_ADMITTED = checked(inspect_static_export(fixture.request(), files));
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_export_manifest(K_ADMITTED,
                                                  fixture.m_receipt,
                                                  fixture.directory(),
                                                  { 261 },
                                                  0,
                                                  K_CONTEXT,
                                                  stop.get_token(),
                                                  [&](std::size_t count) {
                                                      if (count == 2uz) {
                                                          stop.request_stop();
                                                      }
                                                  });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_export_manifest(
        K_ADMITTED, fixture.m_receipt, fixture.directory(), { 261 }, 0, K_CONTEXT));
    CHECK(checked(publish_export_manifest(
                      K_ADMITTED, fixture.m_receipt, fixture.directory(), { 261 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_export_manifest(fixture.directory(), K_COMMIT, fixture.request(), files));
    auto stale = K_COMMIT;
    ++stale.m_generation;
    CHECK(!read_export_manifest(fixture.directory(), stale, fixture.request(), files));
    const auto K_SNAPSHOT = checked(read_immutable_records(fixture.directory()));
    std::vector<ImmutableRecord> partial;
    for (const auto& [K_KEY, K_BYTES] : K_SNAPSHOT.m_records) {
        if (partial.size() + 1uz < K_SNAPSHOT.m_records.size()) {
            partial.push_back({ K_KEY, K_BYTES });
        }
    }
    const auto K_PARTIAL = fixture.directory().parent_path() / "partial";
    const auto K_OTHER = checked(publish_immutable_records(K_PARTIAL, { 262 }, 0, partial, {}));
    CHECK(!read_export_manifest(K_PARTIAL, K_OTHER, fixture.request(), files));
    CHECK(!publish_export_manifest(
        K_ADMITTED, fixture.m_receipt, fixture.m_receipt.source_directory(), { 263 }, 0, K_CONTEXT));
}

TEST_CASE(
    "static export writes consumable local files last manifest and preserves existing destinations",
    "[static-export]") {
    StaticExportFixture fixture;
    auto files = checked(prepare_static_export(fixture.request()));
    const auto K_ADMITTED = checked(inspect_static_export(fixture.request(), files));
    const auto K_OUTPUT = fixture.directory().parent_path() / "export";
    CHECK(write_static_export(K_ADMITTED, K_OUTPUT));
    for (const auto& output : K_ADMITTED.files()) {
        const auto K_BYTES =
            checked(asset_files::read_bytes(K_OUTPUT / output.m_path, 1024uz * 1024uz, {}));
        CHECK(K_BYTES == output.m_bytes);
    }
    CHECK(!write_static_export(K_ADMITTED, K_OUTPUT));
    CHECK(!write_static_export(K_ADMITTED, fixture.m_receipt.source_directory() / "forbidden"));
    std::stop_source stop;
    stop.request_stop();
    CHECK(!write_static_export(K_ADMITTED, K_OUTPUT.parent_path() / "cancelled", stop.get_token()));
}

TEST_CASE(
    "static export exact asset and instance counts admit and excess refuses before conversion",
    "[static-export]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    StaticExportFixture fixture(
        { .m_assets = K_CASE == 1 ? 17uz : 16uz, .m_instances_per_asset = K_CASE == 2 ? 5uz : 4uz });
    const auto K_RESULT = prepare_static_export(fixture.request());
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        CHECK(inspect_static_export(fixture.request(), *K_RESULT));
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
}

TEST_CASE("static export selected files bound bytes count and paths before parsing",
          "[static-export]") {
    StaticExportFixture fixture;
    std::vector<ExportPackageFile> files(57uz);
    CHECK(!inspect_static_export(fixture.request(), files));
    files = checked(prepare_static_export(fixture.request()));
    files[0uz].m_bytes.resize(1024uz * 1024uz + 1uz);
    CHECK(!inspect_static_export(fixture.request(), files));
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCEL = prepare_static_export(fixture.request(), stop.get_token());
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(!prepare_static_export({}));
}

TEST_CASE("static export synthetic external validation probe", "[static-export]") {
    StaticExportFixture fixture;
    auto request = fixture.request();
    // Linux-only opt-in evidence; the ordinary test path writes no retained artifact.
#if defined(__linux__)
    if (auto audit_path = std::getenv("WE_EXPORT_PROBE_AUDIT")) {
        const auto K_RAW = checked(asset_files::read_bytes(audit_path, 128uz * 1024uz, {}));
        const auto K_DOC = Json::parse(
            std::string_view(reinterpret_cast<const char*>(K_RAW.data()), K_RAW.size()));
        const auto K_REPORT = K_DOC.at("report").dump();
        const auto K_BYTES = std::as_bytes(std::span(K_REPORT));
        request.m_validator =
            ExportAudit { ExportAuditState::REPORTED_PASS,
                         ExportToolIdentity { "KHRONOS_GLTF_VALIDATOR",
                                             K_DOC.at("version").get<std::string>(),
                                             K_DOC.at("executable_sha256").get<std::string>(),
                                             K_DOC.at("package_sha256").get<std::string>() },
                         hash(K_REPORT),
                         { K_BYTES.begin(), K_BYTES.end() },
                         0,
                         K_DOC.at("report").at("issues").at("numWarnings").get<std::uint64_t>(),
                         K_DOC.at("glb_sha256").get<std::string>() };
    }
#endif
    const auto K_FILES = checked(prepare_static_export(request));
    const auto K_ADMITTED = checked(inspect_static_export(request, K_FILES));
    CHECK(K_ADMITTED.files().size() >= 9uz);
#if defined(__linux__)
    if (auto output_path = std::getenv("WE_EXPORT_PROBE_OUTPUT")) {
        CHECK(write_static_export(K_ADMITTED, output_path));
    }
#endif
}
