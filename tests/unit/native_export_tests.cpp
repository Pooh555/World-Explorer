#include "../../src/scene_store/gltf_internal.hpp"
#include "../fixtures/native_assets_fixture.hpp"
#include "world_explorer/application/native_export.hpp"
#include "world_explorer/core/asset_files.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cstdlib>
#include <limits>
#include <span>

using namespace world_explorer;
using namespace world_explorer::native_assets_test;

namespace {

const ExportPackageFile& file(const std::vector<ExportPackageFile>& files,
                              const std::string& path) {
    const auto K_FOUND = std::ranges::find(files, path, &ExportPackageFile::m_path);
    REQUIRE(K_FOUND != files.end());
    return *K_FOUND;
}

Json json(const std::vector<ExportPackageFile>& files, const std::string& path) {
    const auto& data = file(files, path).m_bytes;
    return Json::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

std::uint32_t word(const std::vector<std::byte>& bytes, std::size_t offset) {
    REQUIRE(offset + 4uz <= bytes.size());
    std::uint32_t value {};
    for (std::size_t index { 0uz }; index < 4uz; ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index]) << (8uz * index);
    }
    return value;
}

MaterialFixtureMutations pattern() {
    MaterialFixtureMutations result;
    result.m_channel = [](std::string& data, MaterialChannelRole role) {
        const std::array<std::array<float, 3>, 4> K_RGB {
            { { 0, .0031308F, .18F }, { .5F, 1, .04045F }, { .01F, .1F, .25F }, { .9F, .8F, .7F } } };
        const std::array<std::array<float, 3>, 4> K_NORMAL {
            { { 0, 0, 1 }, { .6F, 0, .8F }, { 0, -.6F, .8F }, { .70710677F, 0, .70710677F } } };
        const std::array<float, 4> K_METALLIC { 0, .25F, .5F, 1 }, K_ROUGHNESS { 1, .5F, .25F, 0 };
        const bool K_RGB_ROLE = role == MaterialChannelRole::BASE_COLOR ||
                                role == MaterialChannelRole::NORMAL ||
                                role == MaterialChannelRole::EMISSION;
        for (std::size_t pixel { 0uz }; pixel < 4uz; ++pixel) {
            for (std::size_t channel { 0uz }; channel < (K_RGB_ROLE ? 3uz : 1uz); ++channel) {
                float value {};
                switch (role) {
                case MaterialChannelRole::BASE_COLOR:
                    value = K_RGB[pixel][channel];
                    break;
                case MaterialChannelRole::EMISSION:
                    value = K_RGB[pixel][channel] / 8;
                    break;
                case MaterialChannelRole::NORMAL:
                    value = K_NORMAL[pixel][channel];
                    break;
                case MaterialChannelRole::METALLIC:
                    value = K_METALLIC[pixel];
                    break;
                case MaterialChannelRole::ROUGHNESS:
                    value = K_ROUGHNESS[pixel];
                    break;
                case MaterialChannelRole::OCCLUSION:
                    value = static_cast<float>(pixel + 1uz) / 10;
                    break;
                default:
                    throw std::logic_error("Unexpected pattern role");
                }
                put(data,
                    64uz + (pixel * (K_RGB_ROLE ? 3uz : 1uz) + channel) * 4uz,
                    std::bit_cast<std::uint32_t>(value),
                    4uz);
            }
        }
    };
    return result;
}

NativeAssetsRequest export_source(const NativeFixture& fixture) {
    auto native = fixture.request();
    // This authored exporter fixture chooses the original material selection
    // for all copies. A distinct selection is tested as unsupported below.
    for (auto& instance : native.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    return native;
}

NativeExportRequest request(const NativeFixture& fixture) {
    return { checked(inspect_native_assets(export_source(fixture))),
            fixture.m_receipt,
            fixture.m_receipt.graph().m_context,
            {} };
}

Json resolved_material(Json doc, std::size_t material, const std::string& prefix) {
    auto result = doc["materials"][material];
    const auto K_RESOLVE = [&](Json& texture) {
        const auto K_TEXTURE = texture["index"].get<std::size_t>();
        const auto& row = doc["textures"][K_TEXTURE];
        texture = {
            { "image",
             prefix + doc["images"][row["source"].get<std::size_t>()]["uri"].get<std::string>() },
            { "sampler", doc["samplers"][row["sampler"].get<std::size_t>()] } };
    };
    K_RESOLVE(result["pbrMetallicRoughness"]["baseColorTexture"]);
    K_RESOLVE(result["pbrMetallicRoughness"]["metallicRoughnessTexture"]);
    for (auto key : { "normalTexture", "occlusionTexture", "emissiveTexture" }) {
        K_RESOLVE(result[key]);
    }
    return result;
}

}  // namespace

TEST_CASE("native texture export converts actual channels and shares resources between assets and "
          "assembly",
          "[native-export]") {
    NativeFixture fixture(false, {}, AssetLightingMode::ESTIMATED_PBR, pattern());
    const auto K_REQUEST = request(fixture);
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, K_FILES));
    const auto K_MANIFEST = json(K_FILES, "manifest.json");
    CHECK(K_MANIFEST["qualification"] == "UNQUALIFIED_EXPORT");
    CHECK(K_MANIFEST["synthetic_diagnostic"] == true);
    CHECK(K_MANIFEST["source_images_added"] == false);
    CHECK(K_MANIFEST["validator"] == "NOT_RUN");
    const auto K_ASSET = json(K_FILES, "assets/42/asset.gltf");
    const auto K_SCENE = json(K_FILES, "environment.gltf");
    CHECK(K_ASSET["meshes"][0uz]["primitives"].size() == 2uz);
    CHECK(K_SCENE["meshes"].size() == 1uz);
    CHECK(K_SCENE["nodes"].size() == 2uz);
    CHECK(K_SCENE["nodes"][0uz]["mesh"] == K_SCENE["nodes"][1uz]["mesh"]);
    CHECK("assets/42/" + K_ASSET["buffers"][0uz]["uri"].get<std::string>() ==
          K_SCENE["buffers"][0uz]["uri"].get<std::string>());
    for (std::size_t material { 0uz }; material < 2uz; ++material) {
        CHECK(resolved_material(K_ASSET, material, "assets/42/") ==
              resolved_material(K_SCENE, material, ""));
    }
    const auto& geometry = file(K_FILES, "assets/42/geometry.bin").m_bytes;
    CHECK(std::bit_cast<float>(word(geometry, 0uz)) == -1);
    CHECK(std::bit_cast<float>(word(geometry, 12uz + 8uz)) == 1);
    CHECK(std::bit_cast<float>(word(geometry, 24uz)) == 1);
    CHECK(std::bit_cast<float>(word(geometry, 36uz)) == 1);
    CHECK(word(geometry, 4uz * 48uz) == 0);
    CHECK(word(geometry, 4uz * 48uz + 12uz) == 1);
    const auto K_BASE =
        gltf_detail::decode_png(file(K_FILES, "assets/42/material-101-base.png").m_bytes, {});
    CHECK(K_BASE.m_width == 2);
    CHECK(K_BASE.m_height == 2);
    CHECK(K_BASE.m_rgba[0uz] == std::byte { 0 });
    CHECK(K_BASE.m_rgba[2uz] == std::byte { 118 });
    CHECK(K_BASE.m_rgba[4uz] == std::byte { 188 });
    CHECK(K_BASE.m_rgba[5uz] == std::byte { 255 });
    const auto K_ORM =
        gltf_detail::decode_png(file(K_FILES, "assets/42/material-101-orm.png").m_bytes, {});
    CHECK(K_ORM.m_rgba[0uz] == std::byte { 26 });
    CHECK(K_ORM.m_rgba[1uz] == std::byte { 255 });
    CHECK(K_ORM.m_rgba[2uz] == std::byte { 0 });
    CHECK(K_ORM.m_rgba[5uz] == std::byte { 128 });
    CHECK(K_ORM.m_rgba[6uz] == std::byte { 64 });
    const auto K_NORMAL =
        gltf_detail::decode_png(file(K_FILES, "assets/42/material-101-normal.png").m_bytes, {});
    CHECK(K_NORMAL.m_rgba[4uz] == std::byte { 204 });
    CHECK(K_NORMAL.m_rgba[9uz] == std::byte { 51 });
    for (const auto& row : K_MANIFEST["files"]) {
        CHECK(sha256(file(K_FILES, row["path"].get<std::string>()).m_bytes) ==
              row["sha256"].get<std::string>());
    }
    CHECK(K_PACKAGE.files().size() == K_FILES.size());
#if defined(__linux__)
    if (auto output = std::getenv("WE_NATIVE_EXPORT_PROBE_OUTPUT")) {
        REQUIRE(write_native_export(K_PACKAGE, output));
    }
#endif
}

TEST_CASE("native export rejects changed canonical packages and unsafe file sets",
          "[native-export]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    NativeFixture fixture;
    const auto K_REQUEST = request(fixture);
    auto files = checked(prepare_native_export(K_REQUEST));
    switch (K_CASE) {
    case 0:
        files.pop_back();
        break;
    case 1:
        files.push_back(files[0uz]);
        break;
    case 2:
        files[0uz].m_path = "../escape";
        break;
    case 3:
        files[0uz].m_bytes[0uz] ^= std::byte { 1 };
        break;
    case 4:
        files[0uz].m_bytes.clear();
        break;
    case 5:
        files[0uz].m_bytes.resize(1024uz * 1024uz + 1uz);
        break;
    case 6:
        files.resize(65uz);
        break;
    default:
        throw std::logic_error("Unexpected package case");
    }
    CHECK(!inspect_native_export(K_REQUEST, files));
}

TEST_CASE("native export refuses foreign stale hidden unsupported and excessive-error requests",
          "[native-export]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    NativeFixture fixture;
    auto input = request(fixture);
    switch (K_CASE) {
    case 0:
        input.m_assets.reset();
        break;
    case 1:
        input.m_receipt.reset();
        break;
    case 2:
        ++input.m_expected.m_pose_revision.m_value;
        break;
    case 3:
        input.m_policy.m_numeric = 0;
        break;
    case 4:
        input.m_policy.m_normal_map = 0;
        break;
    case 5:
        input.m_policy.m_linear_color = std::numeric_limits<double>::quiet_NaN();
        break;
    case 6:
        input.m_policy.m_position_units = -1;
        break;
    case 7: {
        MaterialFixture foreign;
        input.m_receipt = foreign.m_receipt;
        break;
    }
    default:
        throw std::logic_error("Unexpected request case");
    }
    CHECK(!prepare_native_export(input));
}

TEST_CASE("native PBR exporter refuses captured radiance and HDR emission", "[native-export]") {
    const bool K_CAPTURED = GENERATE(true, false);
    auto changes = pattern();
    if (!K_CAPTURED) {
        changes.m_channel = [](std::string& data, auto role) {
            if (role == MaterialChannelRole::EMISSION) {
                put(data, 64uz, std::bit_cast<std::uint32_t>(2.0F), 4uz);
            }
        };
    } else {
        changes = {};
    }
    NativeFixture fixture(false,
                          {},
                          K_CAPTURED ? AssetLightingMode::CAPTURED_APPEARANCE
                                     : AssetLightingMode::ESTIMATED_PBR,
                          changes);
    const auto K_RESULT = prepare_native_export(request(fixture));
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}

TEST_CASE("native export preserves manual ancestry and rejects cancellation before writes",
          "[native-export]") {
    NativeFixture fixture(true);
    const auto K_REQUEST = request(fixture);
    const auto K_FILES = checked(prepare_native_export(K_REQUEST));
    CHECK(json(K_FILES, "manifest.json")["manual_ancestor"] == true);
    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK(!prepare_native_export(K_REQUEST, cancellation.get_token()));
    CHECK(!inspect_native_export(K_REQUEST, K_FILES, cancellation.get_token()));
}

TEST_CASE("native export owned publication replays and recovers cancelled closures without "
          "touching inputs",
          "[native-export]") {
    NativeFixture fixture;
    const auto K_REQUEST = request(fixture);
    auto files = checked(prepare_native_export(K_REQUEST));
    std::ranges::reverse(files);
    const auto K_PACKAGE = checked(inspect_native_export(K_REQUEST, files));
    const auto K_DIRECTORY = fixture.m_output.m_root / "export-store";
    std::stop_source cancellation;
    const auto K_FIRST = publish_native_export(K_PACKAGE,
                                               fixture.m_receipt,
                                               K_DIRECTORY,
                                               { 1 },
                                               0,
                                               K_REQUEST.m_expected,
                                               cancellation.get_token(),
                                               [&](std::size_t) {
                                                   cancellation.request_stop();
                                               });
    REQUIRE(!K_FIRST);
    CHECK(K_FIRST.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(K_DIRECTORY)).m_records.empty());
    const auto K_COMMIT = checked(publish_native_export(
        K_PACKAGE, fixture.m_receipt, K_DIRECTORY, { 1 }, 0, K_REQUEST.m_expected));
    const auto K_REPLAY = checked(publish_native_export(K_PACKAGE,
                                                        fixture.m_receipt,
                                                        K_DIRECTORY,
                                                        { 1 },
                                                        0,
                                                        K_REQUEST.m_expected,
                                                        {},
                                                        [](std::size_t) {
                                                            FAIL("Replay cannot write");
                                                        }));
    CHECK(K_REPLAY.m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_native_export(K_DIRECTORY, K_COMMIT, K_REQUEST, files));
    auto wrong = K_COMMIT;
    ++wrong.m_generation;
    CHECK(!read_native_export(K_DIRECTORY, wrong, K_REQUEST, files));
    CHECK(!publish_native_export(K_PACKAGE,
                                 fixture.m_receipt,
                                 fixture.m_receipt.source_directory(),
                                 { 2 },
                                 0,
                                 K_REQUEST.m_expected));
    const auto K_OUTPUT = fixture.m_output.m_root / "portable";
    REQUIRE(write_native_export(K_PACKAGE, K_OUTPUT));
    CHECK(!write_native_export(K_PACKAGE, K_OUTPUT));
    CHECK(!write_native_export(K_PACKAGE, fixture.m_receipt.source_directory() / "inside"));
    for (const auto& entry : K_PACKAGE.files()) {
        CHECK(checked(asset_files::read_bytes(K_OUTPUT / entry.m_path, 1024uz * 1024uz, {})) ==
              entry.m_bytes);
    }
}

TEST_CASE(
    "native export offsets resources for distinct definitions without forking individual buffers",
    "[native-export]") {
    NativeFixture fixture;
    auto native = export_source(fixture);
    auto second = native.m_assets[0uz];
    second.m_definition.m_asset = { 43 };
    second.m_source_instance = { 3 };
    const auto K_DEFINITION = checked(admit_asset_definition(
        second.m_definition, fixture.m_receipt, second.m_provenance, native.m_expected));
    auto instance = native.m_instances.m_instances[0uz];
    instance.m_definition = K_DEFINITION.reference();
    instance.m_instance = { 3 };
    native.m_assets.push_back(second);
    native.m_instances.m_instances.push_back(instance);
    NativeExportRequest input {
        checked(inspect_native_assets(native)), fixture.m_receipt, native.m_expected, {} };
    const auto K_FILES = checked(prepare_native_export(input));
    const auto K_ASSEMBLY = json(K_FILES, "environment.gltf");
    const auto K_INDIVIDUAL = json(K_FILES, "assets/43/asset.gltf");
    CHECK(K_ASSEMBLY["meshes"].size() == 2uz);
    CHECK(K_ASSEMBLY["buffers"].size() == 2uz);
    CHECK(K_ASSEMBLY["nodes"][2uz]["mesh"] == 1);
    CHECK(K_ASSEMBLY["buffers"][1uz]["uri"] == "assets/43/geometry.bin");
    CHECK(K_ASSEMBLY["bufferViews"][3uz]["buffer"] == 1);
    CHECK(K_ASSEMBLY["accessors"][6uz]["bufferView"] == 3);
    CHECK(K_ASSEMBLY["meshes"][1uz]["primitives"][0uz]["attributes"]["POSITION"] == 6);
    for (std::size_t material { 0uz }; material < 2uz; ++material) {
        CHECK(resolved_material(K_INDIVIDUAL, material, "assets/43/") ==
              resolved_material(K_ASSEMBLY, material + 2uz, ""));
    }
    CHECK(file(K_FILES, "assets/42/geometry.bin").m_bytes ==
          file(K_FILES, "assets/43/geometry.bin").m_bytes);
    const auto K_REPEAT = checked(prepare_native_export(input));
    CHECK(checked(inspect_native_export(input, K_REPEAT)).manifest_sha256() ==
          checked(inspect_native_export(input, K_FILES)).manifest_sha256());
    second.m_definition.m_asset = { 44 };
    second.m_source_instance = { 4 };
    const auto K_THIRD = checked(admit_asset_definition(
        second.m_definition, fixture.m_receipt, second.m_provenance, native.m_expected));
    instance.m_instance = { 4 };
    instance.m_definition = K_THIRD.reference();
    native.m_assets.push_back(second);
    native.m_instances.m_instances.push_back(instance);
    input.m_assets = checked(inspect_native_assets(native));
    CHECK(!prepare_native_export(input));
}

TEST_CASE("native export enforces original world error policy and does not invent hidden geometry "
          "visibility",
          "[native-export]") {
    NativeFixture fixture;
    auto native = export_source(fixture);
    auto& definition = native.m_assets[0uz].m_definition;
    definition.m_pivot.m_asset_from_geometry.m_translation.m_x = -1.1;
    definition.m_bounds.m_minimum.m_x = -1.1;
    definition.m_bounds.m_maximum.m_x = 2 - 1.1;
    rebind(native);
    native.m_instances.m_instances[0uz].m_world_from_asset.m_translation.m_x = 12.2;
    NativeExportRequest input {
        checked(inspect_native_assets(native)), fixture.m_receipt, native.m_expected, {} };
    const auto K_FILES = checked(prepare_native_export(input));
    CHECK(json(K_FILES, "manifest.json")["assets"][0uz]["errors"]["position_units"].get<double>() >
          0);
    input.m_policy.m_position_units = 0;
    CHECK(!prepare_native_export(input));
    input.m_policy = {};
    native.m_instances.m_instances[1uz].m_visible = false;
    input.m_assets = checked(inspect_native_assets(native));
    CHECK(!prepare_native_export(input));
}

TEST_CASE(
    "native export rejects negative tangent-space Z instead of flipping a material convention",
    "[native-export]") {
    MaterialFixtureMutations changes;
    changes.m_channel = [](std::string& data, auto role) {
        if (role == MaterialChannelRole::NORMAL) {
            put(data, 64uz + 8uz, std::bit_cast<std::uint32_t>(-1.0F), 4uz);
        }
    };
    NativeFixture fixture(false, {}, AssetLightingMode::ESTIMATED_PBR, changes);
    const auto K_RESULT = prepare_native_export(request(fixture));
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}

TEST_CASE("native export refuses alternate per-instance material selections", "[native-export]") {
    NativeFixture fixture;
    NativeExportRequest input { checked(inspect_native_assets(fixture.request())),
                               fixture.m_receipt,
                               fixture.m_receipt.graph().m_context,
                               {} };
    const auto K_RESULT = prepare_native_export(input);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}
