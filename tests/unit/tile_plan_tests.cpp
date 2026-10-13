#include "world_explorer/render_core/tile_plan.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <vector>

using namespace world_explorer;
namespace {
tile_rectangle next_rectangle(tile_region_plan& plan) {
    const auto next = plan.next();
    REQUIRE(next);
    if (!*next)
        throw std::runtime_error("expected another tile rectangle");
    return **next;
}
std::vector<tile_rectangle> partition(tile_region_plan& plan, std::uint64_t limit) {
    std::vector<tile_rectangle> rectangles;
    for (;;) {
        auto next = plan.next();
        REQUIRE(next);
        if (!*next)
            break;
        const auto rectangle = **next;
        if (static_cast<std::uint64_t>(rectangle.width) * rectangle.height > limit) {
            REQUIRE(plan.split_current());
        } else {
            rectangles.push_back(rectangle);
            REQUIRE(plan.accept_current());
        }
    }
    return rectangles;
}
void check_coverage(tile_grid grid, const std::vector<tile_rectangle>& rectangles) {
    std::vector<unsigned> pixels(static_cast<std::size_t>(grid.width) * grid.height);
    for (const auto& rectangle : rectangles) {
        REQUIRE(rectangle.width > 0);
        REQUIRE(rectangle.height > 0);
        REQUIRE(rectangle.x + rectangle.width <= grid.width);
        REQUIRE(rectangle.y + rectangle.height <= grid.height);
        for (auto y = rectangle.y; y < rectangle.y + rectangle.height; ++y)
            for (auto x = rectangle.x; x < rectangle.x + rectangle.width; ++x)
                ++pixels[static_cast<std::size_t>(y) * grid.width + x];
    }
    for (auto visits : pixels)
        REQUIRE(visits == 1);
}
} // namespace
TEST_CASE("tile plan retains exact odd-grid coverage and revalidates cached candidates") {
    tile_region_plan plan;
    REQUIRE(plan.begin({7, 5}).value() == 0);
    const auto original = partition(plan, 6);
    check_coverage({7, 5}, original);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({7, 5}).value() == original.size());
    REQUIRE(partition(plan, 6) == original);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({7, 5}).value() == original.size());
    const auto denser = partition(plan, 2);
    REQUIRE(denser.size() > original.size());
    check_coverage({7, 5}, denser);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({7, 5}).value() == denser.size());
    REQUIRE(partition(plan, 100) == denser); // Counts are caller-owned; geometry alone is retained.
    REQUIRE(plan.commit());
}
TEST_CASE("failed and misordered tile plans preserve the last complete partition") {
    tile_region_plan plan;
    REQUIRE_FALSE(plan.next());
    REQUIRE_FALSE(plan.accept_current());
    REQUIRE_FALSE(plan.split_current());
    REQUIRE_FALSE(plan.commit());
    REQUIRE_FALSE(plan.begin({0, 5}));
    REQUIRE(plan.begin({7, 5}));
    const auto original = partition(plan, 6);
    REQUIRE(plan.commit());
    REQUIRE_FALSE(plan.commit());
    REQUIRE(plan.begin({7, 5}));
    auto copy = next_rectangle(plan);
    copy.width = 0;
    REQUIRE(copy.width == 0);
    REQUIRE_FALSE(plan.next());
    REQUIRE_FALSE(plan.commit());
    REQUIRE(plan.accept_current());
    const auto remaining = partition(plan, 6);
    REQUIRE(std::vector<tile_rectangle>(original.begin() + 1, original.end()) == remaining);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({7, 5}));
    REQUIRE(plan.next());
    REQUIRE(plan.split_current());
    REQUIRE(plan.begin({7, 5}).value() == original.size());
    REQUIRE(partition(plan, 6) == original);
    REQUIRE(plan.commit());
    REQUIRE(plan.begin({8, 5}).value() == 0);
    REQUIRE((next_rectangle(plan) == tile_rectangle{0, 0, 8, 5}));
    REQUIRE(plan.begin({7, 5}, false).value() == 0);
    REQUIRE((next_rectangle(plan) == tile_rectangle{0, 0, 7, 5}));
    plan.reset();
    REQUIRE(plan.begin({7, 5}).value() == 0);
}
TEST_CASE("bounded tile cache never truncates an excessively fragmented partition") {
    tile_region_plan plan;
    REQUIRE(plan.begin({17, 17}));
    const auto rectangles = partition(plan, 1);
    REQUIRE(rectangles.size() == 289);
    check_coverage({17, 17}, rectangles);
    REQUIRE(plan.commit());
    REQUIRE(plan.cached_region_count() == 0);
    REQUIRE(plan.begin({17, 17}).value() == 0);
    REQUIRE((next_rectangle(plan) == tile_rectangle{0, 0, 17, 17}));
}
TEST_CASE("single-tile rejection and maximum dimensions preserve owned geometry") {
    tile_region_plan plan;
    REQUIRE(plan.begin({1, 1}));
    REQUIRE(plan.next());
    const auto rejected = plan.split_current();
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    REQUIRE(plan.accept_current());
    REQUIRE(plan.commit());
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    REQUIRE(plan.begin({maximum, maximum}).value() == 0);
    for (unsigned depth = 0; depth < 64; ++depth) {
        const auto rectangle = next_rectangle(plan);
        if (rectangle.width == 1 && rectangle.height == 1) {
            REQUIRE(plan.accept_current());
            break;
        }
        REQUIRE(plan.split_current());
    }
    REQUIRE_FALSE(plan.commit());
}
