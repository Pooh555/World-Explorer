#include "pose_fixture.hpp"
#include "training_camera_fixture.hpp"
#include "world_explorer/application/dense_inputs.hpp"
#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/pose_export.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/reconstruction_adapters/colmap_images.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>

using namespace world_explorer;
namespace {
template <std::size_t count> void append(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::size_t i = 0; i < count; ++i)
        bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & std::uint64_t{255}));
}
void number(std::vector<std::byte>& bytes, double value) {
    append<8>(bytes, std::bit_cast<std::uint64_t>(value));
}
std::vector<std::byte> model_bytes(const std::string& name = "0.png") {
    // Independent asymmetric native cam_from_world: R=identity, t=(1,2,3).
    // Its canonical world_from_camera has q=(0,1,0,0), center=(-1,2,3).
    std::vector<std::byte> bytes;
    append<8>(bytes, 1);
    append<4>(bytes, 7);
    for (const auto value : {1., 0., 0., 0., 1., 2., 3.})
        number(bytes, value);
    append<4>(bytes, 1);
    for (const auto character : name)
        append<1>(bytes, static_cast<unsigned char>(character));
    append<1>(bytes, 0);
    append<8>(bytes, 1);
    number(bytes, 12.25);
    number(bytes, 8.75);
    append<8>(bytes, UINT64_MAX);
    return bytes;
}
struct fixture {
    test::pose_fixture poses;
    std::filesystem::path outputs = poses.root.string() + "_dense";
    std::filesystem::path images_file = poses.posed / "training/sparse/images.bin";
    dense_input_request request;
    fixture() {
        std::filesystem::create_directory(outputs);
        std::filesystem::create_directories(images_file.parent_path());
        std::filesystem::create_directories(poses.posed / "training/images");
        test::write_training_camera(poses.posed / "training/sparse/cameras.bin",
                                    test::training_camera_bytes(1, 17, 13, {21, 29, 7.25, 4.75}));
        // Only the header is tested here: these explicitly synthetic images are
        // never offered as a decoded PNG or reconstruction quality fixture.
        std::vector<std::byte> png(33);
        const std::array<unsigned char, 29> header{137, 80, 78, 71, 13, 10, 26, 10, 0, 0,
                                                   0,   13, 73, 72, 68, 82, 0,  0,  0, 17,
                                                   0,   0,  0,  13, 8,  2,  0,  0,  0};
        for (std::size_t i = 0; i < header.size(); ++i)
            png[i] = static_cast<std::byte>(header[i]);
        std::vector<std::byte> images;
        append<8>(images, 3);
        for (int index = 0; index < 3; ++index) {
            test::write_training_camera(
                poses.posed / "training/images" / (std::to_string(index) + ".png"), png);
            // Set canonical metadata independently to the known model transform.
            auto& pose = poses.canonical["poses"][static_cast<std::size_t>(index)];
            pose["world_from_camera"] = {{"rotation_wxyz", {0, 1, 0, 0}},
                                         {"translation", {-1, 2, 3}}};
            auto bytes = model_bytes(std::to_string(index) + ".png");
            bytes[8] = static_cast<std::byte>(index + 1);
            images.insert(images.end(), bytes.begin() + 8, bytes.end());
        }
        test::write_training_camera(images_file, images);
        test::write_training_camera(images_file.parent_path() / "points3D.bin",
                                    std::vector<std::byte>(8));
        poses.publish();
        request.completed = {poses.root, poses.source_digest};
        request.canonical_poses = poses.root / "canonical.weposes";
        request.depth_recipe = outputs / "recipe.json";
        std::ofstream(request.depth_recipe)
            << nlohmann::json{{"schema_version", 1},
                              {"input_profile", "colmap_native_dense_input_v1"},
                              {"depth_producer", "MULTIVIEW_STEREO"},
                              {"backend", "SYNTHETIC_DIAGNOSTIC_NOT_EXECUTED"}}
                   .dump();
        request.depth_recipe_sha256 = *sha256_file(request.depth_recipe, 65536);
        request.pose_sha256 =
            export_completed_pose_asset(
                {{poses.root, poses.root / "source.mp4", {}}, request.canonical_poses}, {})
                .value();
    }
    void inventory() { poses.inventory(poses.posed); }
    ~fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(outputs, ignored);
    }
};
} // namespace

TEST_CASE("dense model reads native poses with canonical axes and quaternion order", "[dense]") {
    test::pose_fixture directory;
    const auto file = directory.root / "images.bin";
    const auto bytes = model_bytes();
    test::write_training_camera(file, bytes);
    const auto images = read_colmap_image_poses(file, sha256(bytes));
    REQUIRE(images);
    REQUIRE(images->size() == 1);
    CHECK(images->front().image_id == 7);
    const auto point = transform_point(images->front().world_from_camera, {1, 2, 4});
    CHECK(point == vec3d{0, 0, -1});
    CHECK(read_colmap_image_poses(file, std::string(64, 'a')).has_value() == false);
}

TEST_CASE("dense model rejects every truncated prefix trailing bytes and unsafe names", "[dense]") {
    test::pose_fixture directory;
    const auto file = directory.root / "images.bin";
    auto bytes = model_bytes();
    const auto original = bytes;
    for (std::size_t length = 0; length < original.size(); ++length) {
        bytes.assign(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(length));
        test::write_training_camera(file, bytes);
        REQUIRE(read_colmap_image_poses(file, sha256(bytes)).has_value() == false);
    }
    bytes = original;
    bytes.push_back(std::byte{});
    test::write_training_camera(file, bytes);
    CHECK(read_colmap_image_poses(file, sha256(bytes)).has_value() == false);
    for (const auto* name : {"../escape.png", "a/b", "a\\b", "C:bad", "", ".", ".."}) {
        bytes = model_bytes(name);
        test::write_training_camera(file, bytes);
        CHECK(read_colmap_image_poses(file, sha256(bytes)).has_value() == false);
    }
}

TEST_CASE("dense model bounds counts and rejects nonfinite tracks invalid rotations duplicates",
          "[dense]") {
    test::pose_fixture directory;
    const auto file = directory.root / "images.bin";
    auto bytes = model_bytes();
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5);
    if (mode == 0)
        bytes[0] = std::byte{121};
    if (mode == 1)
        bytes[8] = std::byte{0};
    if (mode == 2) // zero-length quaternion
        for (std::size_t i = 12; i < 44; ++i)
            bytes[i] = std::byte{};
    if (mode == 3) {
        const auto nan =
            std::bit_cast<std::array<std::byte, 8>>(std::numeric_limits<double>::quiet_NaN());
        std::copy(nan.begin(), nan.end(), bytes.end() - 24);
    }
    if (mode == 4)
        for (auto i = bytes.size() - 32; i < bytes.size() - 24; ++i)
            bytes[i] = std::byte{255};
    if (mode == 5) {
        const auto repeated = bytes;
        bytes[0] = std::byte{2};
        bytes.insert(bytes.end(), repeated.begin() + 8, repeated.end());
    }
    test::write_training_camera(file, bytes);
    CHECK(read_colmap_image_poses(file, sha256(bytes)).has_value() == false);
}

TEST_CASE("dense preparation independently binds canonical observations calibrated images and grid",
          "[dense]") {
    fixture data;
    const auto prepared = prepare_dense_inputs(data.request);
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    REQUIRE(prepared->frames.size() == 3);
    const auto& metadata = prepared->frames.front().expected_depth;
    CHECK(metadata.camera.fx == 21);
    CHECK(metadata.camera.fy == 29);
    CHECK(metadata.camera.cx == 6.75);
    CHECK(metadata.camera.cy == 4.25);
    CHECK(metadata.width == 17);
    CHECK(metadata.height == 13);
    CHECK(metadata.producer == depth_producer_kind::MULTIVIEW_STEREO);
    CHECK(metadata.dependency.pose_asset_sha256 == data.request.pose_sha256);
    CHECK(metadata.dependency.observation_image_sha256 != metadata.calibrated_image_sha256);
    const auto pixel = unproject(metadata.camera, {13, 2}, 4);
    REQUIRE(pixel);
    const auto projected = project(metadata.camera, *pixel);
    REQUIRE(projected);
    CHECK((*projected)[0] == 13);
    CHECK((*projected)[1] == 2);
    const auto manifest = data.outputs / "dense.json";
    const auto published = write_dense_inputs(data.request, manifest);
    REQUIRE(published);
    const auto loaded = load_dense_inputs(data.request, manifest, *published);
    REQUIRE(loaded);
    CHECK(loaded->manifest_json == prepared->manifest_json);
    CHECK(write_dense_inputs(data.request, manifest).has_value() == false);
    CHECK(*sha256_file(manifest, std::size_t{1024} * 1024) == *published);
    depth_image_asset depth;
    depth.metadata = loaded->frames.front().expected_depth;
    depth.axial_depth.resize(std::size_t{17} * 13);
    depth.valid.resize(std::size_t{17} * 13);
    depth.confidence.resize(std::size_t{17} * 13);
    depth.support_count.resize(std::size_t{17} * 13);
    const auto depth_file = data.poses.root / "unknown.wedepth";
    const auto digest = write_depth_asset(depth_file, depth);
    REQUIRE(digest);
    REQUIRE(load_pose_bound_depth({depth_file, data.request.canonical_poses, *digest,
                                   data.request.pose_sha256, depth.metadata}));
}

TEST_CASE("dense preparation rejects rehashed pose name camera and PNG grid mismatches",
          "[dense]") {
    fixture data;
    const auto mode = GENERATE(0, 1, 2, 3, 4);
    if (mode < 3) {
        auto bytes = asset_files::read_bytes(data.images_file, 65536, {}).value();
        if (mode == 0)
            bytes[68] = std::byte{2}; // wrong camera id
        if (mode == 1)
            bytes[72] = std::byte{'x'}; // unknown name
        if (mode == 2)
            bytes[50] ^= std::byte{64}; // finite changed translation
        test::write_training_camera(data.images_file, bytes);
    } else {
        const auto file = data.poses.posed / "training/images/0.png";
        auto bytes = asset_files::read_bytes(file, 65536, {}).value();
        bytes[mode == 3 ? 19 : 25] = std::byte{1};
        test::write_training_camera(file, bytes);
    }
    data.inventory();
    // Re-export binds the changed stage; this tests semantic checks after hashes pass.
    std::filesystem::remove(data.request.canonical_poses);
    data.request.pose_sha256 =
        export_completed_pose_asset(
            {{data.poses.root, data.poses.root / "source.mp4", {}}, data.request.canonical_poses},
            {})
            .value();
    CHECK(prepare_dense_inputs(data.request).has_value() == false);
}

TEST_CASE("dense manifests reject stale dependencies mutation overlap and cancellation",
          "[dense]") {
    fixture data;
    const auto file = data.outputs / "dense.json";
    auto prepared = prepare_dense_inputs(data.request);
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    auto bytes = prepared->manifest_json;
    bytes[0] = '[';
    std::ofstream(file, std::ios::binary) << bytes;
    CHECK(load_dense_inputs(data.request, file, sha256(std::as_bytes(std::span(bytes))))
              .error()
              .code == error_code::RESYNC_REQUIRED);
    CHECK(write_dense_inputs(data.request, data.poses.posed / "dense.json").has_value() == false);
    CHECK(std::filesystem::exists(data.poses.posed / "dense.json") == false);
    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK(prepare_dense_inputs(data.request, cancellation.get_token()).error().code ==
          error_code::CANCELLED);
    data.request.pose_sha256 = std::string(64, 'a');
    CHECK(prepare_dense_inputs(data.request).has_value() == false);
}

TEST_CASE("dense input preparation pins the complete selected depth recipe separately", "[dense]") {
    fixture data;
    const auto mode = GENERATE(0, 1, 2, 3);
    if (mode == 0) {
        data.request.depth_recipe_sha256 = std::string(64, 'f');
    } else {
        const auto value =
            mode == 1 ? "[]"
            : mode == 2
                ? R"({"schema_version":1,"input_profile":"resized","depth_producer":"MULTIVIEW_STEREO"})"
                : R"({"schema_version":1,"input_profile":"colmap_native_dense_input_v1","depth_producer":"SYNTHETIC"})";
        std::ofstream(data.request.depth_recipe) << value;
        data.request.depth_recipe_sha256 = *sha256_file(data.request.depth_recipe, 65536);
    }
    CHECK(prepare_dense_inputs(data.request).has_value() == false);
    CHECK(write_dense_inputs(data.request, data.outputs / "new.json").has_value() == false);
    CHECK(std::filesystem::exists(data.outputs / "new.json") == false);
}

TEST_CASE("dense native grids require explicit depth dimension budgets without implicit resizing",
          "[dense]") {
    fixture data;
    test::write_training_camera(data.poses.posed / "training/sparse/cameras.bin",
                                test::training_camera_bytes(1, 4104, 13, {21, 29, 7.25, 4.75}));
    for (int i = 0; i < 3; ++i) {
        const auto path = data.poses.posed / "training/images" / (std::to_string(i) + ".png");
        auto bytes = asset_files::read_bytes(path, 65536, {}).value();
        bytes[18] = std::byte{16};
        bytes[19] = std::byte{8};
        test::write_training_camera(path, bytes);
    }
    data.inventory();
    std::filesystem::remove(data.request.canonical_poses);
    data.request.pose_sha256 =
        export_completed_pose_asset(
            {{data.poses.root, data.poses.root / "source.mp4", {}}, data.request.canonical_poses},
            {})
            .value();
    const auto prepared = prepare_dense_inputs(data.request);
    REQUIRE(prepared);
    depth_image_asset asset;
    asset.metadata = prepared->frames.front().expected_depth;
    CHECK(asset.metadata.width == 4104);
    const auto pixels = std::size_t{4104} * 13;
    asset.axial_depth.resize(pixels);
    asset.valid.resize(pixels);
    asset.confidence.resize(pixels);
    asset.support_count.resize(pixels);
    CHECK(validate_depth_asset(asset).has_value() == false);
    depth_asset_limits explicit_limits;
    explicit_limits.max_dimension = 8192;
    CHECK(validate_depth_asset(asset, explicit_limits).has_value());
}
