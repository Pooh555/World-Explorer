#include "../fixtures/material_product_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::material_product_test;

TEST_CASE("material products own six linear channels complete provenance and explicit fit inputs",
          "[material-product]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedMaterialProduct, AssetDefinition>);
    MaterialFixture fixture({ .m_white_balance = true });
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->geometry_product() == fixture.m_geometry.selection().m_product);
    CHECK(fixture.m_geometry.capture_product() == fixture.m_capture.record().m_selection.m_product);
    CHECK(K_RESULT->channels().size() == 6uz);
    CHECK(K_RESULT->fit_inputs().size() == 3uz);
    CHECK(K_RESULT->fit_inputs()[1uz].m_values == std::vector<double> { 1.2 });
    CHECK(K_RESULT->synthetic_source());
    CHECK(!K_RESULT->manual_ancestor());
    CHECK(K_RESULT->context().m_world_epoch == WorldEpoch { 3 });
    CHECK(K_RESULT->receipt_sha256() == fixture.m_receipt.receipt_sha256());
    const auto& channel = K_RESULT->channels().at(MaterialChannelRole::BASE_COLOR);
    CHECK(channel.m_components == 3);
    CHECK(channel.m_values == std::vector<float>(12uz, .25F));
    CHECK(channel.m_provenance.m_coverage == std::array<std::uint64_t, 5> { 0, 0, 4, 0, 0 });
    fixture.m_selected.m_channels.clear();
    fixture.m_record.clear();
    CHECK(K_RESULT->selection().m_channels.size() == 6uz);
}

TEST_CASE(
    "captured material preserves unlit radiance while generated material retains edited ancestry",
    "[material-product]") {
    const bool K_CAPTURED = GENERATE(true, false);
    MaterialFixture fixture({ .m_lighting = K_CAPTURED ? AssetLightingMode::CAPTURED_APPEARANCE
                                                      : AssetLightingMode::GENERATED_PBR,
                             .m_manual = !K_CAPTURED });
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->channels().size() == (K_CAPTURED ? 1uz : 6uz));
    CHECK(K_RESULT->fit_inputs().empty());
    CHECK(K_RESULT->manual_ancestor() == !K_CAPTURED);
    const auto& channel = K_RESULT->channels().begin()->second;
    CHECK(channel.m_provenance.m_coverage[K_CAPTURED ? 1uz : 4uz] == 4);
}

TEST_CASE("material products reject rehashed root profile changes and unsupported claims",
          "[material-product]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21);
    MaterialFixtureMutations changes;
    changes.m_root = [&](Json& doc, auto&) {
        switch (K_CASE) {
        case 0:
            doc["brdf"] = "CAPTURED_UNLIT";
            break;
        case 1:
            doc["relighting_scope"] = "QUALIFIED";
            break;
        case 2:
            doc["normal"]["y"] = "NEGATIVE_BITANGENT";
            break;
        case 3:
            doc["normal"]["basis"] = "WORLD";
            break;
        case 4:
            doc["channels"]["BASE_COLOR"]["color_space"] = "SRGB";
            break;
        case 5:
            doc["channels"]["NORMAL"]["color_space"] = "LINEAR_SRGB";
            break;
        case 6:
            doc["uv_set"] = 1;
            break;
        case 7:
            doc["uv_origin"] = "BOTTOM_LEFT";
            break;
        case 8:
            doc["sampler"]["mip"] = "AUTO";
            break;
        case 9:
            doc["sampler"]["filter"] = "NEAREST";
            break;
        case 10:
            doc["lighting"] = "GENERATED_PBR";
            break;
        case 11:
            doc["geometry"]["revision"] = 2;
            break;
        case 12:
            doc["capture"]["revision"] = 2;
            break;
        case 13:
            doc["grid"]["width"] = 1;
            break;
        case 14:
            doc["variant_id"] = 102;
            break;
        case 15:
            doc["recipe_sha256"] = hash("foreign recipe");
            break;
        case 16:
            doc["manual_ancestor"] = true;
            break;
        case 17:
            doc["unknown"] = 0;
            break;
        case 18:
            doc["fit_inputs"][1uz] = doc["fit_inputs"][0uz];
            break;
        case 19:
            doc["fit_inputs"][0uz]["role"] = "UNKNOWN";
            break;
        case 20:
            doc["world_epoch"] = 4;
            break;
        case 21:
            doc["schema_version"] = 2;
            break;
        default:
            throw std::logic_error("Unexpected material test case");
        }
    };
    MaterialFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("material raw channels reject rehashed corrupt identities lengths and nonphysical values",
          "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    MaterialFixtureMutations changes;
    changes.m_channel = [&](std::string& bytes, auto role) {
        if (role !=
            (K_CASE == 16 ? MaterialChannelRole::NORMAL : MaterialChannelRole::BASE_COLOR)) {
            return;
        }
        switch (K_CASE) {
        case 0:
            bytes[0uz] = 'X';
            break;
        case 1:
            put(bytes, 8uz, 2, 4uz);
            break;
        case 2:
            put(bytes, 12uz, 0, 4uz);
            break;
        case 3:
            put(bytes, 16uz, 1, 4uz);
            break;
        case 4:
            put(bytes, 20uz, 1, 4uz);
            break;
        case 5:
            put(bytes, 24uz, 1, 4uz);
            break;
        case 6:
            put(bytes, 28uz, 1, 4uz);
            break;
        case 7:
            put(bytes, 32uz, 999, 8uz);
            break;
        case 8:
            put(bytes, 40uz, 2, 8uz);
            break;
        case 9:
            put(bytes, 48uz, 4, 8uz);
            break;
        case 10:
            put(bytes, 56uz, 6, 8uz);
            break;
        case 11:
            bytes.resize(63uz);
            break;
        case 12:
            bytes.push_back('\0');
            break;
        case 13:
            put(bytes,
                64uz,
                std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()),
                4uz);
            break;
        case 14:
            put(bytes, 64uz, std::bit_cast<std::uint32_t>(1.1F), 4uz);
            break;
        case 15:
            put(bytes, 64uz, std::bit_cast<std::uint32_t>(-.1F), 4uz);
            break;
        case 16:
            put(bytes, 72uz, 0, 4uz);
            break;
        default:
            throw std::logic_error("Unexpected material test case");
        }
    };
    MaterialFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "material texel partitions reject gaps wrong sources and hidden generated classifications",
    "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    MaterialFixtureMutations changes;
    changes.m_provenance = [&](Json& doc, auto role) {
        if (role != MaterialChannelRole::BASE_COLOR) {
            return;
        }
        switch (K_CASE) {
        case 0:
            doc["spans"][0uz]["count"] = 3;
            break;
        case 1:
            doc["spans"][0uz]["first"] = 1;
            break;
        case 2:
            doc["spans"][0uz]["class"] = "GENERATED";
            break;
        case 3:
            doc["domain"]["width"] = 1;
            break;
        case 4:
            doc["sources"][0uz]["pose_sha256"] = hash("foreign pose");
            break;
        case 5:
            doc["spans"][0uz]["confidence"] = 1.1;
            break;
        case 6:
            doc["spans"][0uz]["class"] = "MANUALLY_EDITED";
            break;
        case 7:
            doc["spans"][0uz]["support"] = Json::array();
            doc["spans"][0uz]["class"] = "OBSERVED";
            break;
        default:
            throw std::logic_error("Unexpected material test case");
        }
    };
    MaterialFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "material fit records reject foreign views roles models ranges and extra evaluation policy",
    "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    MaterialFixtureMutations changes;
    changes.m_fit = [&](Json& doc, auto& pin) {
        if (pin.m_role != MaterialFitRole::EXPOSURE) {
            return;
        }
        switch (K_CASE) {
        case 0:
            doc["views"][0uz]["frame_id"] = 7;
            break;
        case 1:
            pin.m_views[0uz].m_frame = { 7 };
            doc["views"][0uz]["frame_id"] = 7;
            break;
        case 2:
            doc["role"] = "LIGHT";
            break;
        case 3:
            doc["parameters"]["model"] = "AUTO_EXPOSURE";
            break;
        case 4:
            doc["parameters"]["gain"] = 0;
            break;
        case 5:
            doc["parameters"]["gain"] = 65;
            break;
        case 6:
            doc["parameters"]["gain"] = true;
            break;
        case 7:
            doc["evaluation_threshold"] = 23;
            break;
        case 8:
            doc["geometry"]["revision"] = 2;
            break;
        case 9:
            doc["views"].push_back(doc["views"][0uz]);
            pin.m_views.push_back(pin.m_views[0uz]);
            break;
        default:
            throw std::logic_error("Unexpected material test case");
        }
    };
    MaterialFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "material selection cannot replace independent geometry capture receipt or required fit roles",
    "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    MaterialFixture fixture;
    auto request = fixture.request();
    if (!request.m_selection) {
        throw std::logic_error("Missing material selection");
    }
    auto& selected = request.m_selection.value();
    switch (K_CASE) {
    case 0:
        request.m_geometry.reset();
        break;
    case 1:
        request.m_capture.reset();
        break;
    case 2:
        request.m_receipt.reset();
        break;
    case 3:
        request.m_selection.reset();
        break;
    case 4:
        request.m_expected.m_pose_revision = { 6 };
        break;
    case 5:
        selected.m_variant = { 999 };
        break;
    case 6:
        selected.m_fit_inputs.erase(selected.m_fit_inputs.begin());
        break;
    case 7:
        selected.m_fit_inputs.pop_back();
        break;
    case 8:
        selected.m_fit_inputs.push_back(selected.m_fit_inputs[0uz]);
        break;
    case 9:
        selected.m_channels.begin()->second.m_region.m_sources[0uz].m_pose_sha256 =
            hash("foreign native pose");
        break;
    case 10:
        request.m_geometry = checked(fixture.m_base.inspect());
        break;
    case 11:
        request.m_capture = fixture.m_base.m_capture;
        break;
    default:
        throw std::logic_error("Unexpected material test case");
    }
    CHECK(!inspect_material_product(request));
}

TEST_CASE("material strict JSON rejects duplicate deep oversized and NUL root or fit records",
          "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    const bool K_FIT = GENERATE(true, false);
    MaterialFixtureMutations changes;
    auto mutation = [&](std::string& text) {
        if (K_CASE == 0) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 1) {
            text = std::string(17uz, '[') + "0" + std::string(17uz, ']');
        }
        if (K_CASE == 2) {
            text.resize(64uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 3) {
            text.push_back('\0');
        }
    };
    if (K_FIT) {
        changes.m_fit_bytes = mutation;
    } else {
        changes.m_root_bytes = mutation;
    }
    MaterialFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("material admission binds exact dependency closure including inference firewall",
          "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    MaterialFixtureMutations changes;
    changes.m_graph = [&](auto& graph, const auto& selected) {
        for (auto& node : graph.m_products) {
            if (K_CASE == 0 && node.m_product == selected.m_product) {
                node.m_dependencies.pop_back();
            }
            if (K_CASE == 1 && node.m_product == selected.m_channels.begin()->second.m_product) {
                node.m_dependencies.clear();
            }
            if (K_CASE == 2 && node.m_product == selected.m_fit_inputs[0uz].m_product) {
                node.m_dependencies.pop_back();
            }
            if (K_CASE == 3 && node.m_product == selected.m_product) {
                const auto K_VIEW = std::ranges::find_if(graph.m_products, [](const auto& entry) {
                    return entry.m_product.m_product == AssetProductId { 1 };
                });
                if (K_VIEW == graph.m_products.end()) {
                    throw std::logic_error("Missing held-out image");
                }
                node.m_dependencies.push_back(K_VIEW->m_product);
            }
        }
    };
    MaterialFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("material publication cancels without visible partial closure and exact retries reopen "
          "all records",
          "[material-product]") {
    const auto K_STOP_AFTER = GENERATE(1uz, 7uz, 15uz);
    MaterialFixture fixture;
    const auto K_PRODUCT = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_material_product(K_PRODUCT,
                                                   fixture.m_receipt,
                                                   fixture.directory(),
                                                   { 231 },
                                                   0,
                                                   K_CONTEXT,
                                                   stop.get_token(),
                                                   [&](std::size_t count) {
                                                       if (count == K_STOP_AFTER) {
                                                           stop.request_stop();
                                                       }
                                                   });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_material_product(
        K_PRODUCT, fixture.m_receipt, fixture.directory(), { 231 }, 0, K_CONTEXT));
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.size() == 15uz);
    CHECK(checked(publish_material_product(
                      K_PRODUCT, fixture.m_receipt, fixture.directory(), { 231 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_material_product(fixture.directory(), K_COMMIT, fixture.request()));
    auto stale = K_COMMIT;
    ++stale.m_generation;
    CHECK(!read_material_product(fixture.directory(), stale, fixture.request()));
    CHECK(!publish_material_product(
        K_PRODUCT, fixture.m_receipt, fixture.directory(), { 232 }, 0, K_CONTEXT));
    CHECK(!publish_material_product(
        K_PRODUCT, fixture.m_receipt, fixture.m_receipt.source_directory(), { 231 }, 0, K_CONTEXT));
}

TEST_CASE("material reload requires every committed binary sidecar and fit independently of root",
          "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    MaterialFixture fixture;
    const auto K_PRODUCT = checked(fixture.inspect());
    const auto K_COMMIT = checked(publish_material_product(K_PRODUCT,
                                                           fixture.m_receipt,
                                                           fixture.directory(),
                                                           { 233 },
                                                           0,
                                                           fixture.m_receipt.graph().m_context));
    const auto K_FULL = checked(read_immutable_records(fixture.directory()));
    std::vector<ImmutableRecord> records;
    const auto K_PREFIX = K_CASE == 0   ? "material-channel-"
                          : K_CASE == 1 ? "material-region-"
                                        : "material-fit-";
    bool removed {};
    for (const auto& [K_KEY, K_BYTES] : K_FULL.m_records) {
        if (!removed && K_KEY.starts_with(K_PREFIX)) {
            removed = true;
            continue;
        }
        records.push_back({ K_KEY, K_BYTES });
    }
    REQUIRE(removed);
    const auto K_PARTIAL = fixture.directory().parent_path() / "partial";
    const auto K_OTHER = checked(publish_immutable_records(K_PARTIAL, { 234 }, 0, records, {}));
    CHECK(!read_material_product(K_PARTIAL, K_OTHER, fixture.request()));
    CHECK(K_COMMIT.m_generation == 1);
}

TEST_CASE("material bounded maximum grids publish and one-past limits reject",
          "[material-product]") {
    const auto K_SIZE = GENERATE(0U, 256U, 257U);
    MaterialFixture fixture({ .m_width = K_SIZE, .m_height = K_SIZE });
    const auto K_RESULT = fixture.inspect();
    if (K_SIZE != 256) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
        return;
    }
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->channels().at(MaterialChannelRole::NORMAL).m_values.size() ==
          256uz * 256uz * 3uz);
    const auto K_COMMIT = checked(publish_material_product(*K_RESULT,
                                                           fixture.m_receipt,
                                                           fixture.directory(),
                                                           { 235 },
                                                           0,
                                                           fixture.m_receipt.graph().m_context));
    CHECK(read_material_product(fixture.directory(), K_COMMIT, fixture.request()));
}

TEST_CASE("material owns values after source lifetime and pre cancellation returns no handle",
          "[material-product]") {
    const auto K_PRODUCT = [] {
        MaterialFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        return checked(fixture.inspect());
    }();
    CHECK(K_PRODUCT.channels().at(MaterialChannelRole::NORMAL).m_values ==
          std::vector<float> { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 });
    CHECK(!inspect_material_product({}));
}

TEST_CASE(
    "generated material preserves complete mixed observed inferred generated and unknown texels",
    "[material-product]") {
    MaterialFixtureMutations changes;
    changes.m_provenance = [](Json& doc, auto) {
        const auto K_SPAN = doc["spans"][0uz];
        doc["spans"] = Json::array();
        const std::array K_CLASSES { "OBSERVED", "INFERRED", "GENERATED", "UNKNOWN" };
        for (std::size_t index { 0uz }; index < K_CLASSES.size(); ++index) {
            auto row = K_SPAN;
            row["first"] = index;
            row["count"] = 1;
            row["class"] = K_CLASSES[index];
            if (index == 3uz) {
                row["confidence"] = nullptr;
                row["support"] = Json::array();
            }
            doc["spans"].push_back(row);
        }
    };
    MaterialFixture fixture({ .m_lighting = AssetLightingMode::GENERATED_PBR }, changes);
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    for (const auto& [K_ROLE, K_CHANNEL] : K_RESULT->channels()) {
        CHECK(K_CHANNEL.m_provenance.m_coverage == std::array<std::uint64_t, 5> { 1, 1, 1, 1, 0 });
    }
}

TEST_CASE("captured material rejects inferred texels and PBR conversion declarations",
          "[material-product]") {
    const bool K_PROVENANCE = GENERATE(true, false);
    MaterialFixtureMutations changes;
    if (K_PROVENANCE) {
        changes.m_provenance = [](Json& doc, auto) {
            doc["spans"][0uz]["class"] = "INFERRED";
        };
    } else {
        changes.m_root = [](Json& doc, auto&) {
            doc["brdf"] = "METALLIC_ROUGHNESS_GGX";
        };
    }
    MaterialFixture fixture({ .m_lighting = AssetLightingMode::CAPTURED_APPEARANCE }, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("material RGB light and white balance ranges reject rehashed invalid values",
          "[material-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    MaterialFixtureMutations changes;
    changes.m_fit = [&](Json& doc, auto& selected) {
        if (selected.m_role == MaterialFitRole::LIGHT && K_CASE < 2) {
            doc["parameters"]["linear_rgb"][0uz] = K_CASE == 0 ? -.1 : 65;
        }
        if (selected.m_role == MaterialFitRole::WHITE_BALANCE && K_CASE >= 2) {
            doc["parameters"]["linear_rgb"][0uz] = K_CASE == 2 ? 0 : 65;
        }
    };
    MaterialFixture fixture({ .m_white_balance = true }, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("material geometry requires actual UV and tangent arrays", "[material-product]") {
    MaterialFixture fixture({}, {}, [](Json& doc, auto&) {
        doc["mesh"]["uv"] = nullptr;
        doc["mesh"]["tangents"] = nullptr;
    });
    CHECK(!fixture.inspect());
}
