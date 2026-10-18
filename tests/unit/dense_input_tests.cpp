#include "pose_fixture.hpp"
#include "training_camera_fixture.hpp"
#include "world_explorer/application/dense_inputs.hpp"
#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/application/pose_export.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/reconstruction_adapters/colmap_images.hpp"

#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

namespace {

template <std::size_t Count> void append(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::size_t i { 0uz }; i < Count; ++i)
        bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & std::uint64_t { 255 }));
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
    for (const auto K_VALUE : {1., 0., 0., 0., 1., 2., 3.}) number(bytes, K_VALUE);
    append<4>(bytes, 1);
    for (const auto K_CHARACTER : name) append<1>(bytes, static_cast<unsigned char>(K_CHARACTER));
    append<1>(bytes, 0);
    append<8>(bytes, 1);
    number(bytes, 12.25);
    number(bytes, 8.75);
    append<8>(bytes, UINT64_MAX);
    return bytes;
}

struct Fixture {
    test::pose_fixture m_poses;
    std::filesystem::path m_outputs = m_poses.root.string() + "_dense";
    std::filesystem::path m_images_file = m_poses.posed / "training/sparse/images.bin";
    DenseInputRequest m_request;

    Fixture() {
        std::filesystem::create_directory(m_outputs);
        std::filesystem::create_directories(m_images_file.parent_path());
        std::filesystem::create_directories(m_poses.posed / "training/images");
        test::write_training_camera(m_poses.posed / "training/sparse/cameras.bin",
                                    test::training_camera_bytes(1, 17, 13, {21, 29, 7.25, 4.75}));
        // Only the header is tested here: these explicitly synthetic images are
        // never offered as a decoded PNG or reconstruction quality fixture.
        std::vector<std::byte> png(33);
        const std::array<unsigned char, 29> K_HEADER {137, 80, 78, 71, 13, 10, 26, 10, 0, 0,
                                                      0,   13, 73, 72, 68, 82, 0,  0,  0, 17,
                                                      0,   0,  0,  13, 8,  2,  0,  0,  0};
        for (std::size_t i { 0uz }; i < K_HEADER.size(); ++i)
            png[i] = static_cast<std::byte>(K_HEADER[i]);
        std::vector<std::byte> images;
        append<8>(images, 3);
        for (int index { 0 }; index < 3; ++index) {
            test::write_training_camera(
                m_poses.posed / "training/images" / (std::to_string(index) + ".png"), png);
            // Set canonical metadata independently to the known model transform.
            auto& pose = m_poses.canonical["poses"][static_cast<std::size_t>(index)];
            pose["world_from_camera"] = { { "rotation_wxyz", { 0, 1, 0, 0 } },
                                         { "translation", { -1, 2, 3 } } };
            auto bytes = model_bytes(std::to_string(index) + ".png");
            bytes[8] = static_cast<std::byte>(index + 1);
            images.insert(images.end(), bytes.begin() + 8, bytes.end());
        }
        test::write_training_camera(m_images_file, images);
        test::write_training_camera(m_images_file.parent_path() / "points3D.bin",
                                    std::vector<std::byte>(8));
        m_poses.publish();
        m_request.m_completed = { m_poses.root, m_poses.source_digest };
        m_request.m_canonical_poses = m_poses.root / "canonical.weposes";
        m_request.m_depth_recipe = m_outputs / "recipe.json";
        std::ofstream(m_request.m_depth_recipe)
            << nlohmann::json {{"schema_version", 1},
                               {"input_profile", "colmap_native_dense_input_v1"},
                               {"depth_producer", "MULTIVIEW_STEREO"},
                               {"backend", "SYNTHETIC_DIAGNOSTIC_NOT_EXECUTED"}}
                   .dump();
        m_request.m_depth_recipe_sha256 = *sha256_file(m_request.m_depth_recipe, 65536);
        m_request.m_pose_sha256 =
            export_completed_pose_asset(
                {{m_poses.root, m_poses.root / "source.mp4", {}}, m_request.m_canonical_poses}, {})
                .value();
    }

    void inventory() { m_poses.inventory(m_poses.posed); }

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_outputs, ignored);
    }
};

}  // namespace

TEST_CASE("dense model reads native poses with canonical axes and quaternion order", "[dense]") {
    test::pose_fixture directory;
    const auto K_FILE = directory.root / "images.bin";
    const auto K_BYTES = model_bytes();
    test::write_training_camera(K_FILE, K_BYTES);
    const auto K_IMAGES = read_colmap_image_poses(K_FILE, sha256(K_BYTES));
    REQUIRE(K_IMAGES);
    REQUIRE(K_IMAGES->size() == 1);
    CHECK(K_IMAGES->front().m_image_id == 7);
    const auto K_POINT = transform_point(K_IMAGES->front().m_world_from_camera, {1, 2, 4});
    CHECK(K_POINT == Vec3d { 0, 0, -1 });
    CHECK(read_colmap_image_poses(K_FILE, std::string(64, 'a')).has_value() == false);
}

TEST_CASE("dense model rejects every truncated prefix trailing bytes and unsafe names", "[dense]") {
    test::pose_fixture directory;
    const auto K_FILE = directory.root / "images.bin";
    auto bytes = model_bytes();
    const auto K_ORIGINAL = bytes;
    for (std::size_t length { 0uz }; length < K_ORIGINAL.size(); ++length) {
        bytes.assign(K_ORIGINAL.begin(), K_ORIGINAL.begin() + static_cast<std::ptrdiff_t>(length));
        test::write_training_camera(K_FILE, bytes);
        REQUIRE(read_colmap_image_poses(K_FILE, sha256(bytes)).has_value() == false);
    }
    bytes = K_ORIGINAL;
    bytes.push_back(std::byte {});
    test::write_training_camera(K_FILE, bytes);
    CHECK(read_colmap_image_poses(K_FILE, sha256(bytes)).has_value() == false);
    for (const auto* name : {"../escape.png", "a/b", "a\\b", "C:bad", "", ".", ".."}) {
        bytes = model_bytes(name);
        test::write_training_camera(K_FILE, bytes);
        CHECK(read_colmap_image_poses(K_FILE, sha256(bytes)).has_value() == false);
    }
}

TEST_CASE("dense model bounds counts and rejects nonfinite tracks invalid rotations duplicates",
          "[dense]") {
    test::pose_fixture directory;
    const auto K_FILE = directory.root / "images.bin";
    auto bytes = model_bytes();
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4, 5);
    if (K_MODE == 0) bytes[0] = std::byte { 121 };
    if (K_MODE == 1) bytes[8] = std::byte { 0 };
    if (K_MODE == 2)  // zero-length quaternion
        for (std::size_t i { 12uz }; i < 44uz; ++i) bytes[i] = std::byte {};
    if (K_MODE == 3) {
        const auto K_NAN =
            std::bit_cast<std::array<std::byte, 8>>(std::numeric_limits<double>::quiet_NaN());
        std::copy(K_NAN.begin(), K_NAN.end(), bytes.end() - 24);
    }
    if (K_MODE == 4)
        for (auto i = bytes.size() - 32; i < bytes.size() - 24; ++i) bytes[i] = std::byte { 255 };
    if (K_MODE == 5) {
        const auto K_REPEATED = bytes;
        bytes[0] = std::byte { 2 };
        bytes.insert(bytes.end(), K_REPEATED.begin() + 8, K_REPEATED.end());
    }
    test::write_training_camera(K_FILE, bytes);
    CHECK(read_colmap_image_poses(K_FILE, sha256(bytes)).has_value() == false);
}

TEST_CASE("dense preparation independently binds canonical observations calibrated images and grid",
          "[dense]") {
    Fixture data;
    const auto K_PREPARED = prepare_dense_inputs(data.m_request);
    INFO((K_PREPARED ? "prepared" : K_PREPARED.error().m_message));
    REQUIRE(K_PREPARED);
    REQUIRE(K_PREPARED->m_frames.size() == 3);
    const auto& metadata = K_PREPARED->m_frames.front().m_expected_depth;
    CHECK(metadata.m_camera.m_fx == 21);
    CHECK(metadata.m_camera.m_fy == 29);
    CHECK(metadata.m_camera.m_cx == 6.75);
    CHECK(metadata.m_camera.m_cy == 4.25);
    CHECK(metadata.m_width == 17);
    CHECK(metadata.m_height == 13);
    CHECK(metadata.m_producer == DepthProducerKind::MULTIVIEW_STEREO);
    CHECK(metadata.m_dependency.m_pose_asset_sha256 == data.m_request.m_pose_sha256);
    CHECK(metadata.m_dependency.m_observation_image_sha256 != metadata.m_calibrated_image_sha256);
    const auto K_PIXEL = unproject(metadata.m_camera, {13, 2}, 4);
    REQUIRE(K_PIXEL);
    const auto K_PROJECTED = project(metadata.m_camera, *K_PIXEL);
    REQUIRE(K_PROJECTED);
    CHECK((*K_PROJECTED)[0] == 13);
    CHECK((*K_PROJECTED)[1] == 2);
    const auto K_MANIFEST = data.m_outputs / "dense.json";
    const auto K_PUBLISHED = write_dense_inputs(data.m_request, K_MANIFEST);
    REQUIRE(K_PUBLISHED);
    const auto K_LOADED = load_dense_inputs(data.m_request, K_MANIFEST, *K_PUBLISHED);
    REQUIRE(K_LOADED);
    CHECK(K_LOADED->m_manifest_json == K_PREPARED->m_manifest_json);
    CHECK(write_dense_inputs(data.m_request, K_MANIFEST).has_value() == false);
    CHECK(*sha256_file(K_MANIFEST, 1024uz * 1024) == *K_PUBLISHED);
    DepthImageAsset depth;
    depth.m_metadata = K_LOADED->m_frames.front().m_expected_depth;
    depth.m_axial_depth.resize(17uz * 13);
    depth.m_valid.resize(17uz * 13);
    depth.m_confidence.resize(17uz * 13);
    depth.m_support_count.resize(17uz * 13);
    const auto K_DEPTH_FILE = data.m_poses.root / "unknown.wedepth";
    const auto K_DIGEST = write_depth_asset(K_DEPTH_FILE, depth);
    REQUIRE(K_DIGEST);
    REQUIRE(load_pose_bound_depth({K_DEPTH_FILE,
                                   data.m_request.m_canonical_poses,
                                   *K_DIGEST,
                                   data.m_request.m_pose_sha256,
                                   depth.m_metadata}));
}

TEST_CASE("dense preparation rejects rehashed pose name camera and PNG grid mismatches",
          "[dense]") {
    Fixture data;
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    if (K_MODE < 3) {
        auto bytes = asset_files::read_bytes(data.m_images_file, 65536, {}).value();
        if (K_MODE == 0) bytes[68] = std::byte { 2 };    // wrong camera id
        if (K_MODE == 1) bytes[72] = std::byte { 'x' };  // unknown name
        if (K_MODE == 2) bytes[50] ^= std::byte { 64 };  // finite changed translation
        test::write_training_camera(data.m_images_file, bytes);
    } else {
        const auto K_FILE = data.m_poses.posed / "training/images/0.png";
        auto bytes = asset_files::read_bytes(K_FILE, 65536, {}).value();
        bytes[K_MODE == 3 ? 19 : 25] = std::byte { 1 };
        test::write_training_camera(K_FILE, bytes);
    }
    data.inventory();
    // Re-export binds the changed stage; this tests semantic checks after hashes pass.
    std::filesystem::remove(data.m_request.m_canonical_poses);
    data.m_request.m_pose_sha256 =
        export_completed_pose_asset({{data.m_poses.root, data.m_poses.root / "source.mp4", {}},
                                     data.m_request.m_canonical_poses},
                                    {})
            .value();
    CHECK(prepare_dense_inputs(data.m_request).has_value() == false);
}

TEST_CASE("dense manifests reject stale dependencies mutation overlap and cancellation",
          "[dense]") {
    Fixture data;
    const auto K_FILE = data.m_outputs / "dense.json";
    auto prepared = prepare_dense_inputs(data.m_request);
    INFO((prepared ? "prepared" : prepared.error().m_message));
    REQUIRE(prepared);
    auto bytes = prepared->m_manifest_json;
    bytes[0] = '[';
    std::ofstream(K_FILE, std::ios::binary) << bytes;
    CHECK(load_dense_inputs(data.m_request, K_FILE, sha256(std::as_bytes(std::span(bytes))))
              .error()
              .m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(write_dense_inputs(data.m_request, data.m_poses.posed / "dense.json").has_value() ==
          false);
    CHECK(std::filesystem::exists(data.m_poses.posed / "dense.json") == false);
    std::stop_source cancellation;
    cancellation.request_stop();
    CHECK(prepare_dense_inputs(data.m_request, cancellation.get_token()).error().m_code ==
          ErrorCode::CANCELLED);
    data.m_request.m_pose_sha256 = std::string(64, 'a');
    CHECK(prepare_dense_inputs(data.m_request).has_value() == false);
}

TEST_CASE("dense input preparation pins the complete selected depth recipe separately", "[dense]") {
    Fixture data;
    const auto K_MODE = GENERATE(0, 1, 2, 3);
    if (K_MODE == 0) {
        data.m_request.m_depth_recipe_sha256 = std::string(64, 'f');
    } else {
        const auto K_VALUE =
            K_MODE == 1 ? "[]"
            : K_MODE == 2
                ? R"({"schema_version":1,"input_profile":"resized","depth_producer":"MULTIVIEW_STEREO"})"
                : R"({"schema_version":1,"input_profile":"colmap_native_dense_input_v1","depth_producer":"SYNTHETIC"})";
        std::ofstream(data.m_request.m_depth_recipe) << K_VALUE;
        data.m_request.m_depth_recipe_sha256 = *sha256_file(data.m_request.m_depth_recipe, 65536);
    }
    CHECK(prepare_dense_inputs(data.m_request).has_value() == false);
    CHECK(write_dense_inputs(data.m_request, data.m_outputs / "new.json").has_value() == false);
    CHECK(std::filesystem::exists(data.m_outputs / "new.json") == false);
}

TEST_CASE("dense native grids require explicit depth dimension budgets without implicit resizing",
          "[dense]") {
    Fixture data;
    test::write_training_camera(data.m_poses.posed / "training/sparse/cameras.bin",
                                test::training_camera_bytes(1, 4104, 13, {21, 29, 7.25, 4.75}));
    for (int i { 0 }; i < 3; ++i) {
        const auto K_PATH = data.m_poses.posed / "training/images" / (std::to_string(i) + ".png");
        auto bytes = asset_files::read_bytes(K_PATH, 65536, {}).value();
        bytes[18] = std::byte { 16 };
        bytes[19] = std::byte { 8 };
        test::write_training_camera(K_PATH, bytes);
    }
    data.inventory();
    std::filesystem::remove(data.m_request.m_canonical_poses);
    data.m_request.m_pose_sha256 =
        export_completed_pose_asset({{data.m_poses.root, data.m_poses.root / "source.mp4", {}},
                                     data.m_request.m_canonical_poses},
                                    {})
            .value();
    const auto K_PREPARED = prepare_dense_inputs(data.m_request);
    REQUIRE(K_PREPARED);
    DepthImageAsset asset;
    asset.m_metadata = K_PREPARED->m_frames.front().m_expected_depth;
    CHECK(asset.m_metadata.m_width == 4104);
    const auto K_PIXELS = 4104uz * 13;
    asset.m_axial_depth.resize(K_PIXELS);
    asset.m_valid.resize(K_PIXELS);
    asset.m_confidence.resize(K_PIXELS);
    asset.m_support_count.resize(K_PIXELS);
    CHECK(validate_depth_asset(asset).has_value() == false);
    DepthAssetLimits explicit_limits;
    explicit_limits.m_max_dimension = 8192;
    CHECK(validate_depth_asset(asset, explicit_limits).has_value());
}

namespace {

struct ResizedFixture {
    Fixture m_native;
    std::filesystem::path m_root = m_native.m_outputs / "resized";
    std::filesystem::path m_manifest = m_root / "manifest.json";
    ResizedDenseInputRequest m_request;
    nlohmann::json m_json;

    ResizedFixture() {
        std::filesystem::create_directories(m_root / "training/images");
        m_request.m_native = m_native.m_request;
        m_request.m_depth_recipe = m_native.m_outputs / "resized-recipe.json";
        const nlohmann::json K_RESIZE {{"recipe", "colmap_resized_dense_input_v1"},
                                       {"crop", "none"},
                                       {"resize", "PILLOW_BILINEAR"},
                                       {"width", 11},
                                       {"height", 11},
                                       {"pixel_centers", "integer"},
                                       {"image_encoding", "PNG_RGB8"},
                                       {"pillow_version", "11.3.0"},
                                       {"worker_sha256", std::string(64uz, 'a')},
                                       {"environment_sha256", std::string(64uz, 'b')}};
        std::ofstream(m_request.m_depth_recipe) << nlohmann::json {
            {"schema_version", 1},
            {"input_profile", "colmap_resized_dense_input_v1"},
            {"depth_producer", "MULTIVIEW_STEREO"},
            {"resize", K_RESIZE}}.dump();
        m_request.m_depth_recipe_sha256 = *sha256_file(m_request.m_depth_recipe, 65536uz);
        const auto K_NATIVE = prepare_dense_inputs(m_native.m_request).value();
        m_json = nlohmann::json::parse(K_NATIVE.m_manifest_json);
        m_json["kind"] = "DENSE_INPUTS_RESIZED_GRID";
        m_json["native_manifest_sha256"] =
            sha256(std::as_bytes(std::span(K_NATIVE.m_manifest_json)));
        m_json["preparation_recipe"] = K_RESIZE;
        const auto K_BYTES = K_RESIZE.dump();
        m_json["preparation_recipe_sha256"] = sha256(std::as_bytes(std::span(K_BYTES)));
        m_json["depth_recipe_sha256"] = m_request.m_depth_recipe_sha256;
        for (auto& frame : m_json["frames"]) {
            const auto K_NAME = frame["image"].get<std::string>();
            auto png =
                asset_files::read_bytes(m_native.m_poses.posed / K_NAME, 65536uz, {}).value();
            png[19uz] = std::byte { 11 };
            png[23uz] = std::byte { 11 };
            test::write_training_camera(m_root / K_NAME, png);
            frame["native_image_sha256"] = frame["calibrated_image_sha256"];
            frame["calibrated_image_sha256"] = sha256(png);
            frame["width"] = 11;
            frame["height"] = 11;
            // Independent known fixture calibration in COLMAP half-pixel coordinates.
            frame["intrinsics"] = {
                11. / 17. * 21., 11. / 13. * 29., 11. / 17. * 7.25 - .5, 11. / 13. * 4.75 - .5};
        }
    }

    Result<PreparedDenseInputs> load(const std::stop_token& stop = {}) {
        const auto K_BYTES = m_json.dump();
        std::ofstream(m_manifest) << K_BYTES;
        return load_resized_dense_inputs(
            m_request, m_manifest, sha256(std::as_bytes(std::span(K_BYTES))), stop);
    }
};

}  // namespace

TEST_CASE("resized dense admission binds unequal axis calibration and independent depth metadata",
          "[dense][resize]") {
    ResizedFixture data;
    const auto K_PREPARED = data.load();
    INFO((K_PREPARED ? "prepared" : K_PREPARED.error().m_message));
    REQUIRE(K_PREPARED);
    const auto& metadata = K_PREPARED->m_frames.front().m_expected_depth;
    CHECK(metadata.m_width == 11);
    CHECK(metadata.m_height == 11);
    CHECK(std::abs(metadata.m_camera.m_cx - (7.25 * 11. / 17. - .5)) < 1e-12);
    CHECK(std::abs(metadata.m_camera.m_cy - (4.75 * 11. / 13. - .5)) < 1e-12);
    CHECK(metadata.m_calibrated_image_sha256 !=
          data.m_json["frames"][0uz]["native_image_sha256"].get<std::string>());
    const auto K_NATIVE = prepare_dense_inputs(data.m_native.m_request).value();
    CHECK(metadata.m_dependency == K_NATIVE.m_frames.front().m_expected_depth.m_dependency);
    DepthImageAsset depth;
    depth.m_metadata = metadata;
    depth.m_axial_depth.resize(121uz);
    depth.m_valid.resize(121uz);
    depth.m_confidence.resize(121uz);
    depth.m_support_count.resize(121uz);
    CHECK(admit_depth_asset(depth, metadata).has_value());
    CHECK(admit_depth_asset(depth, K_NATIVE.m_frames.front().m_expected_depth).has_value() ==
          false);
    CHECK(std::filesystem::exists(K_PREPARED->m_frames.front().m_calibrated_image));
}

TEST_CASE("resized dense admission rejects rehashed dependency calibration and inventory forgery",
          "[dense][resize]") {
    ResizedFixture data;
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    if (K_MODE == 0) data.m_json["frames"][0uz]["intrinsics"][2uz] = 6.75 * 11. / 17.;
    if (K_MODE == 1) data.m_json["frames"][0uz]["native_image_sha256"] = std::string(64uz, 'f');
    if (K_MODE == 2) data.m_json["frames"][0uz]["world_from_camera"]["translation"][0uz] = 9;
    if (K_MODE == 3)
        data.m_json["frames"][0uz]["observation_image_sha256"] = std::string(64uz, 'f');
    if (K_MODE == 4) data.m_json["frames"][0uz]["image"] = "../escape.png";
    if (K_MODE == 5) data.m_json["native_manifest_sha256"] = std::string(64uz, 'f');
    if (K_MODE == 6) data.m_json["frames"].erase(0uz);
    if (K_MODE == 7) data.m_json["preparation_recipe"]["width"] = 12;
    if (K_MODE == 8) data.m_json["depth_recipe_sha256"] = std::string(64uz, 'f');
    CHECK(data.load().has_value() == false);
}

TEST_CASE("resized dense admission rejects changed PNG bytes headers links and cancellation",
          "[dense][resize]") {
    ResizedFixture data;
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    const auto K_PATH = data.m_root / "training/images/0.png";
    auto bytes = asset_files::read_bytes(K_PATH, 65536uz, {}).value();
    if (K_MODE == 0) bytes[32uz] = std::byte { 1 };
    if (K_MODE == 1) bytes[19uz] = std::byte { 12 };
    test::write_training_camera(K_PATH, bytes);
    if (K_MODE == 1) data.m_json["frames"][0uz]["calibrated_image_sha256"] = sha256(bytes);
    if (K_MODE == 2) {
#ifndef _WIN32
        std::filesystem::remove(K_PATH);
        std::filesystem::create_symlink(data.m_native.m_poses.posed / "training/images/0.png",
                                        K_PATH);
#else
        return;
#endif
    }
    std::stop_source cancellation;
    if (K_MODE == 3) cancellation.request_stop();
    if (K_MODE == 4) data.m_request.m_native.m_pose_sha256 = std::string(64uz, 'f');
    CHECK(data.load(cancellation.get_token()).has_value() == false);
}

TEST_CASE("resized dense admission rejects duplicate JSON keys and unsupported resize recipes",
          "[dense][resize]") {
    ResizedFixture data;
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    if (K_MODE == 0) {
        auto bytes = data.m_json.dump();
        bytes.insert(1uz, "\"kind\":\"ignored\",");
        std::ofstream(data.m_manifest) << bytes;
        CHECK(load_resized_dense_inputs(
                  data.m_request, data.m_manifest, sha256(std::as_bytes(std::span(bytes))))
                  .has_value() == false);
    } else {
        auto recipe = nlohmann::json::parse(std::ifstream(data.m_request.m_depth_recipe));
        if (K_MODE == 1) recipe["resize"]["width"] = 8193;
        if (K_MODE == 2) recipe["resize"]["width"] = -1;
        if (K_MODE == 3) recipe["resize"]["height"] = true;
        if (K_MODE == 4) recipe["resize"]["resize"] = "IMPLICIT_BACKEND_RESIZE";
        std::ofstream(data.m_request.m_depth_recipe) << recipe.dump();
        data.m_request.m_depth_recipe_sha256 = *sha256_file(data.m_request.m_depth_recipe, 65536uz);
        CHECK(data.load().has_value() == false);
    }
}
