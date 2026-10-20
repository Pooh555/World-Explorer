#include "world_explorer/application/depth_space.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/depth_space.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <limits>

using namespace world_explorer;

namespace {

DepthImageAsset fixture() {
    DepthImageAsset depth;
    auto& metadata = depth.m_metadata;
    metadata.m_dependency = { { 1 }, { 2 }, { 3 }, { 4 }, std::string(64uz, 'a'), std::string(64uz, 'b') };
    metadata.m_width = 17;
    metadata.m_height = 13;
    metadata.m_camera = { 8, 6, 8, 6 };
    metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
    metadata.m_recipe_sha256 = std::string(64uz, 'd');
    depth.m_axial_depth.assign(221uz, 4);
    depth.m_valid.assign(221uz, 1);
    depth.m_confidence.assign(221uz, .5);
    depth.m_support_count.assign(221uz, 2);
    return depth;
}

// Independent scalar oracle: project all corners, round outwards, enumerate
// every original pixel in the enclosing block footprint. No production pyramid.
DepthCellRelation
scalar(const DepthImageAsset& depth, const DepthSpaceRecipe& recipe, const Bounds3d& bounds) {
    std::vector<Vec3d> corners;
    for (const double K_X : { bounds.m_minimum.m_x, bounds.m_maximum.m_x }) {
        for (const double K_Y : { bounds.m_minimum.m_y, bounds.m_maximum.m_y }) {
            for (const double K_Z : { bounds.m_minimum.m_z, bounds.m_maximum.m_z }) {
                corners.push_back(
                    inverse_transform_point(recipe.m_world_from_camera, { K_X, K_Y, K_Z }));
            }
        }
    }
    double left { 1e100 }, top { 1e100 }, right { -1e100 }, bottom { -1e100 };
    double near { 1e100 }, far { -1e100 };
    for (const auto K_P : corners) {
        if (!finite(K_P) || K_P.m_z <= 0) {
            return DepthCellRelation::OUTSIDE_VIEW;
        }
        const auto& camera = depth.m_metadata.m_camera;
        const double K_U { camera.m_fx * K_P.m_x / K_P.m_z + camera.m_cx };
        const double K_V { camera.m_fy * K_P.m_y / K_P.m_z + camera.m_cy };
        left = std::min(left, K_U);
        right = std::max(right, K_U);
        top = std::min(top, K_V);
        bottom = std::max(bottom, K_V);
        near = std::min(near, K_P.m_z);
        far = std::max(far, K_P.m_z);
    }
    left = std::floor(left - 1e-7);
    top = std::floor(top - 1e-7);
    right = std::ceil(right + 1e-7);
    bottom = std::ceil(bottom + 1e-7);
    const auto K_WIDTH = depth.m_metadata.m_width, K_HEIGHT = depth.m_metadata.m_height;
    if (left < 0 || top < 0 || right >= K_WIDTH || bottom >= K_HEIGHT) {
        return DepthCellRelation::OUTSIDE_VIEW;
    }
    const auto K_EXTENT = static_cast<std::size_t>(std::max(right - left, bottom - top)) + 1uz;
    std::size_t side { 1uz };
    while (side < K_EXTENT) {
        side *= 2uz;
    }
    const auto K_X0 = static_cast<std::size_t>(left) / side * side;
    const auto K_Y0 = static_cast<std::size_t>(top) / side * side;
    const auto K_X1 = std::min((static_cast<std::size_t>(right) / side + 1uz) * side,
                               static_cast<std::size_t>(K_WIDTH));
    const auto K_Y1 = std::min((static_cast<std::size_t>(bottom) / side + 1uz) * side,
                               static_cast<std::size_t>(K_HEIGHT));
    bool front { true }, behind { true };
    const double K_GUARD { recipe.m_world_guard / recipe.m_world_from_camera.m_scale };
    for (auto y = K_Y0; y < K_Y1; ++y) {
        for (auto x = K_X0; x < K_X1; ++x) {
            const auto K_INDEX = x + static_cast<std::size_t>(K_WIDTH) * y;
            if (!depth.m_valid[K_INDEX] || depth.m_support_count[K_INDEX] < 2 ||
                depth.m_confidence[K_INDEX] <= 0) {
                return DepthCellRelation::UNKNOWN_DEPTH;
            }
            const double K_SIGMA {
                depth.m_standard_deviation ? depth.m_standard_deviation->at(K_INDEX) : 0. };
            front = front && far + K_GUARD < depth.m_axial_depth[K_INDEX] - 2. * K_SIGMA;
            behind = behind && near - K_GUARD > depth.m_axial_depth[K_INDEX] + 2. * K_SIGMA;
        }
    }
    return front    ? DepthCellRelation::FRONT_DIAGNOSTIC
           : behind ? DepthCellRelation::BEHIND_SURFACE
                    : DepthCellRelation::SURFACE_BAND;
}

constexpr Bounds3d K_CELL { { -.1, -.1, 1.8 }, { .1, .1, 2.2 } };

struct Scratch {
    std::filesystem::path m_path =
        std::filesystem::temp_directory_path() /
        ("we-depth-space-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Scratch() {
        std::filesystem::create_directory(m_path);
    }

    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
};

}  // namespace

TEST_CASE("depth-space distinguishes observed front band behind and outside without occupancy",
          "[depth-space]") {
    const auto K_VIEW = DepthSpaceView::create(fixture(), { {}, .1 });
    REQUIRE(K_VIEW);
    CHECK_FALSE(K_VIEW->uncertainty_available());
    CHECK(K_VIEW->classify(K_CELL) == DepthCellRelation::FRONT_DIAGNOSTIC);
    CHECK(K_VIEW->classify({ { -.1, -.1, 3.9 }, { .1, .1, 4.1 } }) == DepthCellRelation::SURFACE_BAND);
    CHECK(K_VIEW->classify({ { -.1, -.1, 5.9 }, { .1, .1, 6.1 } }) == DepthCellRelation::BEHIND_SURFACE);
    CHECK(K_VIEW->classify({ { -.1, -.1, -.1 }, { .1, .1, .1 } }) == DepthCellRelation::OUTSIDE_VIEW);
    CHECK(K_VIEW->classify({ { 1.9, -.1, 1.8 }, { 2.1, .1, 2.2 } }) == DepthCellRelation::OUTSIDE_VIEW);
    CHECK_FALSE(K_VIEW->classify({ { 1, 0, 1 }, { 0, 1, 2 } }));
}

TEST_CASE("depth-space never uses center-only visibility or bridges unknown unsupported pixels",
          "[depth-space]") {
    const auto K_MODE = GENERATE(0, 1, 2);
    auto depth = fixture();
    // Off-center pixel in the complete enclosing footprint, not the center ray.
    constexpr std::size_t K_PIXEL { 7uz + 17uz * 4uz };
    if (K_MODE == 0) {
        depth.m_valid[K_PIXEL] = 0;
        depth.m_axial_depth[K_PIXEL] = depth.m_confidence[K_PIXEL] = 0;
        depth.m_support_count[K_PIXEL] = 0;
    } else if (K_MODE == 1) {
        depth.m_support_count[K_PIXEL] = 1;
    } else {
        depth.m_confidence[K_PIXEL] = 0;
    }
    const auto K_VIEW = DepthSpaceView::create(depth, { {}, .1 });
    REQUIRE(K_VIEW);
    CHECK(K_VIEW->classify(K_CELL) == DepthCellRelation::UNKNOWN_DEPTH);
}

TEST_CASE("depth-space retains sigma availability and scales the explicit world guard",
          "[depth-space]") {
    auto depth = fixture();
    depth.m_standard_deviation = std::vector<float>(221uz, 1);
    const auto K_VIEW = DepthSpaceView::create(depth, { {}, .1 });
    REQUIRE(K_VIEW);
    CHECK(K_VIEW->uncertainty_available());
    CHECK(K_VIEW->classify(K_CELL) == DepthCellRelation::SURFACE_BAND);
    const SimilarityTransform K_POSE { { 0, 0, 1, 0 }, { 7, -4, 8 }, 3 };
    const auto K_SCALED = DepthSpaceView::create(fixture(), { K_POSE, .3 });
    REQUIRE(K_SCALED);
    const auto K_A = transform_point(K_POSE, K_CELL.m_minimum),
               K_B = transform_point(K_POSE, K_CELL.m_maximum);
    CHECK(K_SCALED->classify(
              { { std::min(K_A.m_x, K_B.m_x), std::min(K_A.m_y, K_B.m_y), std::min(K_A.m_z, K_B.m_z) },
               { std::max(K_A.m_x, K_B.m_x),
                std::max(K_A.m_y, K_B.m_y),
                std::max(K_A.m_z, K_B.m_z) } }) == DepthCellRelation::FRONT_DIAGNOSTIC);
}

TEST_CASE("depth-space pyramid matches independent scalar pixel-ray oracle on odd grids and "
          "rotated cells",
          "[depth-space]") {
    const auto K_MODE = GENERATE(0, 1, 2, 3);
    auto depth = fixture();
    depth.m_standard_deviation = std::vector<float>(221uz, .03F);
    for (std::size_t pixel { 0uz }; pixel < 221uz; ++pixel) {
        depth.m_axial_depth[pixel] = 2.F + static_cast<float>(pixel % 11uz) * .3F;
        if (K_MODE == 1 && pixel % 37uz == 0uz) {
            depth.m_support_count[pixel] = 1;
        }
        if (K_MODE == 2 && pixel % 43uz == 0uz) {
            depth.m_valid[pixel] = 0;
            depth.m_support_count[pixel] = 0;
            depth.m_axial_depth[pixel] = depth.m_confidence[pixel] = 0;
            depth.m_standard_deviation->at(pixel) = 0;
        }
    }
    if (K_MODE == 3) {
        depth.m_standard_deviation.reset();
    }
    const DepthSpaceRecipe K_RECIPE {
        { { .9238795325112867, 0, .3826834323650898, 0 }, { 1, -2, .5 }, 1.7 }, .02 };
    const auto K_VIEW = DepthSpaceView::create(depth, K_RECIPE);
    REQUIRE(K_VIEW);
    for (std::size_t sample { 0uz }; sample < 1500uz; ++sample) {
        const Vec3d K_CENTER =
            transform_point(K_RECIPE.m_world_from_camera,
                            { (static_cast<double>(sample % 19uz) - 9.) * .19,
                             (static_cast<double>(sample / 19uz % 13uz) - 6.) * .17,
                             .1 + static_cast<double>(sample % 23uz) * .27 });
        const double K_RADIUS { .015 + static_cast<double>(sample % 7uz) * .045 };
        const Vec3d K_R { K_RADIUS, K_RADIUS, K_RADIUS };
        const Bounds3d K_BOUNDS { K_CENTER - K_R, K_CENTER + K_R };
        INFO(sample);
        CHECK(K_VIEW->classify(K_BOUNDS) == scalar(depth, K_RECIPE, K_BOUNDS));
    }
}

TEST_CASE("depth-space rejects invalid recipes depths and cancellation before using caller data",
          "[depth-space]") {
    auto depth = fixture();
    const auto K_BEFORE = encode_depth_asset(depth);
    REQUIRE(K_BEFORE);
    for (const double K_GUARD : { 0.,
                                 -1.,
                                 std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN() }) {
        CHECK_FALSE(DepthSpaceView::create(depth, { {}, K_GUARD }));
    }
    CHECK_FALSE(DepthSpaceView::create(depth, { { {}, {}, 0 }, .1 }));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = DepthSpaceView::create(depth, { {}, .1 }, cancelled.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    const auto K_AFTER = encode_depth_asset(depth);
    REQUIRE(K_AFTER);
    CHECK(*K_AFTER == *K_BEFORE);
    depth.m_axial_depth[0uz] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(DepthSpaceView::create(depth, { {}, .1 }));
}

TEST_CASE("depth-space application refuses stale pins existing outputs and cancelled admission",
          "[depth-space]") {
    Scratch scratch;
    const auto K_REQUEST = scratch.m_path / "request.json";
    constexpr std::string_view K_TEXT { "{}" };
    REQUIRE(asset_files::publish_asset(K_REQUEST, std::as_bytes(std::span(K_TEXT)), {}));
    const auto K_OUTPUT = scratch.m_path / "new";
    CHECK_FALSE(inspect_depth_space(K_REQUEST, std::string(64uz, 'a'), K_OUTPUT));
    CHECK_FALSE(std::filesystem::exists(K_OUTPUT));
    CHECK_FALSE(
        inspect_depth_space(K_REQUEST, sha256(std::as_bytes(std::span(K_TEXT))), scratch.m_path));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = inspect_depth_space(
        K_REQUEST, sha256(std::as_bytes(std::span(K_TEXT))), K_OUTPUT, cancelled.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK_FALSE(std::filesystem::exists(K_OUTPUT));
}

TEST_CASE("depth-space retains conflicting observations regardless of ordering and unknown views",
          "[depth-space]") {
    DepthCellEvidence evidence;
    CHECK(evidence.state() == DepthEvidenceState::UNKNOWN);
    evidence.m_counts[0uz] = 11uz;
    evidence.m_counts[1uz] = 7uz;
    CHECK(evidence.state() == DepthEvidenceState::UNKNOWN);
    evidence.m_counts[2uz] = 3uz;
    CHECK(evidence.state() == DepthEvidenceState::FRONT_ONLY);
    evidence.m_counts[4uz] = 1uz;
    CHECK(evidence.state() == DepthEvidenceState::CONFLICT);
    evidence.m_counts[2uz] = 0uz;
    CHECK(evidence.state() == DepthEvidenceState::SURFACE_OR_BEHIND);
    evidence.m_counts[4uz] = 0uz;
    evidence.m_counts[3uz] = 1uz;
    CHECK(evidence.state() == DepthEvidenceState::SURFACE_OR_BEHIND);
    evidence.m_counts[2uz] = 1uz;
    CHECK(evidence.state() == DepthEvidenceState::CONFLICT);
    CHECK(evidence.m_counts[0uz] == 11uz);
    CHECK(evidence.m_counts[1uz] == 7uz);
}
