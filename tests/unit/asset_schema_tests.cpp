#include "../fixtures/asset_schema_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>

using namespace world_explorer;
using namespace world_explorer::asset_schema_test;
using world_explorer::asset_component_test::tree;

TEST_CASE("asset definitions freeze reusable identity independently of scene placement",
          "[asset-schema]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_region.m_directory.m_root);
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    CHECK(K_DEFINITION->reference().m_asset == AssetId { 42 });
    CHECK(K_DEFINITION->reference().m_sha256 == hash(K_DEFINITION->canonical_json()));
    CHECK(K_DEFINITION->provenance().size() == 2uz);
    CHECK(K_DEFINITION->provenance()[1uz].m_coverage ==
          std::array<std::uint64_t, 5> { 2, 2, 2, 2, 0 });
    std::ranges::reverse(fixture.m_definition.m_sources);
    std::ranges::reverse(fixture.m_definition.m_provenance);
    std::ranges::reverse(fixture.m_provenance);
    const auto K_REORDERED = fixture.admit(K_RECEIPT);
    REQUIRE(K_REORDERED);
    CHECK(K_REORDERED->reference() == K_DEFINITION->reference());
    auto instances = fixture.instances(*K_DEFINITION);
    const auto K_FIRST =
        admit_asset_instances(instances, { *K_DEFINITION }, K_RECEIPT, instances.m_context);
    REQUIRE(K_FIRST);
    CHECK(K_FIRST->definitions().size() == 1uz);
    CHECK(K_FIRST->instances().m_instances.size() == 2uz);
    CHECK(K_FIRST->sha256() == hash(K_FIRST->canonical_json()));
    std::ranges::reverse(instances.m_instances);
    const auto K_SAME =
        admit_asset_instances(instances, { *K_DEFINITION }, K_RECEIPT, instances.m_context);
    REQUIRE(K_SAME);
    CHECK(K_SAME->sha256() == K_FIRST->sha256());
    instances.m_instances[0uz].m_world_from_asset.m_translation.m_x += 10;
    const auto K_MOVED =
        admit_asset_instances(instances, { *K_DEFINITION }, K_RECEIPT, instances.m_context);
    REQUIRE(K_MOVED);
    CHECK(K_MOVED->sha256() != K_FIRST->sha256());
    CHECK(K_MOVED->definitions()[0uz].reference() == K_DEFINITION->reference());
    CHECK(K_FIRST->instances().m_instances[1uz].m_world_from_asset.m_translation.m_x == 5);
    CHECK(tree(fixture.m_region.m_directory.m_root) == K_BEFORE);
    fixture.m_definition.m_revision = 2;
    const auto K_NEW = fixture.admit(K_RECEIPT);
    REQUIRE(K_NEW);
    CHECK(K_NEW->reference().m_sha256 != K_DEFINITION->reference().m_sha256);
    const auto K_STALE = admit_asset_instances(instances, { *K_NEW }, K_RECEIPT, instances.m_context);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
}

TEST_CASE("asset definitions reject invalid identities roles units bounds and pivots",
          "[asset-schema]") {
    const auto K_VARIANT =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    auto& value = fixture.m_definition;
    if (K_VARIANT == 0) {
        value.m_asset = {};
    }
    if (K_VARIANT == 1) {
        value.m_revision = 0;
    }
    if (K_VARIANT == 2) {
        value.m_context.m_pose_revision = { 6 };
    }
    if (K_VARIANT == 3) {
        value.m_sources.push_back(value.m_sources.front());
    }
    if (K_VARIANT == 4) {
        value.m_geometry.m_revision = 2;
    }
    if (K_VARIANT == 5) {
        value.m_materials[0uz].m_variant = {};
    }
    if (K_VARIANT == 6) {
        value.m_materials.push_back(value.m_materials.front());
    }
    if (K_VARIANT == 7) {
        value.m_lods.push_back(value.m_geometry);
    }
    if (K_VARIANT == 8) {
        value.m_bounds.m_minimum.m_x = 3;
    }
    if (K_VARIANT == 9) {
        value.m_bounds.m_maximum.m_y = std::numeric_limits<double>::infinity();
    }
    if (K_VARIANT == 10) {
        value.m_pivot.m_asset_from_geometry.m_rotation.m_w = 2;
    }
    if (K_VARIANT == 11) {
        value.m_pivot.m_asset_from_geometry.m_scale = -1;
    }
    if (K_VARIANT == 12) {
        value.m_pivot.m_asset_from_geometry.m_scale = 2;
    }
    if (K_VARIANT == 13) {
        value.m_pivot.m_policy = AssetPivotPolicy::STRUCTURAL_FRAME;
    }
    if (K_VARIANT == 14) {
        value.m_pivot.m_policy = AssetPivotPolicy::OVERRIDE;
    }
    if (K_VARIANT == 15) {
        value.m_units.m_meters_per_unit = 1;
    }
    if (K_VARIANT == 16) {
        value.m_units.m_status = AssetUnitStatus::DECLARED_METRIC;
    }
    if (K_VARIANT == 17) {
        value.m_rights.m_content_sha256 = std::string(65536uz, 'a');
    }
    if (K_VARIANT == 18) {
        value.m_manual_ancestor = true;
    }
    if (K_VARIANT == 19) {
        value.m_schema_version = 2;
    }
    const auto K_BEFORE = tree(fixture.m_region.m_directory.m_root);
    const auto K_REJECTED = fixture.admit(K_RECEIPT);
    REQUIRE(!K_REJECTED);
    CHECK(tree(fixture.m_region.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("asset definitions bind independently selected geometry and material provenance",
          "[asset-schema]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5);
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    if (K_VARIANT == 0) {
        fixture.m_provenance.pop_back();
    }
    if (K_VARIANT == 1) {
        fixture.m_provenance[1uz] = fixture.m_provenance[0uz];
    }
    if (K_VARIANT == 2) {
        fixture.m_provenance[0uz].m_region.m_domain.m_kind = AssetRegionKind::MATERIAL_TEXELS;
    }
    if (K_VARIANT == 3) {
        fixture.m_provenance[1uz].m_region.m_sources[0uz].m_pose_sha256 = hash("different pose");
    }
    if (K_VARIANT == 4) {
        fixture.m_provenance[1uz].m_product.m_revision = 2;
    }
    if (K_VARIANT == 5) {
        fixture.m_definition.m_provenance.pop_back();
    }
    const auto K_REJECTED = fixture.admit(K_RECEIPT);
    REQUIRE(!K_REJECTED);
}

TEST_CASE("instance sets reject stale selections unsafe transforms and unit mismatches",
          "[asset-schema]") {
    const auto K_VARIANT = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    auto value = fixture.instances(*K_DEFINITION);
    auto& row = value.m_instances[0uz];
    if (K_VARIANT == 0) {
        row.m_instance = {};
    }
    if (K_VARIANT == 1) {
        value.m_instances[1uz].m_instance = row.m_instance;
    }
    if (K_VARIANT == 2) {
        row.m_definition.m_revision = 2;
    }
    if (K_VARIANT == 3) {
        row.m_definition.m_sha256 = std::string(65536uz, 'f');
    }
    if (K_VARIANT == 4) {
        row.m_material_variant = { 99 };
    }
    if (K_VARIANT == 5) {
        row.m_world_from_asset.m_scale = 0;
    }
    if (K_VARIANT == 6) {
        row.m_world_from_asset.m_scale = -1;
    }
    if (K_VARIANT == 7) {
        row.m_world_from_asset.m_rotation.m_w = 0;
    }
    if (K_VARIANT == 8) {
        row.m_world_from_asset.m_translation.m_x = std::numeric_limits<double>::quiet_NaN();
    }
    if (K_VARIANT == 9) {
        row.m_world_from_asset.m_scale = std::numeric_limits<double>::max();
    }
    if (K_VARIANT == 10) {
        value.m_units.m_status = AssetUnitStatus::DECLARED_METRIC;
        value.m_units.m_meters_per_unit = 1;
        value.m_units.m_measurement = fixture.m_measurement;
    }
    if (K_VARIANT == 11) {
        row.m_room = fixture.m_definition.m_geometry;
        row.m_room.value().m_revision = 2;
    }
    if (K_VARIANT == 12) {
        row.m_lineage = { { {}, fixture.m_measurement } };
    }
    if (K_VARIANT == 13) {
        row.m_manual_ancestor = true;
    }
    if (K_VARIANT == 14) {
        value.m_context.m_world_epoch = { 4 };
    }
    if (K_VARIANT == 15) {
        value.m_manual_ancestor = true;
    }
    const auto K_BEFORE = tree(fixture.m_region.m_directory.m_root);
    const auto K_REJECTED = admit_asset_instances(
        value, { *K_DEFINITION }, K_RECEIPT, fixture.m_region.m_graph.m_context);
    REQUIRE(!K_REJECTED);
    CHECK(tree(fixture.m_region.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("declared metric units and pivot overrides remain explicit and propagate manual ancestry",
          "[asset-schema]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    fixture.m_definition.m_units = { AssetUnitStatus::DECLARED_METRIC, 0.01, fixture.m_measurement };
    fixture.m_definition.m_pivot.m_policy = AssetPivotPolicy::OVERRIDE;
    fixture.m_definition.m_pivot.m_override_action = fixture.m_action;
    fixture.m_definition.m_manual_ancestor = true;
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    CHECK(K_DEFINITION->definition().m_units.m_status == AssetUnitStatus::DECLARED_METRIC);
    CHECK(K_DEFINITION->definition().m_manual_ancestor);
    auto value = fixture.instances(*K_DEFINITION);
    const auto K_ADMITTED =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(K_ADMITTED);
    value.m_instances[0uz].m_manual_ancestor = false;
    const auto K_CLEARED =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_CLEARED);
}

TEST_CASE("instance lineage and explicit actions cannot clear declared manual ancestry",
          "[asset-schema]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    auto value = fixture.instances(*K_DEFINITION);
    value.m_instances[0uz].m_lineage = { { { 55 }, fixture.m_action } };
    value.m_instances[0uz].m_manual_ancestor = true;
    value.m_manual_ancestor = true;
    const auto K_LINEAGE =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(K_LINEAGE);
    value.m_instances[0uz].m_lineage.push_back(value.m_instances[0uz].m_lineage.front());
    const auto K_DUPLICATE =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_DUPLICATE);
    value = fixture.instances(*K_DEFINITION);
    value.m_manual_action = fixture.m_action;
    value.m_manual_ancestor = true;
    for (auto& instance : value.m_instances) {
        instance.m_manual_ancestor = true;
    }
    const auto K_ACTION = admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(K_ACTION);
    value.m_instances[1uz].m_manual_ancestor = false;
    const auto K_CLEARED =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_CLEARED);
}

TEST_CASE(
    "asset and instance admission retain cancellation empty sets and source snapshot identity",
    "[asset-schema]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED = fixture.admit(K_RECEIPT, stop.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    auto value = fixture.instances(*K_DEFINITION);
    const auto K_STOPPED =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context, stop.get_token());
    REQUIRE(!K_STOPPED);
    CHECK(K_STOPPED.error().m_code == ErrorCode::CANCELLED);
    const auto K_DUPLICATE =
        admit_asset_instances(value, { *K_DEFINITION, *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_DUPLICATE);
    value.m_instances.clear();
    const auto K_UNUSED = admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_UNUSED);
    const auto K_EMPTY = admit_asset_instances(value, {}, K_RECEIPT, value.m_context);
    REQUIRE(K_EMPTY);
    CHECK(K_EMPTY->instances().m_instances.empty());
    fixture.m_region.m_graph.m_products[0uz].m_recipe_sha256 = hash("other opaque recipe");
    fixture.m_region.m_data[hash("other opaque recipe")] = "other opaque recipe";
    const auto K_CHANGED_RECEIPT = fixture.freeze();
    value = fixture.instances(*K_DEFINITION);
    const auto K_FOREIGN =
        admit_asset_instances(value, { *K_DEFINITION }, K_CHANGED_RECEIPT, value.m_context);
    REQUIRE(!K_FOREIGN);
    CHECK(K_FOREIGN.error().m_code == ErrorCode::RESYNC_REQUIRED);
}

TEST_CASE("material lighting declarations cannot hide generated texel evidence", "[asset-schema]") {
    const auto K_CAPTURED = GENERATE(false, true);
    SchemaFixture fixture;
    fixture.m_definition.m_materials[0uz].m_lighting =
        K_CAPTURED ? AssetLightingMode::CAPTURED_APPEARANCE : AssetLightingMode::ESTIMATED_PBR;
    const auto K_MIXED_RECEIPT = fixture.freeze();
    const auto K_MIXED = fixture.admit(K_MIXED_RECEIPT);
    REQUIRE(!K_MIXED);
    for (auto& span : fixture.m_material_record["spans"]) {
        if (span["class"].get<std::string>() == "GENERATED") {
            span["class"] = "INFERRED";
        }
    }
    if (K_CAPTURED) {
        auto& spans = fixture.m_material_record["spans"];
        for (std::size_t index { 2uz }; index < 4uz; ++index) {
            spans[index]["class"] = "OBSERVED";
            spans[index]["support"] = spans[1uz]["support"];
        }
    }
    const auto K_CLEAN_RECEIPT = fixture.freeze();
    const auto K_CLEAN = fixture.admit(K_CLEAN_RECEIPT);
    REQUIRE(K_CLEAN);
    CHECK(K_CLEAN->definition().m_materials[0uz].m_lighting ==
          fixture.m_definition.m_materials[0uz].m_lighting);
}

TEST_CASE(
    "instance sets enforce exact instance and lineage limits and retain distinct asset identities",
    "[asset-schema]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    fixture.m_definition.m_asset = { 43 };
    const auto K_OTHER = fixture.admit(K_RECEIPT);
    REQUIRE(K_OTHER);
    CHECK(K_OTHER->reference() != K_DEFINITION->reference());
    CHECK(K_OTHER->definition().m_geometry == K_DEFINITION->definition().m_geometry);
    auto value = fixture.instances(*K_DEFINITION);
    value.m_instances[1uz].m_definition = K_OTHER->reference();
    const auto K_DISTINCT =
        admit_asset_instances(value, { *K_OTHER, *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(K_DISTINCT);
    CHECK(K_DISTINCT->definitions().size() == 2uz);
    CHECK(K_DISTINCT->definitions()[0uz].reference().m_asset == AssetId { 42 });
    auto row = value.m_instances.front();
    value.m_instances.clear();
    for (std::size_t index { 0uz }; index < 4096uz; ++index) {
        row.m_instance = { index + 1uz };
        if (index < 1024uz) {
            row.m_lineage.clear();
            for (std::size_t parent { 0uz }; parent < 16uz; ++parent) {
                row.m_lineage.push_back({ { parent + 1uz }, fixture.m_measurement });
            }
        } else {
            row.m_lineage.clear();
        }
        value.m_instances.push_back(row);
    }
    const auto K_EXACT = admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(K_EXACT);
    CHECK(K_EXACT->instances().m_instances.size() == 4096uz);
    value.m_instances[1024uz].m_lineage.push_back({ { 17 }, fixture.m_measurement });
    const auto K_LINEAGE_EXCESS =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_LINEAGE_EXCESS);
    CHECK(K_LINEAGE_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    value.m_instances[1024uz].m_lineage.clear();
    row.m_instance = { 4097 };
    row.m_lineage.clear();
    value.m_instances.push_back(row);
    const auto K_INSTANCE_EXCESS =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_INSTANCE_EXCESS);
    CHECK(K_INSTANCE_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    value.m_instances.resize(1uz);
    value.m_instances[0uz].m_lineage.push_back({ { 17 }, fixture.m_measurement });
    const auto K_ROW_EXCESS =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(!K_ROW_EXCESS);
}

TEST_CASE("definitions require separate provenance for LODs colliders and material variants",
          "[asset-schema]") {
    SchemaFixture fixture;
    std::vector<AssetProvenanceSelection> extra;
    const auto K_OUTPUT = [&](std::uint64_t id, AssetRegionKind kind) {
        auto output = fixture.m_region.node(id, "synthetic extra product " + std::to_string(id));
        output.m_dependencies = fixture.m_region.m_graph.m_products[2uz].m_dependencies;
        auto selection = fixture.m_region.m_selection;
        selection.m_target = output.m_product;
        auto record = kind == AssetRegionKind::GEOMETRY_FACES ? fixture.m_region.m_record
                                                              : fixture.m_material_record;
        if (kind == AssetRegionKind::MATERIAL_TEXELS) {
            selection.m_domain = { kind, 8, 4, 2 };
        }
        record["target"] = region_ref(output.m_product);
        auto provenance = fixture.m_region.node(id + 1, record.dump());
        provenance.m_dependencies = { output.m_product };
        fixture.m_region.m_graph.m_products.insert(fixture.m_region.m_graph.m_products.end(),
                                                   { output, provenance });
        extra.push_back({ provenance.m_product, selection });
        return output.m_product;
    };
    fixture.m_definition.m_lods.push_back(K_OUTPUT(4000, AssetRegionKind::GEOMETRY_FACES));
    fixture.m_definition.m_collider = { K_OUTPUT(5000, AssetRegionKind::GEOMETRY_FACES),
                                       AssetColliderMode::SYNTHETIC_GAMEPLAY };
    fixture.m_definition.m_materials.push_back(
        { { 8 }, K_OUTPUT(6000, AssetRegionKind::MATERIAL_TEXELS), AssetLightingMode::GENERATED_PBR });
    const auto K_RECEIPT = fixture.freeze();
    for (const auto& item : extra) {
        fixture.m_definition.m_provenance.push_back(item.m_product);
        fixture.m_provenance.push_back(item);
    }
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    CHECK(K_DEFINITION->provenance().size() == 5uz);
    CHECK(K_DEFINITION->definition().m_lods.size() == 1uz);
    auto value = fixture.instances(*K_DEFINITION);
    value.m_instances[0uz].m_material_variant = { 8 };
    const auto K_VARIANT =
        admit_asset_instances(value, { *K_DEFINITION }, K_RECEIPT, value.m_context);
    REQUIRE(K_VARIANT);
    fixture.m_provenance.pop_back();
    const auto K_MISSING = fixture.admit(K_RECEIPT);
    REQUIRE(!K_MISSING);
}

TEST_CASE(
    "instance sets admit exactly sixty-four semantic definitions without merging shared bytes",
    "[asset-schema]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    std::vector<AdmittedAssetDefinition> definitions;
    AssetInstanceSet value { 1, { 90 }, { 1 }, fixture.m_region.m_graph.m_context, {}, {}, {}, false };
    for (std::size_t index { 0uz }; index < 64uz; ++index) {
        fixture.m_definition.m_asset = { index + 1uz };
        const auto K_DEFINITION = fixture.admit(K_RECEIPT);
        REQUIRE(K_DEFINITION);
        definitions.push_back(*K_DEFINITION);
        auto instance = fixture.instances(*K_DEFINITION).m_instances[0uz];
        instance.m_instance = { index + 1uz };
        value.m_instances.push_back(instance);
    }
    const auto K_EXACT = admit_asset_instances(value, definitions, K_RECEIPT, value.m_context);
    REQUIRE(K_EXACT);
    CHECK(K_EXACT->definitions().size() == 64uz);
    definitions.push_back(definitions.front());
    const auto K_EXCESS = admit_asset_instances(value, definitions, K_RECEIPT, value.m_context);
    REQUIRE(!K_EXCESS);
    CHECK(K_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}
