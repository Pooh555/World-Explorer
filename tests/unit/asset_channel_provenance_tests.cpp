#include "../fixtures/native_assets_fixture.hpp"
#include "world_explorer/application/native_pbr_scene.hpp"
#include "world_explorer/asset_catalog/definition_io.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <cstdlib>

using namespace world_explorer;
using namespace world_explorer::native_assets_test;
using world_explorer::asset_component_test::tree;

namespace {

template <class T> const T& required(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("Required channel fixture value missing");
    }
    return *value;
}

MaterialFixtureMutations heterogeneous() {
    MaterialFixtureMutations changes;
    changes.m_provenance = [](Json& doc, MaterialChannelRole role) {
        if (role != MaterialChannelRole::NORMAL) {
            return;
        }
        auto first = doc["spans"][0uz], second = first;
        first["count"] = 1;
        first["confidence"] = .25;
        second["first"] = 1;
        second["count"] = 3;
        second["confidence"] = .75;
        if (first["class"].get<std::string>() != "MANUALLY_EDITED") {
            first["class"] = "OBSERVED";
            second["class"] = "INFERRED";
        }
        doc["spans"] = Json::array({ first, second });
    };
    return changes;
}

NativeAssetsRequest upgrade(NativeAssetsRequest request) {
    auto& asset = request.m_assets[0uz];
    asset.m_definition.m_schema_version = 2;
    std::erase_if(asset.m_provenance, [](const auto& selection) {
        return selection.m_region.m_domain.m_kind == AssetRegionKind::MATERIAL_TEXELS;
    });
    std::map<AssetProductId, AssetProvenanceSelection> channels;
    for (auto& binding : asset.m_definition.m_materials) {
        const auto K_MATERIAL = std::ranges::find_if(asset.m_materials, [&](const auto& material) {
            return material.selection().m_variant == binding.m_variant;
        });
        REQUIRE(K_MATERIAL != asset.m_materials.end());
        for (const auto& [K_ROLE, K_PIN] : K_MATERIAL->selection().m_channels) {
            binding.m_channels.push_back({ K_ROLE, K_PIN.m_product, K_PIN.m_provenance });
            channels.emplace(K_PIN.m_product.m_product,
                             AssetProvenanceSelection { K_PIN.m_provenance, K_PIN.m_region });
        }
    }
    for (const auto& [K_ID, K_CHANNEL] : channels) {
        asset.m_provenance.push_back(K_CHANNEL);
    }
    asset.m_definition.m_provenance.clear();
    for (const auto& pin : asset.m_provenance) {
        asset.m_definition.m_provenance.push_back(pin.m_product);
    }
    rebind(request);
    return request;
}

AdmittedAssetDefinition definition(const NativeAssetsRequest& request) {
    const auto& asset = request.m_assets[0uz];
    return checked(admit_asset_definition(
        asset.m_definition, required(request.m_receipt), asset.m_provenance, request.m_expected));
}

}  // namespace

TEST_CASE("channel provenance schema two preserves heterogeneous masks without a root summary",
          "[asset-channels]") {
    NativeFixture fixture(false, {}, AssetLightingMode::ESTIMATED_PBR, heterogeneous());
    auto legacy = fixture.request();
    const auto K_BEFORE = tree(fixture.m_receipt.source_directory());
    const auto K_REFUSED = inspect_native_assets(legacy);
    REQUIRE(!K_REFUSED);
    CHECK(K_REFUSED.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
    auto request = upgrade(legacy);
    const auto K_ASSETS = checked(inspect_native_assets(request));
    const auto K_DEFINITION = definition(request);
    CHECK(K_DEFINITION.definition().m_schema_version == 2);
    CHECK(K_DEFINITION.provenance().size() == 7uz);
    for (const auto& material : K_ASSETS.sources()[0uz].m_materials) {
        CHECK(material.channels().at(MaterialChannelRole::NORMAL).m_provenance.m_coverage ==
              std::array<std::uint64_t, 5> { 0, 1, 3, 0, 0 });
        CHECK(material.channels().at(MaterialChannelRole::BASE_COLOR).m_provenance.m_coverage ==
              std::array<std::uint64_t, 5> { 0, 0, 4, 0, 0 });
    }
    const auto K_JSON = Json::parse(K_DEFINITION.canonical_json());
    CHECK(K_JSON["materials"][0uz]["channels"].size() == 6uz);
    CHECK(K_JSON["provenance"].size() == 7uz);
    const auto K_DECODE =
        checked(decode_asset_definition(std::as_bytes(std::span(K_DEFINITION.canonical_json())),
                                        K_DEFINITION.reference(),
                                        fixture.m_receipt,
                                        request.m_assets[0uz].m_provenance,
                                        request.m_expected));
    CHECK(K_DECODE.canonical_json() == K_DEFINITION.canonical_json());
    for (auto& material : request.m_assets[0uz].m_definition.m_materials) {
        std::ranges::reverse(material.m_channels);
    }
    std::ranges::reverse(request.m_assets[0uz].m_definition.m_materials);
    std::ranges::reverse(request.m_assets[0uz].m_definition.m_provenance);
    std::ranges::reverse(request.m_assets[0uz].m_provenance);
    CHECK(definition(request).reference() == K_DEFINITION.reference());
    CHECK(tree(fixture.m_receipt.source_directory()) == K_BEFORE);
}

TEST_CASE("channel provenance schema two rejects missing aliased stale and foreign bindings",
          "[asset-channels]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    NativeFixture fixture(false, {}, AssetLightingMode::ESTIMATED_PBR, heterogeneous());
    auto request = upgrade(fixture.request());
    auto& asset = request.m_assets[0uz];
    auto& channels = asset.m_definition.m_materials[0uz].m_channels;
    switch (K_CASE) {
    case 0:
        channels.pop_back();
        break;
    case 1:
        channels[0uz].m_role = channels[1uz].m_role;
        break;
    case 2:
        channels[0uz].m_provenance = channels[1uz].m_provenance;
        break;
    case 3:
        ++channels[0uz].m_product.m_revision;
        break;
    case 4:
        channels[0uz].m_role = static_cast<MaterialChannelRole>(255);
        break;
    case 5:
        channels[0uz].m_product = asset.m_definition.m_geometry;
        break;
    case 6:
        asset.m_provenance.pop_back();
        break;
    case 7:
        asset.m_definition.m_schema_version = 1;
        break;
    case 8:
        channels.push_back(channels[0uz]);
        break;
    case 9:
        asset.m_provenance.resize(107uz);
        break;
    case 10:
        ++request.m_expected.m_pose_revision.m_value;
        break;
    case 11:
        asset.m_definition.m_manual_ancestor = true;
        break;
    case 12:
        std::swap(channels[1uz].m_product, channels[2uz].m_product);
        std::swap(channels[1uz].m_provenance, channels[2uz].m_provenance);
        // Both variants share channels: keep the metadata table internally
        // consistent to exercise the actual material product/role comparison.
        asset.m_definition.m_materials[1uz].m_channels = channels;
        rebind(request);
        break;
    case 13:
        channels[0uz].m_product.m_content_sha256.assign(65537uz, 'a');
        break;
    default:
        throw std::logic_error("Unexpected channel mutation");
    }
    CHECK(!inspect_native_assets(request));
    CHECK(!std::filesystem::exists(fixture.directory()));
}

TEST_CASE("channel provenance versioned byte admission rejects rehashed noncanonical records",
          "[asset-channels]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    NativeFixture fixture;
    const auto K_REQUEST = upgrade(fixture.request());
    const auto K_DEFINITION = definition(K_REQUEST);
    auto doc = Json::parse(K_DEFINITION.canonical_json());
    switch (K_CASE) {
    case 0:
        doc["schema_version"] = 1;
        break;
    case 1:
        doc["materials"][0uz]["channels"][0uz]["extra"] = 0;
        break;
    case 2:
        doc["materials"][0uz]["channels"][0uz]["role"] = "ALBEDO";
        break;
    case 3:
        doc["materials"][0uz]["channels"].erase(0uz);
        break;
    case 4:
        doc["materials"][0uz]["channels"][0uz]["product"]["revision"] = 1.5;
        break;
    case 5:
        std::swap(doc["materials"][0uz]["channels"][0uz], doc["materials"][0uz]["channels"][1uz]);
        break;
    case 6:
        doc["schema_version"] = 3;
        break;
    case 7:
        doc["materials"][0uz].erase("channels");
        break;
    default:
        throw std::logic_error("Unexpected channel JSON mutation");
    }
    auto text = doc.dump();
    auto pin = K_DEFINITION.reference();
    pin.m_sha256 = hash(text);
    CHECK(!decode_asset_definition(std::as_bytes(std::span(text)),
                                   pin,
                                   fixture.m_receipt,
                                   K_REQUEST.m_assets[0uz].m_provenance,
                                   K_REQUEST.m_expected));
}

TEST_CASE("channel provenance owned history preserves legacy revisions and interrupted publication",
          "[asset-channels]") {
    NativeFixture fixture;
    const auto K_OLD = fixture.request();
    const auto K_LEGACY = checked(inspect_native_assets(K_OLD));
    const auto K_FIRST = checked(publish_native_assets(
        K_LEGACY, fixture.m_receipt, fixture.directory(), { 1 }, 0, K_OLD.m_expected));
    auto request = upgrade(K_OLD);
    request.m_assets[0uz].m_definition.m_revision = 2;
    request.m_instances.m_revision = { 2 };
    rebind(request);
    const auto K_NEW = checked(inspect_native_assets(request));
    std::stop_source stop;
    const auto K_INTERRUPTED = publish_native_assets(K_NEW,
                                                     fixture.m_receipt,
                                                     fixture.directory(),
                                                     { 2 },
                                                     1,
                                                     request.m_expected,
                                                     stop.get_token(),
                                                     [&](std::size_t) {
                                                         stop.request_stop();
                                                     });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::CANCELLED);
    CHECK(read_native_assets(fixture.directory(), K_FIRST, K_OLD));
    const auto K_NEXT = checked(publish_native_assets(
        K_NEW, fixture.m_receipt, fixture.directory(), { 2 }, 1, request.m_expected));
    CHECK(K_NEXT.m_generation == 2);
    CHECK(read_native_assets(fixture.directory(), K_NEXT, K_OLD));
    CHECK(read_native_assets(fixture.directory(), K_NEXT, request));
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.size() == 4uz);
    CHECK(checked(publish_native_assets(
                      K_NEW, fixture.m_receipt, fixture.directory(), { 2 }, 1, request.m_expected))
              .m_manifest_sha256 == K_NEXT.m_manifest_sha256);
}

TEST_CASE("channel provenance keeps captured generated and manual classifications separate",
          "[asset-channels]") {
    const auto K_MODE =
        GENERATE(AssetLightingMode::CAPTURED_APPEARANCE, AssetLightingMode::GENERATED_PBR);
    const auto K_MANUAL = GENERATE(false, true);
    NativeFixture fixture(K_MANUAL, {}, K_MODE, heterogeneous());
    const auto K_REQUEST = upgrade(fixture.request());
    const auto K_ASSETS = checked(inspect_native_assets(K_REQUEST));
    CHECK(K_ASSETS.instances().instances().m_manual_ancestor == K_MANUAL);
    CHECK(definition(K_REQUEST).definition().m_manual_ancestor == K_MANUAL);
    for (const auto& material : K_ASSETS.sources()[0uz].m_materials) {
        for (const auto& [K_ROLE, K_CHANNEL] : material.channels()) {
            CHECK(K_CHANNEL.m_provenance.m_manual_ancestor_elements == (K_MANUAL ? 4 : 0));
        }
    }
}

TEST_CASE("channel provenance survives exact portable export and native PBR source bindings",
          "[asset-channels]") {
    NativeFixture fixture(false, {}, AssetLightingMode::ESTIMATED_PBR, heterogeneous());
    auto request = upgrade(fixture.request());
    for (auto& instance : request.m_instances.m_instances) {
        instance.m_material_variant = { 101 };
    }
    const auto K_ASSETS = checked(inspect_native_assets(request));
    const NativeExportRequest K_EXPORT { K_ASSETS, fixture.m_receipt, request.m_expected, {} };
    const auto K_FILES = checked(prepare_native_export(K_EXPORT));
    const auto K_PACKAGE = checked(inspect_native_export(K_EXPORT, K_FILES));
    for (const auto& material : request.m_assets[0uz].m_materials) {
        for (const auto& [K_ROLE, K_PIN] : material.selection().m_channels) {
            const auto K_PATH =
                "provenance/" + std::to_string(K_PIN.m_provenance.m_product.m_value) + "-1.json";
            const auto K_FILE = std::ranges::find(K_FILES, K_PATH, &ExportPackageFile::m_path);
            REQUIRE(K_FILE != K_FILES.end());
            CHECK(K_FILE->m_bytes == checked(fixture.m_receipt.copy_blob(
                                         K_PIN.m_provenance.m_content_sha256, 256uz * 1024uz)));
        }
    }
    const auto K_SCENE = checked(compile_native_pbr_scene(K_PACKAGE, request.m_expected));
    const auto& mesh = required(K_SCENE->m_chunks.begin()->second->m_mesh);
    const auto& pbr = required(mesh.m_pbr);
    CHECK(pbr.m_definition == definition(request).reference());
    CHECK(pbr.m_export_manifest_sha256 == K_PACKAGE.manifest_sha256());
    for (const auto& material : request.m_assets[0uz].m_materials) {
        for (const auto& [K_ROLE, K_PIN] : material.selection().m_channels) {
            CHECK(pbr.m_export_manifest.find(K_PIN.m_provenance.m_content_sha256) !=
                  std::string::npos);
        }
    }
    const auto K_OUTPUT = fixture.m_output.m_root / "channels.wexplore";
    REQUIRE(write_bundle(K_OUTPUT, K_SCENE));
    CHECK(scene_content_hash(*checked(load_bundle(K_OUTPUT))) == scene_content_hash(*K_SCENE));
#if defined(__linux__)
    if (auto output = std::getenv("WE_CHANNEL_SCENE_PROBE_OUTPUT")) {
        REQUIRE(write_bundle(output, K_SCENE));
    }
    if (auto output = std::getenv("WE_CHANNEL_EXPORT_PROBE_OUTPUT")) {
        REQUIRE(write_native_export(K_PACKAGE, output));
    }
#endif
}
