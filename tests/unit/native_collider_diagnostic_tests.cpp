#include "../fixtures/native_collider_fixture.hpp"
#include "../fixtures/native_lod_fixture.hpp"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/native_collider_diagnostic.hpp"
#include "world_explorer/core/asset_files.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>

using namespace world_explorer;
using namespace world_explorer::native_collider_test;
using world_explorer::asset_component_test::tree;

namespace {

NativeAssetsRequest prepared(const ColliderFixture& fixture, bool rotated) {
    auto input = fixture.request();
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    if (rotated) {
        auto& asset = input.m_assets[0uz];
        auto& pivot = asset.m_definition.m_pivot.m_asset_from_geometry;
        pivot.m_rotation = { .5, .5, .5, .5 };
        const Quatd K_INVERSE { .5, -.5, -.5, -.5 };
        const auto& base = native_geometry(asset);
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
        auto& original = input.m_instances.m_instances[0uz].m_world_from_asset;
        original.m_rotation = K_INVERSE;
        original.m_translation = base.world_from_geometry().m_translation -
                                 rotate(K_INVERSE, pivot.m_translation) * original.m_scale;
        input.m_instances.m_instances[1uz].m_world_from_asset = {
            { std::cos(.31), 0, std::sin(.31), 0 }, { 30.125, 5.25, -9.5 }, .75 };
        rebind(input);
    }
    return input;
}

NativeExportRequest export_request(const AdmittedNativeAssets& assets,
                                   const NativeAssetsRequest& input) {
    NativeExportRequest request { assets, input.m_receipt, input.m_expected, {} };
    request.m_colliders = NativeExportColliderPolicy {};
    return request;
}

AdmittedNativeExport package(const NativeExportRequest& input) {
    return checked(inspect_native_export(input, checked(prepare_native_export(input))));
}

std::vector<NativeColliderQuery> queries(const AdmittedNativeAssets& assets) {
    std::vector<NativeColliderQuery> result;
    std::uint64_t next { 1 };
    for (const auto& source : assets.sources()) {
        if (!source.m_collider) {
            continue;
        }
        const auto& mesh = source.m_collider->mesh();
        for (const auto& instance : assets.instances().instances().m_instances) {
            if (instance.m_definition.m_asset != source.m_definition.m_asset) {
                continue;
            }
            const auto K_WORLD = [&](Vec3d point) {
                return transform_point(
                    instance.m_world_from_asset,
                    transform_point(source.m_definition.m_pivot.m_asset_from_geometry, point));
            };
            for (const auto& face : mesh.m_triangles) {
                const auto K_A = mesh.m_positions[face[0uz]], K_B = mesh.m_positions[face[1uz]],
                           K_C = mesh.m_positions[face[2uz]];
                const auto K_CENTER = K_A * .2 + K_B * .3 + K_C * .5;
                auto normal = cross(K_B - K_A, K_C - K_A);
                normal = normal * (.2 / std::sqrt(dot(normal, normal)));
                const auto K_START = K_WORLD(K_CENTER + normal), K_END = K_WORLD(K_CENTER - normal);
                result.push_back({ next++, instance.m_instance, K_START, K_END });
                result.push_back({ next++, instance.m_instance, K_END, K_START });
            }
            result.push_back(
                { next++, instance.m_instance, K_WORLD({ -3, -3, 1 }), K_WORLD({ -3, -3, -1 }) });
        }
    }
    return result;
}

struct Fixture {
    ColliderFixture m_source;
    NativeAssetsRequest m_input;
    AdmittedNativeAssets m_assets;
    AdmittedNativeExport m_package;
    RecipeFixture m_output;
    NativeColliderDiagnosticOutput m_diagnostic;

    explicit Fixture(bool rotated = false)
        : m_input(prepared(m_source, rotated)), m_assets(checked(inspect_native_assets(m_input))),
          m_package(package(export_request(m_assets, m_input))),
          m_diagnostic(checked(inspect_native_colliders(
              m_assets, m_package, m_input.m_expected, queries(m_assets), "package"))) {
        REQUIRE(write_native_export(m_package, m_output.m_root / "package"));
        write(request(), m_diagnostic.m_request_json);
    }

    std::filesystem::path request() const {
        return m_output.m_root / "request.json";
    }

    Result<std::string> inspect(const Json& request_doc) const {
        const auto K_TEXT = request_doc.dump();
        write(request(), K_TEXT);
        return inspect_native_colliders_request(request(), hash(K_TEXT));
    }
};

}  // namespace

TEST_CASE("native collider diagnostic shares actual shapes across selected pivoted instances and "
          "CLI replay",
          "[collider-diagnostic][collider-diagnostic-product]") {
    const auto K_ROTATED = GENERATE(false, true);
    Fixture fixture(K_ROTATED);
    const auto K_BEFORE = tree(fixture.m_output.m_root),
               K_SOURCE = tree(fixture.m_source.m_base.m_receipt.source_directory());
    const auto K_REPORT = checked(inspect_native_colliders_request(
        fixture.request(), hash(fixture.m_diagnostic.m_request_json)));
    CHECK(K_REPORT == fixture.m_diagnostic.m_report_json);
    const auto K_DOC = Json::parse(K_REPORT);
    CHECK(K_DOC["kind"] == "SYNTHETIC_GAMEPLAY_COLLIDER_DIAGNOSTIC");
    CHECK(K_DOC["walking_enabled"] == false);
    CHECK(K_DOC["body_installed"] == false);
    CHECK(K_DOC["shapes"].size() == 1uz);
    CHECK(K_DOC["instances"].size() == 2uz);
    CHECK(K_DOC["queries"].size() == 10uz);
    std::size_t hits {};
    for (const auto& query : K_DOC["queries"]) {
        CHECK(query["world_endpoint_error_units"].get<double>() <= 1e-4);
        if (!query["hit"].is_null()) {
            ++hits;
            CHECK(query["hit"]["fraction"].get<double>() == Catch::Approx(.5).margin(2e-4));
        }
    }
    CHECK(hits == 4uz);
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
    CHECK(tree(fixture.m_source.m_base.m_receipt.source_directory()) == K_SOURCE);
    if (auto output = std::getenv("WE_COLLIDER_DIAGNOSTIC_PROBE_ROOT")) {
        const auto K_ROOT = std::filesystem::path(output) / (K_ROTATED ? "rotated" : "plain");
        std::filesystem::create_directories(K_ROOT);
        REQUIRE(write_native_export(fixture.m_package, K_ROOT / "package"));
        write(K_ROOT / "request.json", fixture.m_diagnostic.m_request_json);
        write(K_ROOT / "report.json", K_REPORT);
    }
}

TEST_CASE("native collider diagnostic refuses changed current assets and permits fresh immutable "
          "selections",
          "[collider-diagnostic]") {
    Fixture fixture;
    auto changed = fixture.m_input;
    changed.m_instances.m_instances[1uz].m_world_from_asset.m_translation.m_x += 3;
    ++changed.m_instances.m_revision.m_value;
    const auto K_CURRENT = checked(inspect_native_assets(changed));
    const auto K_OLD = inspect_native_colliders(
        K_CURRENT, fixture.m_package, changed.m_expected, queries(K_CURRENT), "package");
    REQUIRE(!K_OLD);
    CHECK(K_OLD.error().m_code == ErrorCode::RESYNC_REQUIRED);
    const auto K_NEW_PACKAGE = package(export_request(K_CURRENT, changed));
    const auto K_NEW = checked(inspect_native_colliders(
        K_CURRENT, K_NEW_PACKAGE, changed.m_expected, queries(K_CURRENT), "package"));
    CHECK(K_NEW.m_request_json != fixture.m_diagnostic.m_request_json);
    CHECK(K_NEW.m_report_json != fixture.m_diagnostic.m_report_json);
    CHECK(checked(inspect_native_colliders_request(fixture.request(),
                                                   hash(fixture.m_diagnostic.m_request_json))) ==
          fixture.m_diagnostic.m_report_json);
    auto context = changed.m_expected;
    ++context.m_pose_revision.m_value;
    CHECK(!inspect_native_colliders(
        K_CURRENT, K_NEW_PACKAGE, context, queries(K_CURRENT), "package"));
}

TEST_CASE(
    "native collider diagnostic rejects rehashed stale pins frames and malformed query selections",
    "[collider-diagnostic]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20);
    Fixture fixture;
    auto request = Json::parse(fixture.m_diagnostic.m_request_json);
    auto& selected = request["selection"];
    auto& row = selected["assets"][0uz];
    switch (K_CASE) {
    case 0:
        request["schema_version"] = 2;
        break;
    case 1:
        request["extra"] = true;
        break;
    case 2:
        selected["source_instances_sha256"] = hash("other instances");
        break;
    case 3:
        selected["source_receipt_sha256"] = hash("other receipt");
        break;
    case 4:
        selected["catalog_sha256"] = hash("other catalog");
        break;
    case 5:
        selected["world_epoch"] = 4;
        break;
    case 6:
        selected["pose_revision"] = 6;
        break;
    case 7:
        row["product"]["revision"] = "2";
        break;
    case 8:
        row["cook_sha256"] = hash("other cook");
        break;
    case 9:
        row["asset_from_collider"]["translation"][0uz] = 3;
        break;
    case 10:
        row["instances"][1uz]["world_from_asset"]["translation"][0uz] = 30.000000001;
        break;
    case 11:
        row["instances"][1uz]["world_from_collider"]["translation"][0uz] = 28.000000001;
        break;
    case 12:
        row["instances"].push_back(row["instances"][0uz]);
        break;
    case 13:
        selected["queries"] = Json::array();
        break;
    case 14:
        selected["queries"].push_back(selected["queries"][0uz]);
        break;
    case 15:
        selected["queries"][0uz]["instance_id"] = "999";
        break;
    case 16:
        selected["queries"][0uz]["end"] = selected["queries"][0uz]["start"];
        break;
    case 17:
        selected["policy"]["local_vertex_units"] = -1;
        break;
    case 18:
        selected["queries"] = std::vector<Json>(257uz, selected["queries"][0uz]);
        break;
    case 19:
        row["definition"]["asset_id"] = "042";
        break;
    case 20:
        row["instances"][0uz]["world_from_asset"]["scale"] = nullptr;
        break;
    default:
        throw std::logic_error("Unknown diagnostic mutation");
    }
    const auto K_PACKAGE = tree(fixture.m_output.m_root / "package");
    CHECK(!fixture.inspect(request));
    CHECK(tree(fixture.m_output.m_root / "package") == K_PACKAGE);
}

TEST_CASE(
    "native collider diagnostic closed package replay refuses tampering and preserves callers",
    "[collider-diagnostic]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    Fixture fixture;
    const auto K_ROOT = fixture.m_output.m_root / "package";
    switch (K_CASE) {
    case 0:
        write(K_ROOT / "colliders/42/triangles.bin", "corrupt");
        break;
    case 1:
        write(K_ROOT / "extra.txt", "unlisted");
        break;
    case 2:
        std::filesystem::remove(K_ROOT / "scene.json");
        break;
    case 3:
        std::filesystem::create_directory(K_ROOT / "empty-unlisted");
        break;
    case 4:
        write(K_ROOT / "manifest.json", "{}");
        break;
    case 5:
        write(fixture.request(), "{\"schema_version\":1,\"schema_version\":1}");
        break;
    case 6:
        write(fixture.request(), std::string(1024uz * 1024uz + 1uz, ' '));
        break;
    default:
        throw std::logic_error("Unknown diagnostic package mutation");
    }
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    CHECK(!inspect_native_colliders_request(fixture.request(),
                                            hash(fixture.m_diagnostic.m_request_json)));
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("native collider diagnostic cancellation and CLI errors leave the snapshot unchanged",
          "[collider-diagnostic][collider-diagnostic-cli]") {
    Fixture fixture(true);
    const auto K_BEFORE = tree(fixture.m_output.m_root);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_STOP =
        inspect_native_colliders_request(fixture.request(), "bad", cancelled.get_token());
    REQUIRE(!K_STOP);
    CHECK(K_STOP.error().m_code == ErrorCode::CANCELLED);
    CHECK(!inspect_native_colliders(fixture.m_assets,
                                    fixture.m_package,
                                    fixture.m_input.m_expected,
                                    queries(fixture.m_assets),
                                    "package",
                                    {},
                                    cancelled.get_token()));
    std::string output;
    const auto K_LOG = [&](const std::string& line) {
        output += line;
    };
    CHECK(checked(run_process({ WE_TEST_PROCESS,
                               "inspect-colliders",
                               fixture.request().string(),
                               hash(fixture.m_diagnostic.m_request_json) },
                              {},
                              K_LOG)) == 0);
    CHECK(Json::parse(output) == Json::parse(fixture.m_diagnostic.m_report_json));
    output.clear();
    CHECK(checked(run_process(
              { WE_TEST_PROCESS, "inspect-colliders", fixture.request().string(), hash("wrong") },
              {},
              K_LOG)) == 2);
    CHECK(output.find("checksum differs") != output.npos);
    output.clear();
    CHECK(checked(run_process({ WE_TEST_PROCESS, "inspect-colliders" }, {}, K_LOG)) == 2);
    CHECK(output.find("Usage:") != output.npos);
    CHECK(tree(fixture.m_output.m_root) == K_BEFORE);
}

TEST_CASE("native collider diagnostic stays on original geometry under changed render LOD",
          "[collider-diagnostic]") {
    native_lod_test::LodFixture fixture;
    const auto K_ATTACHED = attach_collider(fixture.m_base);
    auto input = fixture.request();
    input.m_assets[0uz].m_collider = checked(inspect_native_collider(collider_request(
        fixture.m_base.m_base.m_base, fixture.m_base.m_receipt, K_ATTACHED.first)));
    for (auto& instance : input.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    const auto K_ASSETS = checked(inspect_native_assets(input));
    std::vector<std::string> reports;
    for (std::size_t level { 0uz }; level < 2uz; ++level) {
        auto request = export_request(K_ASSETS, input);
        request.m_lods = std::vector<NativeExportLodSelection> { { { 42 }, level } };
        const auto K_REPORT = checked(inspect_native_colliders(
            K_ASSETS, package(request), input.m_expected, queries(K_ASSETS), "package"));
        reports.push_back(K_REPORT.m_report_json);
    }
    const auto K_BASE = Json::parse(reports[0uz]), K_LOD = Json::parse(reports[1uz]);
    CHECK(K_BASE["shapes"] == K_LOD["shapes"]);
    CHECK(K_BASE["queries"] == K_LOD["queries"]);
    CHECK(K_BASE["instances"] == K_LOD["instances"]);
    CHECK(K_BASE["package_manifest_sha256"] != K_LOD["package_manifest_sha256"]);
    CHECK(K_BASE["shapes"][0uz]["triangles"].size() == 4uz);
}

TEST_CASE("native collider diagnostic refuses rehashed unsafe and excessive package inventories",
          "[collider-diagnostic]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    Fixture fixture;
    const auto K_PATH = fixture.m_output.m_root / "package/manifest.json";
    const auto K_BYTES = checked(asset_files::read_bytes(K_PATH, 1024uz * 1024uz, {}));
    auto manifest = Json::parse(
        std::string_view(reinterpret_cast<const char*>(K_BYTES.data()), K_BYTES.size()));
    switch (K_CASE) {
    case 0:
        manifest["files"][0uz]["path"] = "../escape.bin";
        break;
    case 1:
        manifest["files"].push_back(manifest["files"][0uz]);
        break;
    case 2:
        manifest["files"][0uz]["bytes"] = 1024uz * 1024uz + 1uz;
        break;
    case 3:
        manifest["files"] = std::vector<Json>(64uz, manifest["files"][0uz]);
        break;
    case 4:
        manifest["files"][0uz]["path"] = "a/./payload.bin";
        break;
    case 5:
        manifest["files"][0uz]["path"] = "manifest.json";
        break;
    default:
        throw std::logic_error("Unknown diagnostic inventory mutation");
    }
    const auto K_TEXT = manifest.dump();
    write(K_PATH, K_TEXT);
    auto request = Json::parse(fixture.m_diagnostic.m_request_json);
    request["selection"]["package_manifest_sha256"] = hash(K_TEXT);
    const auto K_BEFORE = tree(fixture.m_output.m_root / "package");
    CHECK(!fixture.inspect(request));
    CHECK(tree(fixture.m_output.m_root / "package") == K_BEFORE);
}

#if !defined(_WIN32)
TEST_CASE(
    "native collider diagnostic refuses selected package symlinks without following their payloads",
    "[collider-diagnostic]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    Fixture fixture;
    const auto K_ROOT = fixture.m_output.m_root / "package";
    const auto K_TARGET = K_CASE == 0   ? K_ROOT
                          : K_CASE == 1 ? K_ROOT / "colliders"
                                        : K_ROOT / "colliders/42/triangles.bin";
    const auto K_OUTSIDE = fixture.m_output.m_root / "moved";
    std::filesystem::rename(K_TARGET, K_OUTSIDE);
    if (K_CASE < 2) {
        std::filesystem::create_directory_symlink(K_OUTSIDE, K_TARGET);
    } else {
        std::filesystem::create_symlink(K_OUTSIDE, K_TARGET);
    }
    CHECK(!inspect_native_colliders_request(fixture.request(),
                                            hash(fixture.m_diagnostic.m_request_json)));
    CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(K_TARGET)));
    CHECK(std::filesystem::exists(K_OUTSIDE));
}
#endif
