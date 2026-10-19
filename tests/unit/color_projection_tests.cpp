#include "world_explorer/application/mesh_color_audit.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/image_decoder.hpp"
#include "world_explorer/scene_store/color_projection.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <limits>

using namespace world_explorer;

namespace {

DepthImageAsset depth_fixture() {
    DepthImageAsset depth;
    auto& metadata = depth.m_metadata;
    metadata.m_dependency = { { 1 }, { 2 }, { 3 }, { 4 }, std::string(64uz, 'a'), std::string(64uz, 'b') };
    metadata.m_width = metadata.m_height = 2;
    metadata.m_camera = { 2, 3, .5, .5 };
    metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
    metadata.m_recipe_sha256 = std::string(64uz, 'd');
    depth.m_axial_depth = { 2, 2, 2, 2 };
    depth.m_valid = { 1, 1, 1, 1 };
    depth.m_confidence = { 1, 1, 1, 1 };
    depth.m_support_count = { 2, 2, 2, 2 };
    return depth;
}

std::vector<std::byte> rgb_fixture() {
    return { std::byte { 255 },
            std::byte {},
            std::byte {},
            std::byte {},
            std::byte { 255 },
            std::byte {},
            std::byte {},
            std::byte {},
            std::byte { 255 },
            std::byte { 255 },
            std::byte { 255 },
            std::byte { 255 } };
}

struct Directory {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_color_projection_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Directory() {
        REQUIRE(std::filesystem::create_directory(m_root));
    }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }
};

}  // namespace

TEST_CASE("captured color uses integer centers optical axes and linear light interpolation",
          "[color_projection]") {
    const auto K_VIEW = ColorProjectionView::create(depth_fixture(), rgb_fixture(), {}, .01);
    REQUIRE(K_VIEW);
    const auto K_CENTER = K_VIEW->project_color({ 0, 0, 2 }, { 0, 0, 1 });
    REQUIRE(K_CENTER.m_status == ColorProjectionStatus::ACCEPTED);
    for (auto color : K_CENTER.m_linear_rgb) {
        CHECK(color == .5F);
    }
    CHECK(K_CENTER.m_score == 1);
    const auto K_RED = K_VIEW->project_color({ -.5, -1.0 / 3, 2 }, { 0, 0, -1 });
    REQUIRE(K_RED.m_status == ColorProjectionStatus::ACCEPTED);
    CHECK(K_RED.m_linear_rgb == std::array<float, 3> { 1, 0, 0 });
    const auto K_POINT = unproject(depth_fixture().m_metadata.m_camera, { .25, .75 }, 2);
    REQUIRE(K_POINT);
    const auto K_QUARTER = K_VIEW->project_color(*K_POINT, { 0, 0, 1 });
    REQUIRE(K_QUARTER.m_status == ColorProjectionStatus::ACCEPTED);
    CHECK(K_QUARTER.m_linear_rgb == std::array<float, 3> { .375F, .25F, .75F });
}

TEST_CASE("color projection transforms similarity scale before testing camera axial depth",
          "[color_projection]") {
    const SimilarityTransform K_CAMERA { { 0, 0, 1, 0 }, { 7, -4, 8 }, 3 };
    const auto K_VIEW = ColorProjectionView::create(depth_fixture(), rgb_fixture(), K_CAMERA, .03);
    REQUIRE(K_VIEW);
    const auto K_COLOR = K_VIEW->project_color(transform_point(K_CAMERA, { 0, 0, 2 }), { 0, 0, -1 });
    CHECK(K_COLOR.m_status == ColorProjectionStatus::ACCEPTED);
    CHECK(K_COLOR.m_linear_rgb == std::array<float, 3> { .5F, .5F, .5F });
}

TEST_CASE("color projection never bridges invalid low support or inconsistent depth footprints",
          "[color_projection]") {
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4);
    auto depth = depth_fixture();
    auto expected = ColorProjectionStatus::UNKNOWN_DEPTH;
    if (K_MUTATION == 0) {
        depth.m_valid[3] = 0;
        depth.m_axial_depth[3] = depth.m_confidence[3] = 0;
        depth.m_support_count[3] = 0;
    }
    if (K_MUTATION == 1) {
        depth.m_support_count[3] = 1;
        expected = ColorProjectionStatus::UNSUPPORTED_DEPTH;
    }
    if (K_MUTATION == 2) {
        depth.m_confidence[3] = 0;
        expected = ColorProjectionStatus::UNSUPPORTED_DEPTH;
    }
    if (K_MUTATION == 3) {
        depth.m_axial_depth[3] = 1;
        expected = ColorProjectionStatus::DEPTH_MISMATCH;
    }
    if (K_MUTATION == 4) {
        depth.m_axial_depth[3] = 3;
        expected = ColorProjectionStatus::DEPTH_MISMATCH;
    }
    const auto K_VIEW = ColorProjectionView::create(std::move(depth), rgb_fixture(), {}, .01);
    REQUIRE(K_VIEW);
    // Even a zero-weight corner must be known/supported: no edge exception.
    for (auto point : { Vec3d { 0, 0, 2 }, Vec3d { -.5, -1.0 / 3, 2 } }) {
        const auto K_COLOR = K_VIEW->project_color(point, { 0, 0, 1 });
        CHECK(K_COLOR.m_status == expected);
        CHECK(K_COLOR.m_linear_rgb == std::array<float, 3> {});
        CHECK(K_COLOR.m_score == 0);
    }
}

TEST_CASE("color projection rejects nonfinite behind border and grazing samples",
          "[color_projection]") {
    const auto K_VIEW = ColorProjectionView::create(depth_fixture(), rgb_fixture(), {}, .01);
    REQUIRE(K_VIEW);
    CHECK(K_VIEW->project_color({ 0, 0, 0 }, { 0, 0, 1 }).m_status ==
          ColorProjectionStatus::BEHIND_CAMERA);
    CHECK(K_VIEW->project_color({ .5, 0, 2 }, { 0, 0, 1 }).m_status ==
          ColorProjectionStatus::OUTSIDE_IMAGE);
    CHECK(K_VIEW->project_color({ 0, 1, 2 }, { 0, 0, 1 }).m_status ==
          ColorProjectionStatus::OUTSIDE_IMAGE);
    CHECK(K_VIEW->project_color({ 0, 0, 2 }, { 1, 0, 0 }).m_status == ColorProjectionStatus::GRAZING);
    CHECK(K_VIEW->project_color({ 0, 0, 2 }, {}).m_status == ColorProjectionStatus::INVALID_SAMPLE);
    CHECK(K_VIEW->project_color({ std::numeric_limits<double>::quiet_NaN(), 0, 2 }, { 0, 0, 1 })
              .m_status == ColorProjectionStatus::INVALID_SAMPLE);
}

TEST_CASE("color projection validates byte sizes depth metadata and cancellation before sampling",
          "[color_projection]") {
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4);
    auto depth = depth_fixture();
    auto rgb = rgb_fixture();
    double voxel { .01 };
    if (K_MUTATION == 0) {
        rgb.pop_back();
    }
    if (K_MUTATION == 1) {
        depth.m_valid[0] = 2;
    }
    if (K_MUTATION == 2) {
        voxel = 0;
    }
    if (K_MUTATION == 3) {
        voxel = std::numeric_limits<double>::infinity();
    }
    std::stop_source cancellation;
    if (K_MUTATION == 4) {
        cancellation.request_stop();
    }
    const auto K_REJECTED = ColorProjectionView::create(
        std::move(depth), std::move(rgb), {}, voxel, cancellation.get_token());
    CHECK(K_REJECTED.has_value() == false);
}

TEST_CASE("calibrated RGB decoder checks actual shape not merely byte count",
          "[color_projection][image_decoder]") {
    Directory directory;
    const auto K_IMAGE = directory.m_root / "image.ppm";
    std::string pixels = "P6\n2 2\n255\n";
    const auto K_RGB = rgb_fixture();
    pixels.append(reinterpret_cast<const char*>(K_RGB.data()), K_RGB.size());
    const auto K_SHA = asset_files::publish_asset(K_IMAGE, std::as_bytes(std::span(pixels)), {});
    REQUIRE(K_SHA);
    const auto K_DECODED = decode_calibrated_rgb(K_IMAGE, *K_SHA, 2, 2);
    REQUIRE(K_DECODED);
    CHECK(*K_DECODED == K_RGB);
    CHECK(decode_calibrated_rgb(K_IMAGE, *K_SHA, 4, 1).has_value() == false);
    CHECK(decode_calibrated_rgb(K_IMAGE, std::string(64uz, '0'), 2, 2).has_value() == false);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = decode_calibrated_rgb(K_IMAGE, *K_SHA, 2, 2, cancelled.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(*sha256_file(K_IMAGE, 1024uz) == *K_SHA);
}

TEST_CASE("color audit requests reject malformed data without creating output",
          "[color_projection]") {
    Directory directory;
    const auto K_TEXT = GENERATE(
        "{}", "{\"schema_version\":1,\"schema_version\":1}", "[]", "{\"schema_version\":true}");
    const auto K_PATH = directory.m_root / "request.json";
    const auto K_SHA =
        asset_files::publish_asset(K_PATH, std::as_bytes(std::span(std::string_view(K_TEXT))), {});
    REQUIRE(K_SHA);
    const auto K_OUTPUT = directory.m_root / "output";
    CHECK(audit_mesh_colors(K_PATH, *K_SHA, K_OUTPUT).has_value() == false);
    CHECK(std::filesystem::exists(K_OUTPUT) == false);
    CHECK(audit_mesh_colors(K_PATH, *K_SHA, directory.m_root).has_value() == false);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = audit_mesh_colors(K_PATH, *K_SHA, K_OUTPUT, cancelled.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("color projection preserves sRGB transfer and the fixed depth envelope",
          "[color_projection]") {
    auto rgb = std::vector<std::byte>(12uz, std::byte { 128 });
    const auto K_VIEW = ColorProjectionView::create(depth_fixture(), rgb, {}, .01);
    REQUIRE(K_VIEW);
    const auto K_COLOR = K_VIEW->project_color({ 0, 0, 2 }, { 0, 0, 1 });
    REQUIRE(K_COLOR.m_status == ColorProjectionStatus::ACCEPTED);
    for (auto color : K_COLOR.m_linear_rgb) {
        CHECK(std::abs(color - .21586050011389926) < 1e-7);
    }
    for (auto delta : { .039F, -.039F, .041F, -.041F }) {
        auto depth = depth_fixture();
        depth.m_axial_depth[0] += delta;
        const auto K_CHANGED = ColorProjectionView::create(std::move(depth), rgb, {}, .01);
        REQUIRE(K_CHANGED);
        const auto K_STATUS = K_CHANGED->project_color({ 0, 0, 2 }, { 0, 0, 1 }).m_status;
        CHECK(K_STATUS == (std::abs(delta) < .04F ? ColorProjectionStatus::ACCEPTED
                                                  : ColorProjectionStatus::DEPTH_MISMATCH));
    }
}
