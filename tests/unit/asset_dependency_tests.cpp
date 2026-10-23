#include "world_explorer/asset_catalog/dependency_graph.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>

using namespace world_explorer;

namespace {

AssetDependencyNode node(std::uint64_t id, char content = 'a') {
    return { { { id }, 1, std::string(64uz, content) }, {}, std::string(64uz, 'c'), {}, false };
}

AssetDependencyGraph fixture() {
    auto first = node(1, 'a');
    auto second = node(2, 'b');
    auto third = node(3, 'd');
    third.m_dependencies = { second.m_product, first.m_product };
    return { 1, { { 7 }, { 11 } }, { third, second, first } };
}

}  // namespace

TEST_CASE("asset dependency identity is canonical while semantic IDs and bytes remain distinct",
          "[asset-dependencies]") {
    auto graph = fixture();
    const auto K_BEFORE = graph.m_products;
    const auto K_ADMITTED = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->m_topological_order == std::vector<AssetProductId> { { 1 }, { 2 }, { 3 } });
    CHECK(K_ADMITTED->m_graph_sha256 ==
          "4be7d137ff187c08673fb7b9952b442fdf8a5e2afbee3800e42dbcfbeee0ba12");
    CHECK(graph.m_products == K_BEFORE);
    std::ranges::reverse(graph.m_products);
    for (auto& product : graph.m_products) {
        std::ranges::reverse(product.m_dependencies);
    }
    const auto K_REORDERED = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE(K_REORDERED);
    CHECK(K_REORDERED->m_graph_sha256 == K_ADMITTED->m_graph_sha256);
    CHECK(K_REORDERED->m_topological_order == K_ADMITTED->m_topological_order);
    // Shared bytes do not collapse distinct semantic products.
    graph.m_products[1uz].m_product.m_content_sha256 =
        graph.m_products[0uz].m_product.m_content_sha256;
    graph.m_products[2uz].m_dependencies[1uz] = graph.m_products[1uz].m_product;
    const auto K_SHARED_BYTES = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE(K_SHARED_BYTES);
    CHECK(K_SHARED_BYTES->m_topological_order.size() == 3uz);
    CHECK(K_SHARED_BYTES->m_graph_sha256 != K_ADMITTED->m_graph_sha256);
}

TEST_CASE("asset dependency admission rejects malformed cyclic foreign and stale selections",
          "[asset-dependencies]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    CAPTURE(K_FIELD);
    auto graph = fixture();
    const auto K_CONTEXT = graph.m_context;
    auto expected = ErrorCode::INVALID_DATA;
    if (K_FIELD == 0) {
        graph.m_products.push_back(graph.m_products.back());
    }
    if (K_FIELD == 1) {
        graph.m_products[1uz].m_product.m_product = {};
    }
    if (K_FIELD == 2) {
        graph.m_products[1uz].m_product.m_revision = 0;
    }
    if (K_FIELD == 3) {
        graph.m_products[1uz].m_product.m_content_sha256 = std::string(64uz, 'A');
    }
    if (K_FIELD == 4) {
        graph.m_products[1uz].m_recipe_sha256.clear();
    }
    if (K_FIELD == 5) {
        graph.m_products[1uz].m_manual_action_sha256 = "unbound";
    }
    if (K_FIELD == 6) {
        graph.m_products[0uz].m_dependencies[0uz].m_product = { 99 };
    }
    if (K_FIELD == 7) {
        graph.m_products[0uz].m_dependencies.push_back(graph.m_products[0uz].m_dependencies[0uz]);
    }
    if (K_FIELD == 8) {
        graph.m_products[0uz].m_dependencies.push_back(graph.m_products[0uz].m_product);
    }
    if (K_FIELD == 9) {
        graph.m_products[2uz].m_dependencies.push_back(graph.m_products[0uz].m_product);
    }
    if (K_FIELD == 10) {
        graph.m_products[0uz].m_dependencies[0uz].m_content_sha256 = std::string(64uz, 'e');
        expected = ErrorCode::RESYNC_REQUIRED;
    }
    if (K_FIELD == 11) {
        ++graph.m_products[0uz].m_dependencies[0uz].m_revision;
        expected = ErrorCode::RESYNC_REQUIRED;
    }
    if (K_FIELD == 12) {
        graph.m_schema_version = 2;
        expected = ErrorCode::UNSUPPORTED_OPERATION;
    }
    if (K_FIELD == 13) {
        graph.m_products[2uz].m_manual_ancestor = true;
    }
    if (K_FIELD == 14) {
        graph.m_products[2uz].m_manual_action_sha256 = std::string(64uz, 'e');
    }
    if (K_FIELD == 15) {
        ++graph.m_context.m_world_epoch.m_value;
        expected = ErrorCode::RESYNC_REQUIRED;
    }
    if (K_FIELD == 16) {
        ++graph.m_context.m_pose_revision.m_value;
        expected = ErrorCode::RESYNC_REQUIRED;
    }
    if (K_FIELD == 17) {
        graph.m_products.clear();
    }
    if (K_FIELD == 18) {
        graph.m_products[0uz].m_dependencies[0uz].m_content_sha256.clear();
    }
    const auto K_BEFORE = graph.m_products;
    const auto K_RESULT = validate_asset_dependencies(graph, K_CONTEXT);
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == expected);
    CHECK(graph.m_products == K_BEFORE);
}

TEST_CASE("asset descendants retain manual selection ancestry across revisions",
          "[asset-dependencies]") {
    auto graph = fixture();
    graph.m_products[2uz].m_manual_action_sha256 = std::string(64uz, 'f');
    graph.m_products[2uz].m_manual_ancestor = true;
    graph.m_products[0uz].m_manual_ancestor = true;
    auto fourth = node(4);
    fourth.m_dependencies = { graph.m_products[0uz].m_product };
    fourth.m_manual_ancestor = true;
    graph.m_products.push_back(fourth);
    const auto K_VALID = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE(K_VALID);
    SECTION("direct descendant cannot erase manual ancestry") {
        graph.m_products[0uz].m_manual_ancestor = false;
    }
    SECTION("transitive descendant cannot erase manual ancestry") {
        graph.m_products[3uz].m_manual_ancestor = false;
    }
    const auto K_RESULT = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
}

TEST_CASE("asset dependency cache identity includes revision recipe epoch and manual actions",
          "[asset-dependencies]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    auto graph = fixture();
    const auto K_ORIGINAL = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE(K_ORIGINAL);
    if (K_FIELD == 0) {
        graph.m_products[0uz].m_recipe_sha256 = std::string(64uz, 'e');
    }
    if (K_FIELD == 1) {
        ++graph.m_context.m_world_epoch.m_value;
    }
    if (K_FIELD == 2) {
        ++graph.m_context.m_pose_revision.m_value;
    }
    if (K_FIELD == 3) {
        ++graph.m_products[1uz].m_product.m_revision;
        graph.m_products[0uz].m_dependencies[0uz] = graph.m_products[1uz].m_product;
    }
    if (K_FIELD == 4) {
        graph.m_products[0uz].m_manual_action_sha256 = std::string(64uz, 'f');
        graph.m_products[0uz].m_manual_ancestor = true;
    }
    const auto K_CHANGED = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE(K_CHANGED);
    CHECK(K_CHANGED->m_graph_sha256 != K_ORIGINAL->m_graph_sha256);
}

TEST_CASE("asset dependency bounds reject excess before constructing a derived graph",
          "[asset-dependencies]") {
    auto graph = fixture();
    graph.m_products.clear();
    SECTION("product count") {
        for (std::size_t index { 0uz }; index < 256uz; ++index) {
            graph.m_products.push_back(node(index + 1uz));
        }
        REQUIRE(validate_asset_dependencies(graph, graph.m_context));
        graph.m_products.push_back(node(257));
    }
    SECTION("per product edges") {
        for (std::size_t index { 0uz }; index < 65uz; ++index) {
            graph.m_products.push_back(node(index + 1uz));
        }
        auto child = node(66);
        for (std::size_t index { 0uz }; index < 64uz; ++index) {
            child.m_dependencies.push_back(graph.m_products[index].m_product);
        }
        graph.m_products.push_back(child);
        REQUIRE(validate_asset_dependencies(graph, graph.m_context));
        graph.m_products.back().m_dependencies.push_back(graph.m_products[64uz].m_product);
    }
    SECTION("aggregate edges") {
        for (std::size_t index { 0uz }; index < 128uz; ++index) {
            graph.m_products.push_back(node(index + 1uz));
            if (index >= 64uz) {
                for (std::size_t parent { 0uz }; parent < 64uz; ++parent) {
                    graph.m_products.back().m_dependencies.push_back(
                        graph.m_products[parent].m_product);
                }
            }
        }
        REQUIRE(validate_asset_dependencies(graph, graph.m_context));
        auto child = node(129);
        child.m_dependencies.push_back(graph.m_products[0uz].m_product);
        graph.m_products.push_back(child);
    }
    const auto K_RESULT = validate_asset_dependencies(graph, graph.m_context);
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("asset dependency caller context and cancellation do not admit an unselected snapshot",
          "[asset-dependencies]") {
    const auto K_GRAPH = fixture();
    auto context = K_GRAPH.m_context;
    context.m_pose_revision = {};
    const auto K_CONTEXT = validate_asset_dependencies(K_GRAPH, context);
    REQUIRE_FALSE(K_CONTEXT);
    CHECK(K_CONTEXT.error().m_code == ErrorCode::INVALID_ARGUMENT);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED =
        validate_asset_dependencies(K_GRAPH, K_GRAPH.m_context, stop.get_token());
    REQUIRE_FALSE(K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    auto graph = fixture();
    graph.m_products[0uz].m_product.m_revision = std::numeric_limits<std::uint64_t>::max();
    REQUIRE(validate_asset_dependencies(graph, graph.m_context));
}
