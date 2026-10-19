#include "world_explorer/render_core/tile_plan.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

using namespace world_explorer;

namespace {

TileRectangle next_rectangle(TileRegionPlan& plan) {
    const auto K_NEXT = plan.next();
    REQUIRE(K_NEXT);
    if (!*K_NEXT) {
        throw std::runtime_error("expected another tile rectangle");
    }
    return **K_NEXT;
}

std::vector<TileRectangle> partition(TileRegionPlan& plan, std::uint64_t limit) {
    std::vector<TileRectangle> rectangles;
    for (;;) {
        auto next = plan.next();
        REQUIRE(next);
        if (!*next) {
            break;
        }
        const auto K_RECTANGLE = **next;
        if (static_cast<std::uint64_t>(K_RECTANGLE.m_width) * K_RECTANGLE.m_height > limit) {
            REQUIRE(plan.split_current());
        } else {
            rectangles.push_back(K_RECTANGLE);
            REQUIRE(plan.accept_current());
        }
    }
    return rectangles;
}

void check_coverage(TileGrid grid, const std::vector<TileRectangle>& rectangles) {
    std::vector<unsigned> pixels(static_cast<std::size_t>(grid.m_width) * grid.m_height);
    for (const auto& rectangle : rectangles) {
        REQUIRE(rectangle.m_width > 0);
        REQUIRE(rectangle.m_height > 0);
        REQUIRE(rectangle.m_x + rectangle.m_width <= grid.m_width);
        REQUIRE(rectangle.m_y + rectangle.m_height <= grid.m_height);
        for (auto y = rectangle.m_y; y < rectangle.m_y + rectangle.m_height; ++y) {
            for (auto x = rectangle.m_x; x < rectangle.m_x + rectangle.m_width; ++x) {
                ++pixels[static_cast<std::size_t>(y) * grid.m_width + x];
            }
        }
    }
    for (auto visits : pixels) {
        REQUIRE(visits == 1);
    }
}

}  // namespace

TEST_CASE("tile plan retains exact odd-grid coverage and revalidates cached candidates") {
    TileRegionPlan plan;
    REQUIRE(plan.begin({ 7, 5 }).value() == 0);
    const auto K_ORIGINAL = partition(plan, 6);
    check_coverage({ 7, 5 }, K_ORIGINAL);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({ 7, 5 }).value() == K_ORIGINAL.size());
    REQUIRE(partition(plan, 6) == K_ORIGINAL);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({ 7, 5 }).value() == K_ORIGINAL.size());
    const auto K_DENSER = partition(plan, 2);
    REQUIRE(K_DENSER.size() > K_ORIGINAL.size());
    check_coverage({ 7, 5 }, K_DENSER);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({ 7, 5 }).value() == K_DENSER.size());
    REQUIRE(partition(plan, 100) ==
            K_DENSER);  // Counts are caller-owned; geometry alone is retained.
    REQUIRE(plan.commit());
}

TEST_CASE("failed and misordered tile plans preserve the last complete partition") {
    TileRegionPlan plan;
    REQUIRE_FALSE(plan.next());
    REQUIRE_FALSE(plan.accept_current());
    REQUIRE_FALSE(plan.split_current());
    REQUIRE_FALSE(plan.commit());
    REQUIRE_FALSE(plan.begin({ 0, 5 }));
    REQUIRE(plan.begin({ 7, 5 }));
    const auto K_ORIGINAL = partition(plan, 6);
    REQUIRE(plan.commit());
    REQUIRE_FALSE(plan.commit());
    REQUIRE(plan.begin({ 7, 5 }));
    auto copy = next_rectangle(plan);
    copy.m_width = 0;
    REQUIRE(copy.m_width == 0);
    REQUIRE_FALSE(plan.next());
    REQUIRE_FALSE(plan.commit());
    REQUIRE(plan.accept_current());
    const auto K_REMAINING = partition(plan, 6);
    REQUIRE(std::vector<TileRectangle>(K_ORIGINAL.begin() + 1, K_ORIGINAL.end()) == K_REMAINING);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({ 7, 5 }));
    REQUIRE(plan.next());
    REQUIRE(plan.split_current());
    REQUIRE(plan.begin({ 7, 5 }).value() == K_ORIGINAL.size());
    REQUIRE(partition(plan, 6) == K_ORIGINAL);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({ 8, 5 }).value() == 0);
    REQUIRE((next_rectangle(plan) == TileRectangle { 0, 0, 8, 5 }));
    REQUIRE(plan.begin({ 7, 5 }, false).value() == 0);
    REQUIRE((next_rectangle(plan) == TileRectangle { 0, 0, 7, 5 }));
    plan.reset();
    REQUIRE(plan.begin({ 7, 5 }).value() == 0);
}

TEST_CASE("bounded tile cache never truncates an excessively fragmented partition") {
    TileRegionPlan plan;
    REQUIRE(plan.begin({ 17, 17 }));
    const auto K_RECTANGLES = partition(plan, 1);
    REQUIRE(K_RECTANGLES.size() == 289);
    check_coverage({ 17, 17 }, K_RECTANGLES);
    REQUIRE(plan.commit());
    REQUIRE(plan.cached_region_count() == 0);
    REQUIRE(plan.begin({ 17, 17 }).value() == 0);
    REQUIRE((next_rectangle(plan) == TileRectangle { 0, 0, 17, 17 }));
}

TEST_CASE("single-tile rejection and maximum dimensions preserve owned geometry") {
    TileRegionPlan plan;
    REQUIRE(plan.begin({ 1, 1 }));
    REQUIRE(plan.next());
    const auto K_REJECTED = plan.split_current();
    REQUIRE_FALSE(K_REJECTED);
    REQUIRE(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    REQUIRE(plan.accept_current());
    REQUIRE(plan.commit());
    const auto K_MAXIMUM = std::numeric_limits<std::uint32_t>::max();
    REQUIRE(plan.begin({ K_MAXIMUM, K_MAXIMUM }).value() == 0);
    for (unsigned depth { 0 }; depth < 64; ++depth) {
        const auto K_RECTANGLE = next_rectangle(plan);
        if (K_RECTANGLE.m_width == 1 && K_RECTANGLE.m_height == 1) {
            REQUIRE(plan.accept_current());
            break;
        }
        REQUIRE(plan.split_current());
    }
    REQUIRE_FALSE(plan.commit());
}
