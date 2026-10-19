#include "pose_fixture.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>
#include <limits>
#include <map>
#include <string_view>

using namespace world_explorer;

namespace {

using test::path_utf8;
using test::pose_fixture;

nlohmann::json read_metadata(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input);
    return nlohmann::json::parse(input);
}

void write_text(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE(output);
}

void write_metadata(const std::filesystem::path& path, const nlohmann::json& value) {
    write_text(path, value.dump());
}

std::map<std::string, std::string> fixture_tree(const std::filesystem::path& root) {
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = entry.path().lexically_relative(root).generic_string();
        CAPTURE(K_NAME);
        const auto K_STATUS = entry.symlink_status();
        if (std::filesystem::is_directory(K_STATUS)) {
            result.emplace(K_NAME, "directory");
        } else {
            REQUIRE(std::filesystem::is_regular_file(K_STATUS));
            REQUIRE(entry.file_size() <= 16uz * 1024 * 1024);
            std::ifstream input(entry.path(), std::ios::binary);
            REQUIRE(input);
            std::string bytes { std::istreambuf_iterator<char>(input), {} };
            REQUIRE(!input.bad());
            result.emplace(K_NAME, "file:" + bytes);
        }
    }
    return result;
}

void restore_fixture_files(const pose_fixture& fixture,
                           const std::map<std::string, std::string>& original) {
    for (const auto& [K_NAME, K_VALUE] : original) {
        if (K_VALUE.starts_with("file:")) {
            write_text(fixture.root / K_NAME, std::string_view(K_VALUE).substr(5));
        }
    }
}

std::vector<std::byte> dataset_bytes(const CompletedPoseDataset& dataset) {
    MapStore map;
    MapTransaction tx;
    tx.m_operation = { 1 };
    tx.m_new_revision = dataset.m_revision;
    tx.m_epoch = dataset.m_epoch;
    tx.m_calibrations = dataset.m_calibrations;
    tx.m_observations = dataset.m_observations;
    tx.m_poses = dataset.m_poses;
    tx.m_alignments = { { dataset.m_submap, dataset.m_revision, {} } };
    REQUIRE(map.commit(tx));
    const auto K_ENCODED = encode_pose_asset({ map.acquire_snapshot(),
                                              { dataset.m_source_sha256,
                                               dataset.m_decode_completion_sha256,
                                               dataset.m_pose_completion_sha256,
                                               dataset.m_journal_sha256 } });
    REQUIRE(K_ENCODED);
    return *K_ENCODED;
}

void extend_selection(pose_fixture& fixture, std::size_t count) {
    if (count == 3) {
        return;
    }
    REQUIRE(count >= 3);
    REQUIRE(count <= 120);
    auto decoded = read_metadata(fixture.decoded / "result.json");
    auto frames = nlohmann::json::array();
    auto poses = nlohmann::json::array();
    for (std::size_t index { 0uz }; index < count; ++index) {
        const auto K_FILE = "images/" + std::to_string(index) + ".png";
        if (index >= 3) {
            REQUIRE(std::filesystem::copy_file(fixture.decoded / "images/0.png",
                                               fixture.decoded / K_FILE));
        }
        auto frame = decoded.at("frames")[0];
        frame["file"] = K_FILE;
        frame["frame_id"] = fixture.source_digest.substr(0, 16) + "-" + std::to_string(index * 10);
        frame["source_index"] = index * 10;
        frame["pts"] = index * 10;
        frame["capture_time_ns"] = static_cast<std::int64_t>(index) * 1000000000 / 3;
        frames.push_back(frame);
        auto pose = fixture.canonical.at("poses")[0];
        pose["image_id"] = index + 1;
        pose["backend_rig_frame_id"] = index + 1;
        pose["frame_id"] = frame["frame_id"];
        pose["pts"] = index * 10;
        poses.push_back(pose);
    }
    decoded["source_frame_count"] = (count - 1) * 10 + 1;
    decoded["frames"] = frames;
    write_metadata(fixture.decoded / "result.json", decoded);
    fixture.canonical["registered_frames"] = count;
    fixture.canonical["poses"] = poses;
    auto result = read_metadata(fixture.posed / "result.json");
    result["registered_frames"] = count;
    write_metadata(fixture.posed / "result.json", result);
    fixture.publish();
}

template <class Mutation>
void reject_import_and_repair(ErrorCode expected,
                              Mutation mutate,
                              std::size_t count = 3,
                              const std::stop_token& stop = {}) {
    pose_fixture fixture;
    extend_selection(fixture, count);
    write_text(fixture.root / "caller.txt", "unrelated caller data");
    const auto K_RETAINED = fixture.load();
    REQUIRE(K_RETAINED);
    const auto K_ORIGINAL_BYTES = dataset_bytes(**K_RETAINED);
    const auto K_ORIGINAL = fixture_tree(fixture.root);
    CompletedPoseConfig config { fixture.root, fixture.source_digest };
    mutate(fixture, config);
    const auto K_BEFORE = fixture_tree(fixture.root);
    const auto K_REJECTED = load_completed_poses(config, stop);
    INFO((K_REJECTED ? "unexpected import" : K_REJECTED.error().m_message));
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == expected);
    CHECK(fixture_tree(fixture.root) == K_BEFORE);
    CHECK(dataset_bytes(**K_RETAINED) == K_ORIGINAL_BYTES);
    // Explicitly repair only these owned fixture files; the importer never repairs them.
    restore_fixture_files(fixture, K_ORIGINAL);
    CHECK(fixture_tree(fixture.root) == K_ORIGINAL);
    const auto K_REPAIRED = fixture.load();
    REQUIRE(K_REPAIRED);
    CHECK(dataset_bytes(**K_REPAIRED) == K_ORIGINAL_BYTES);
    CHECK(fixture_tree(fixture.root) == K_ORIGINAL);
}

}  // namespace

TEST_CASE("pose import rejects invalid source configuration and pre-cancelled work without writes",
          "[pose_import_admission][config]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    std::stop_source cancellation;
    if (K_FIELD >= 4) {
        cancellation.request_stop();
    }
    reject_import_and_repair(
        K_FIELD >= 4   ? ErrorCode::CANCELLED
        : K_FIELD == 3 ? ErrorCode::INVALID_DATA
                       : ErrorCode::INVALID_ARGUMENT,
        [K_FIELD](const pose_fixture& fixture, CompletedPoseConfig& config) {
            if (K_FIELD == 0 || K_FIELD == 5) {
                config.m_source_sha256.clear();
            } else if (K_FIELD == 1) {
                config.m_source_sha256.pop_back();
            } else if (K_FIELD == 2) {
                config.m_source_sha256[0] = 'G';
            } else if (K_FIELD == 3) {
                config.m_pose_job = fixture.root / "missing-job";
            }
        },
        3,
        cancellation.get_token());
}

TEST_CASE("pose job completion rejects malformed bounded metadata and mismatched output",
          "[pose_import_admission][completion]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    CAPTURE(K_FIELD);
    reject_import_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](const pose_fixture& fixture, CompletedPoseConfig&) {
            const auto K_PATH = fixture.root / "result.json";
            auto value = read_metadata(K_PATH);
            switch (K_FIELD) {
            case 0:
                value["schema_version"] = 2;
                break;
            case 1:
                value["status"] = "cancelled";
                break;
            case 2:
                value["stage"] = "fit";
                break;
            case 3:
                value["source_sha256"] = std::string(64, 'e');
                break;
            case 4:
                value["output"] = std::string(4097, 'x');
                break;
            case 5:
                value["output"] = path_utf8(fixture.posed) + std::string(1, '\0');
                break;
            case 6:
                value["output"] = path_utf8(fixture.decoded);
                break;
            case 7:
                value["output"] = 17;
                break;
            case 8:
                value.erase("output");
                break;
            case 9:
                REQUIRE(std::filesystem::remove(K_PATH));
                return;
            case 10:
                write_text(K_PATH, "{");
                return;
            case 11:
                write_text(K_PATH, std::string(1024 * 1024 + 1, ' '));
                return;
            case 12:
                write_text(K_PATH, std::string(34, '[') + "0" + std::string(34, ']'));
                return;
            case 13:
                value["journal_sha256"] = std::string(64, 'e');
                break;
            default:
                FAIL("unknown completion mutation");
            }
            write_metadata(K_PATH, value);
        });
}

TEST_CASE("pose and decode pointers reject invalid keys strings and escaping committed paths",
          "[pose_import_admission][pointers]") {
    const auto K_POSE_POINTER = GENERATE(false, true);
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(K_POSE_POINTER, K_FIELD);
    reject_import_and_repair(
        ErrorCode::INVALID_DATA,
        [K_POSE_POINTER, K_FIELD](const pose_fixture& fixture, CompletedPoseConfig&) {
            const auto K_PATH = fixture.root / (K_POSE_POINTER ? "poses.json" : "decode.json");
            auto value = read_metadata(K_PATH);
            switch (K_FIELD) {
            case 0:
                value["key"] = "short";
                break;
            case 1:
                value["key"] = std::string(64, 'G');
                break;
            case 2:
                value["output"] = "";
                break;
            case 3:
                value["output"] = std::string(4097, 'x');
                break;
            case 4:
                value["output"] = value.at("output").get<std::string>() + std::string(1, '\0');
                break;
            case 5:
                value["output"] = path_utf8(fixture.root);
                break;
            case 6:
                value["key"] = std::string(64, 'e');
                break;
            case 7:
                value["output"] = path_utf8(fixture.root / "missing-stage");
                break;
            default:
                FAIL("unknown pointer mutation");
            }
            write_metadata(K_PATH, value);
        });
}

TEST_CASE("explicit decode selection rejects invalid basenames and missing directories",
          "[pose_import_admission][selection]") {
    const auto K_FIELD = GENERATE(0, 1);
    reject_import_and_repair(ErrorCode::INVALID_DATA,
                             [K_FIELD](const pose_fixture& fixture, CompletedPoseConfig& config) {
                                 config.m_decode_stage =
                                     K_FIELD == 0 ? fixture.posed : fixture.root / "missing-decode";
                             });
}

TEST_CASE("pose import requires both canonical and result assets in a checked stage",
          "[pose_import_admission][assets]") {
    const auto K_CANONICAL = GENERATE(false, true);
    reject_import_and_repair(
        ErrorCode::INVALID_DATA, [K_CANONICAL](const pose_fixture& fixture, CompletedPoseConfig&) {
            REQUIRE(std::filesystem::remove(
                fixture.posed / (K_CANONICAL ? "canonical_poses.json" : "result.json")));
            fixture.inventory(fixture.posed);
        });
}

TEST_CASE("checked pose stage results reject unsupported artifact scale and geometry profiles",
          "[pose_import_admission][profile]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    reject_import_and_repair(ErrorCode::UNSUPPORTED_OPERATION,
                             [K_FIELD](const pose_fixture& fixture, CompletedPoseConfig&) {
                                 const auto K_PATH = fixture.posed / "result.json";
                                 auto value = read_metadata(K_PATH);
                                 if (K_FIELD == 0) {
                                     value["canonical_poses"] = "other.json";
                                 } else if (K_FIELD == 1) {
                                     value["scale_status"] = "METRIC_MEASURED";
                                 } else {
                                     value["geometry"] = "COLLISION_READY";
                                 }
                                 write_metadata(K_PATH, value);
                                 fixture.inventory(fixture.posed);
                             });
}

TEST_CASE("rehashed canonical profiles reject unsupported coordinates scale and gravity",
          "[pose_import_admission][profile]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    reject_import_and_repair(
        ErrorCode::UNSUPPORTED_OPERATION, [K_FIELD](pose_fixture& fixture, CompletedPoseConfig&) {
            switch (K_FIELD) {
            case 0:
                fixture.canonical["schema_version"] = 2;
                break;
            case 1:
                fixture.canonical["epoch"] = 1;
                break;
            case 2:
                fixture.canonical["revision"] = 2;
                break;
            case 3:
                fixture.canonical["scale_status"] = "METRIC_MEASURED";
                break;
            case 4:
                fixture.canonical["gravity"] = "measured";
                break;
            case 5:
                fixture.canonical["world_up"] = "-y";
                break;
            case 6:
                fixture.canonical["backend_world_rotation_wxyz"] = { 1, 0, 0, 0 };
                break;
            default:
                FAIL("unknown canonical profile mutation");
            }
            fixture.publish();
        });
}

TEST_CASE("rehashed pose inventories reject malformed arrays counts and quality metadata",
          "[pose_import_admission][inventory]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    reject_import_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](pose_fixture& fixture, CompletedPoseConfig&) {
            switch (K_FIELD) {
            case 0:
                fixture.canonical["cameras"] = nlohmann::json::object();
                break;
            case 1:
                fixture.canonical["cameras"] = nlohmann::json::array();
                break;
            case 2:
                fixture.canonical["cameras"].push_back(fixture.canonical["cameras"][0]);
                break;
            case 3:
                fixture.canonical["poses"] = nlohmann::json::object();
                break;
            case 4:
                fixture.canonical["poses"].erase(2);
                break;
            case 5:
                fixture.canonical["poses"].push_back(fixture.canonical["poses"][0]);
                break;
            case 6:
                fixture.canonical["registered_frames"] = 2;
                break;
            case 7: {
                const auto K_PATH = fixture.posed / "result.json";
                auto value = read_metadata(K_PATH);
                value["registered_frames"] = 2;
                write_metadata(K_PATH, value);
                break;
            }
            case 8:
                fixture.canonical["points"] = 9;
                break;
            case 9:
                fixture.canonical["points"] = -1;
                break;
            case 10:
                fixture.canonical["registered_frames"] = 3.0;
                break;
            case 11:
                fixture.canonical["mean_reprojection_error"] = -1;
                break;
            case 12:
                fixture.canonical["mean_reprojection_error"] = "0.6";
                break;
            case 13:
                fixture.canonical["mean_reprojection_error"] = nullptr;
                break;
            default:
                FAIL("unknown pose inventory mutation");
            }
            fixture.publish();
        });
}

TEST_CASE("rehashed camera metadata rejects invalid dimensions intrinsics and coefficient layouts",
          "[pose_import_admission][camera]") {
    const auto K_FIELD = GENERATE(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24);
    CAPTURE(K_FIELD);
    reject_import_and_repair(ErrorCode::INVALID_DATA,
                             [K_FIELD](pose_fixture& fixture, CompletedPoseConfig&) {
                                 auto& camera = fixture.canonical["cameras"][0];
                                 switch (K_FIELD) {
                                 case 0:
                                     camera["camera_id"] = 0;
                                     break;
                                 case 1:
                                     camera["width"] = 0;
                                     break;
                                 case 2:
                                     camera["height"] = 0;
                                     break;
                                 case 3:
                                     camera["width"] = 8193;
                                     break;
                                 case 4:
                                     camera["height"] = 8193;
                                     break;
                                 case 5:
                                     camera["revision"] = 2;
                                     break;
                                 case 6:
                                     camera["backend_params_centers"] = "integer";
                                     break;
                                 case 7:
                                     camera["canonical_pixel_centers"] = "half_pixel";
                                     break;
                                 case 8:
                                     camera["calibration_matrix"] = nlohmann::json::object();
                                     break;
                                 case 9:
                                     camera["calibration_matrix"].erase(2);
                                     break;
                                 case 10:
                                     camera["calibration_matrix"][0] = nlohmann::json::object();
                                     break;
                                 case 11:
                                     camera["calibration_matrix"][1] = { 0, 529 };
                                     break;
                                 case 12:
                                     camera["calibration_matrix"][0][1] = 1;
                                     break;
                                 case 13:
                                     camera["calibration_matrix"][1][0] = 1;
                                     break;
                                 case 14:
                                     camera["calibration_matrix"][2] = { 0, 0, 2 };
                                     break;
                                 case 15:
                                     camera["backend_params"] = nlohmann::json::object();
                                     break;
                                 case 16:
                                     camera["backend_params"].erase(7);
                                     break;
                                 case 17:
                                     camera["model"] = "PINHOLE";  // eight coefficients are invalid
                                     break;
                                 case 18:
                                     camera["backend_params"][1] = 530;
                                     break;
                                 case 19:
                                     camera["backend_params"][3] = 1.5;
                                     break;
                                 case 20:
                                     camera["backend_params"][0] = 0;
                                     camera["calibration_matrix"][0][0] = 0;
                                     break;
                                 case 21:
                                     camera["backend_params"][4] = "0.1";
                                     break;
                                 case 22:
                                     camera["width"] = 3;  // PNG catalog still has width two
                                     break;
                                 case 23:
                                     camera["height"] = 2;
                                     break;
                                 case 24:
                                     camera["width"] = -1;
                                     break;
                                 default:
                                     FAIL("unknown camera mutation");
                                 }
                                 fixture.publish();
                             });
}

TEST_CASE("rehashed poses reject duplicate backend identities and mismatched observation metadata",
          "[pose_import_admission][poses]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CAPTURE(K_FIELD);
    reject_import_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](pose_fixture& fixture, CompletedPoseConfig&) {
            auto& pose = fixture.canonical["poses"][1];
            switch (K_FIELD) {
            case 0:
                pose["image_id"] = 0;
                break;
            case 1:
                pose["backend_rig_frame_id"] = 0;
                break;
            case 2:
                pose["image_id"] = 1;
                break;
            case 3:
                pose["backend_rig_frame_id"] = 1;
                break;
            case 4:
                pose["frame_id"] = fixture.canonical["poses"][0]["frame_id"];
                break;
            case 5:
                pose["frame_id"] = "unknown";
                break;
            case 6:
                pose["calibration_id"] = "different-lens";
                break;
            case 7:
                pose["camera_id"] = 2;
                break;
            case 8:
                pose["pose_revision"] = 0;
                break;
            case 9:
                pose["state"] = "provisional";
                break;
            case 10:
                pose["pts"] = 11;
                break;
            case 11:
                pose["time_base"] = "1/60";
                break;
            case 12:
                pose["image_id"] = -1;
                break;
            case 13:
                pose["backend_rig_frame_id"] = 2.0;
                break;
            case 14:
                pose["pts"] = -1;  // legacy decode profile requires nonnegative PTS
                break;
            case 15:
                pose["pts"] = std::numeric_limits<std::uint64_t>::max();
                break;
            case 16:
                pose.erase("camera_id");
                break;
            default:
                FAIL("unknown pose identity mutation");
            }
            fixture.publish();
        });
}

TEST_CASE("rehashed pose vectors reject malformed shapes values and nonrigid rotations",
          "[pose_import_admission][vectors]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(K_FIELD);
    reject_import_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](pose_fixture& fixture, CompletedPoseConfig&) {
            auto& transform = fixture.canonical["poses"][0]["world_from_camera"];
            switch (K_FIELD) {
            case 0:
                transform["translation"] = nlohmann::json::object();
                break;
            case 1:
                transform["translation"] = { 1, 2 };
                break;
            case 2:
                transform["translation"][1] = "2";
                break;
            case 3:
                transform["rotation_wxyz"] = nlohmann::json::object();
                break;
            case 4:
                transform["rotation_wxyz"] = { 1, 0, 0 };
                break;
            case 5:
                transform["rotation_wxyz"] = { 1, 0, 0, 0, 0 };
                break;
            case 6:
                transform["rotation_wxyz"][0] = nullptr;
                break;
            case 7:
                transform["rotation_wxyz"] = { 0, 0, 0, 0 };
                break;
            case 8:
                transform["rotation_wxyz"] = { 2, 0, 0, 0 };
                break;
            case 9:
                fixture.canonical["backend_world_rotation_wxyz"] = { 0, 1, 0 };
                break;
            default:
                FAIL("unknown pose vector mutation");
            }
            fixture.publish();
        });
}

TEST_CASE(
    "pose file import retains the exact registration threshold and reports unregistered frames",
    "[pose_import_admission][registration]") {
    reject_import_and_repair(
        ErrorCode::INVALID_DATA,
        [](pose_fixture& fixture, CompletedPoseConfig&) {
            auto& poses = fixture.canonical["poses"];
            poses.erase(poses.begin() + 8, poses.end());
            fixture.canonical["registered_frames"] = 8;
            auto result = read_metadata(fixture.posed / "result.json");
            result["registered_frames"] = 8;
            write_metadata(fixture.posed / "result.json", result);
            fixture.publish();
        },
        10);
    const auto K_COUNT = GENERATE(10uz, 120uz);
    pose_fixture fixture;
    extend_selection(fixture, K_COUNT);
    const auto K_REGISTERED = K_COUNT * 9 / 10;
    auto& poses = fixture.canonical["poses"];
    poses.erase(poses.begin() + static_cast<nlohmann::json::difference_type>(K_REGISTERED),
                poses.end());
    fixture.canonical["registered_frames"] = K_REGISTERED;
    auto result = read_metadata(fixture.posed / "result.json");
    result["registered_frames"] = K_REGISTERED;
    write_metadata(fixture.posed / "result.json", result);
    fixture.publish();
    const auto K_BEFORE = fixture_tree(fixture.root);
    const auto K_LOADED = fixture.load();
    REQUIRE(K_LOADED);
    CHECK((*K_LOADED)->m_observations.size() == K_COUNT);
    CHECK((*K_LOADED)->m_poses.size() == K_REGISTERED);
    CHECK((*K_LOADED)->m_training_frames.size() == K_REGISTERED);
    CHECK(fixture_tree(fixture.root) == K_BEFORE);
}

TEST_CASE("pose import supports PINHOLE calibration while retaining pose ordering and owned data",
          "[pose_import_admission][roundtrip]") {
    pose_fixture fixture;
    auto& camera = fixture.canonical["cameras"][0];
    camera["model"] = "PINHOLE";
    camera["backend_params"] = { 711., 529., .75, 1.25 };
    std::swap(fixture.canonical["poses"][0], fixture.canonical["poses"][2]);
    fixture.publish();
    const auto K_BEFORE = fixture_tree(fixture.root);
    const auto K_LOADED = fixture.load();
    REQUIRE(K_LOADED);
    REQUIRE((*K_LOADED)->m_calibrations.size() == 1);
    const auto& calibration = (*K_LOADED)->m_calibrations[0];
    CHECK(calibration.m_model == CalibrationModel::PINHOLE);
    CHECK(calibration.m_distortion == std::array<double, 4> {});
    CHECK(calibration.m_camera.m_cx == .25);
    CHECK(calibration.m_camera.m_cy == .75);
    REQUIRE((*K_LOADED)->m_poses.size() == 3);
    CHECK((*K_LOADED)->m_poses[0].m_capture_time < (*K_LOADED)->m_poses[1].m_capture_time);
    CHECK((*K_LOADED)->m_poses[1].m_capture_time < (*K_LOADED)->m_poses[2].m_capture_time);
    const auto K_BYTES = dataset_bytes(**K_LOADED);
    CHECK(fixture_tree(fixture.root) == K_BEFORE);
    fixture.canonical["poses"].clear();
    fixture.publish();
    CHECK(!fixture.load());
    CHECK(dataset_bytes(**K_LOADED) == K_BYTES);
}

TEST_CASE("duplicate training basenames cannot collapse distinct checked observation paths",
          "[pose_import_admission][training]") {
    pose_fixture fixture;
    const auto K_RETAINED = fixture.load();
    REQUIRE(K_RETAINED);
    const auto K_BYTES = dataset_bytes(**K_RETAINED);
    const auto K_ORIGINAL = fixture_tree(fixture.root);
    const auto K_NESTED = fixture.decoded / "images/nested";
    REQUIRE(std::filesystem::create_directory(K_NESTED));
    REQUIRE(std::filesystem::copy_file(fixture.decoded / "images/1.png", K_NESTED / "0.png"));
    auto result = read_metadata(fixture.decoded / "result.json");
    result["frames"][1]["file"] = "images/nested/0.png";
    write_metadata(fixture.decoded / "result.json", result);
    fixture.inventory(fixture.decoded);
    const auto K_BEFORE = fixture_tree(fixture.root);
    const auto K_REJECTED = fixture.load();
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(K_REJECTED.error().m_message == "Duplicate training image name");
    CHECK(fixture_tree(fixture.root) == K_BEFORE);
    CHECK(dataset_bytes(**K_RETAINED) == K_BYTES);
    REQUIRE(std::filesystem::remove(K_NESTED / "0.png"));
    REQUIRE(std::filesystem::remove(K_NESTED));
    restore_fixture_files(fixture, K_ORIGINAL);
    const auto K_REPAIRED = fixture.load();
    REQUIRE(K_REPAIRED);
    CHECK(dataset_bytes(**K_REPAIRED) == K_BYTES);
    CHECK(fixture_tree(fixture.root) == K_ORIGINAL);
}
