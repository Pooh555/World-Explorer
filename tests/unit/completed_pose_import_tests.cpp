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
        const auto name = entry.path().lexically_relative(root).generic_string();
        CAPTURE(name);
        const auto status = entry.symlink_status();
        if (std::filesystem::is_directory(status))
            result.emplace(name, "directory");
        else {
            REQUIRE(std::filesystem::is_regular_file(status));
            REQUIRE(entry.file_size() <= std::size_t{16} * 1024 * 1024);
            std::ifstream input(entry.path(), std::ios::binary);
            REQUIRE(input);
            std::string bytes{std::istreambuf_iterator<char>(input), {}};
            REQUIRE(!input.bad());
            result.emplace(name, "file:" + bytes);
        }
    }
    return result;
}
void restore_fixture_files(const pose_fixture& fixture,
                           const std::map<std::string, std::string>& original) {
    for (const auto& [name, value] : original)
        if (value.starts_with("file:"))
            write_text(fixture.root / name, std::string_view(value).substr(5));
}
std::vector<std::byte> dataset_bytes(const completed_pose_dataset& dataset) {
    map_store map;
    map_transaction tx;
    tx.operation = {1};
    tx.new_revision = dataset.revision;
    tx.epoch = dataset.epoch;
    tx.calibrations = dataset.calibrations;
    tx.observations = dataset.observations;
    tx.poses = dataset.poses;
    tx.alignments = {{dataset.submap, dataset.revision, {}}};
    REQUIRE(map.commit(tx));
    const auto encoded =
        encode_pose_asset({map.acquire_snapshot(),
                           {dataset.source_sha256, dataset.decode_completion_sha256,
                            dataset.pose_completion_sha256, dataset.journal_sha256}});
    REQUIRE(encoded);
    return *encoded;
}
void extend_selection(pose_fixture& fixture, std::size_t count) {
    if (count == 3)
        return;
    REQUIRE(count >= 3);
    REQUIRE(count <= 120);
    auto decoded = read_metadata(fixture.decoded / "result.json");
    auto frames = nlohmann::json::array();
    auto poses = nlohmann::json::array();
    for (std::size_t index = 0; index < count; ++index) {
        const auto file = "images/" + std::to_string(index) + ".png";
        if (index >= 3)
            REQUIRE(std::filesystem::copy_file(fixture.decoded / "images/0.png",
                                               fixture.decoded / file));
        auto frame = decoded.at("frames")[0];
        frame["file"] = file;
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
template <class mutation>
void reject_import_and_repair(error_code expected, mutation mutate, std::size_t count = 3,
                              const std::stop_token& stop = {}) {
    pose_fixture fixture;
    extend_selection(fixture, count);
    write_text(fixture.root / "caller.txt", "unrelated caller data");
    const auto retained = fixture.load();
    REQUIRE(retained);
    const auto original_bytes = dataset_bytes(**retained);
    const auto original = fixture_tree(fixture.root);
    completed_pose_config config{fixture.root, fixture.source_digest};
    mutate(fixture, config);
    const auto before = fixture_tree(fixture.root);
    const auto rejected = load_completed_poses(config, stop);
    INFO((rejected ? "unexpected import" : rejected.error().message));
    REQUIRE(!rejected);
    CHECK(rejected.error().code == expected);
    CHECK(fixture_tree(fixture.root) == before);
    CHECK(dataset_bytes(**retained) == original_bytes);
    // Explicitly repair only these owned fixture files; the importer never repairs them.
    restore_fixture_files(fixture, original);
    CHECK(fixture_tree(fixture.root) == original);
    const auto repaired = fixture.load();
    REQUIRE(repaired);
    CHECK(dataset_bytes(**repaired) == original_bytes);
    CHECK(fixture_tree(fixture.root) == original);
}
} // namespace

TEST_CASE("pose import rejects invalid source configuration and pre-cancelled work without writes",
          "[pose_import_admission][config]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(field);
    std::stop_source cancellation;
    if (field >= 4)
        cancellation.request_stop();
    reject_import_and_repair(
        field >= 4   ? error_code::CANCELLED
        : field == 3 ? error_code::INVALID_DATA
                     : error_code::INVALID_ARGUMENT,
        [field](const pose_fixture& fixture, completed_pose_config& config) {
            if (field == 0 || field == 5)
                config.source_sha256.clear();
            else if (field == 1)
                config.source_sha256.pop_back();
            else if (field == 2)
                config.source_sha256[0] = 'G';
            else if (field == 3)
                config.pose_job = fixture.root / "missing-job";
        },
        3, cancellation.get_token());
}

TEST_CASE("pose job completion rejects malformed bounded metadata and mismatched output",
          "[pose_import_admission][completion]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    CAPTURE(field);
    reject_import_and_repair(
        error_code::INVALID_DATA, [field](const pose_fixture& fixture, completed_pose_config&) {
            const auto path = fixture.root / "result.json";
            auto value = read_metadata(path);
            switch (field) {
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
                REQUIRE(std::filesystem::remove(path));
                return;
            case 10:
                write_text(path, "{");
                return;
            case 11:
                write_text(path, std::string(1024 * 1024 + 1, ' '));
                return;
            case 12:
                write_text(path, std::string(34, '[') + "0" + std::string(34, ']'));
                return;
            case 13:
                value["journal_sha256"] = std::string(64, 'e');
                break;
            default:
                FAIL("unknown completion mutation");
            }
            write_metadata(path, value);
        });
}

TEST_CASE("pose and decode pointers reject invalid keys strings and escaping committed paths",
          "[pose_import_admission][pointers]") {
    const auto pose_pointer = GENERATE(false, true);
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(pose_pointer, field);
    reject_import_and_repair(
        error_code::INVALID_DATA,
        [pose_pointer, field](const pose_fixture& fixture, completed_pose_config&) {
            const auto path = fixture.root / (pose_pointer ? "poses.json" : "decode.json");
            auto value = read_metadata(path);
            switch (field) {
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
            write_metadata(path, value);
        });
}

TEST_CASE("explicit decode selection rejects invalid basenames and missing directories",
          "[pose_import_admission][selection]") {
    const auto field = GENERATE(0, 1);
    reject_import_and_repair(error_code::INVALID_DATA, [field](const pose_fixture& fixture,
                                                               completed_pose_config& config) {
        config.decode_stage = field == 0 ? fixture.posed : fixture.root / "missing-decode";
    });
}

TEST_CASE("pose import requires both canonical and result assets in a checked stage",
          "[pose_import_admission][assets]") {
    const auto canonical = GENERATE(false, true);
    reject_import_and_repair(
        error_code::INVALID_DATA, [canonical](const pose_fixture& fixture, completed_pose_config&) {
            REQUIRE(std::filesystem::remove(fixture.posed /
                                            (canonical ? "canonical_poses.json" : "result.json")));
            fixture.inventory(fixture.posed);
        });
}

TEST_CASE("checked pose stage results reject unsupported artifact scale and geometry profiles",
          "[pose_import_admission][profile]") {
    const auto field = GENERATE(0, 1, 2);
    reject_import_and_repair(error_code::UNSUPPORTED_OPERATION,
                             [field](const pose_fixture& fixture, completed_pose_config&) {
                                 const auto path = fixture.posed / "result.json";
                                 auto value = read_metadata(path);
                                 if (field == 0)
                                     value["canonical_poses"] = "other.json";
                                 else if (field == 1)
                                     value["scale_status"] = "METRIC_MEASURED";
                                 else
                                     value["geometry"] = "COLLISION_READY";
                                 write_metadata(path, value);
                                 fixture.inventory(fixture.posed);
                             });
}

TEST_CASE("rehashed canonical profiles reject unsupported coordinates scale and gravity",
          "[pose_import_admission][profile]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    reject_import_and_repair(
        error_code::UNSUPPORTED_OPERATION, [field](pose_fixture& fixture, completed_pose_config&) {
            switch (field) {
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
                fixture.canonical["backend_world_rotation_wxyz"] = {1, 0, 0, 0};
                break;
            default:
                FAIL("unknown canonical profile mutation");
            }
            fixture.publish();
        });
}

TEST_CASE("rehashed pose inventories reject malformed arrays counts and quality metadata",
          "[pose_import_admission][inventory]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    reject_import_and_repair(
        error_code::INVALID_DATA, [field](pose_fixture& fixture, completed_pose_config&) {
            switch (field) {
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
                const auto path = fixture.posed / "result.json";
                auto value = read_metadata(path);
                value["registered_frames"] = 2;
                write_metadata(path, value);
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
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                19, 20, 21, 22, 23, 24);
    CAPTURE(field);
    reject_import_and_repair(error_code::INVALID_DATA,
                             [field](pose_fixture& fixture, completed_pose_config&) {
                                 auto& camera = fixture.canonical["cameras"][0];
                                 switch (field) {
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
                                     camera["calibration_matrix"][1] = {0, 529};
                                     break;
                                 case 12:
                                     camera["calibration_matrix"][0][1] = 1;
                                     break;
                                 case 13:
                                     camera["calibration_matrix"][1][0] = 1;
                                     break;
                                 case 14:
                                     camera["calibration_matrix"][2] = {0, 0, 2};
                                     break;
                                 case 15:
                                     camera["backend_params"] = nlohmann::json::object();
                                     break;
                                 case 16:
                                     camera["backend_params"].erase(7);
                                     break;
                                 case 17:
                                     camera["model"] = "PINHOLE"; // eight coefficients are invalid
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
                                     camera["width"] = 3; // PNG catalog still has width two
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
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    CAPTURE(field);
    reject_import_and_repair(
        error_code::INVALID_DATA, [field](pose_fixture& fixture, completed_pose_config&) {
            auto& pose = fixture.canonical["poses"][1];
            switch (field) {
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
                pose["pts"] = -1; // legacy decode profile requires nonnegative PTS
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
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(field);
    reject_import_and_repair(
        error_code::INVALID_DATA, [field](pose_fixture& fixture, completed_pose_config&) {
            auto& transform = fixture.canonical["poses"][0]["world_from_camera"];
            switch (field) {
            case 0:
                transform["translation"] = nlohmann::json::object();
                break;
            case 1:
                transform["translation"] = {1, 2};
                break;
            case 2:
                transform["translation"][1] = "2";
                break;
            case 3:
                transform["rotation_wxyz"] = nlohmann::json::object();
                break;
            case 4:
                transform["rotation_wxyz"] = {1, 0, 0};
                break;
            case 5:
                transform["rotation_wxyz"] = {1, 0, 0, 0, 0};
                break;
            case 6:
                transform["rotation_wxyz"][0] = nullptr;
                break;
            case 7:
                transform["rotation_wxyz"] = {0, 0, 0, 0};
                break;
            case 8:
                transform["rotation_wxyz"] = {2, 0, 0, 0};
                break;
            case 9:
                fixture.canonical["backend_world_rotation_wxyz"] = {0, 1, 0};
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
        error_code::INVALID_DATA,
        [](pose_fixture& fixture, completed_pose_config&) {
            auto& poses = fixture.canonical["poses"];
            poses.erase(poses.begin() + 8, poses.end());
            fixture.canonical["registered_frames"] = 8;
            auto result = read_metadata(fixture.posed / "result.json");
            result["registered_frames"] = 8;
            write_metadata(fixture.posed / "result.json", result);
            fixture.publish();
        },
        10);
    const auto count = GENERATE(std::size_t{10}, std::size_t{120});
    pose_fixture fixture;
    extend_selection(fixture, count);
    const auto registered = count * 9 / 10;
    auto& poses = fixture.canonical["poses"];
    poses.erase(poses.begin() + static_cast<nlohmann::json::difference_type>(registered),
                poses.end());
    fixture.canonical["registered_frames"] = registered;
    auto result = read_metadata(fixture.posed / "result.json");
    result["registered_frames"] = registered;
    write_metadata(fixture.posed / "result.json", result);
    fixture.publish();
    const auto before = fixture_tree(fixture.root);
    const auto loaded = fixture.load();
    REQUIRE(loaded);
    CHECK((*loaded)->observations.size() == count);
    CHECK((*loaded)->poses.size() == registered);
    CHECK((*loaded)->training_frames.size() == registered);
    CHECK(fixture_tree(fixture.root) == before);
}

TEST_CASE("pose import supports PINHOLE calibration while retaining pose ordering and owned data",
          "[pose_import_admission][roundtrip]") {
    pose_fixture fixture;
    auto& camera = fixture.canonical["cameras"][0];
    camera["model"] = "PINHOLE";
    camera["backend_params"] = {711., 529., .75, 1.25};
    std::swap(fixture.canonical["poses"][0], fixture.canonical["poses"][2]);
    fixture.publish();
    const auto before = fixture_tree(fixture.root);
    const auto loaded = fixture.load();
    REQUIRE(loaded);
    REQUIRE((*loaded)->calibrations.size() == 1);
    const auto& calibration = (*loaded)->calibrations[0];
    CHECK(calibration.model == calibration_model::PINHOLE);
    CHECK(calibration.distortion == std::array<double, 4>{});
    CHECK(calibration.camera.cx == .25);
    CHECK(calibration.camera.cy == .75);
    REQUIRE((*loaded)->poses.size() == 3);
    CHECK((*loaded)->poses[0].capture_time < (*loaded)->poses[1].capture_time);
    CHECK((*loaded)->poses[1].capture_time < (*loaded)->poses[2].capture_time);
    const auto bytes = dataset_bytes(**loaded);
    CHECK(fixture_tree(fixture.root) == before);
    fixture.canonical["poses"].clear();
    fixture.publish();
    CHECK(!fixture.load());
    CHECK(dataset_bytes(**loaded) == bytes);
}

TEST_CASE("duplicate training basenames cannot collapse distinct checked observation paths",
          "[pose_import_admission][training]") {
    pose_fixture fixture;
    const auto retained = fixture.load();
    REQUIRE(retained);
    const auto bytes = dataset_bytes(**retained);
    const auto original = fixture_tree(fixture.root);
    const auto nested = fixture.decoded / "images/nested";
    REQUIRE(std::filesystem::create_directory(nested));
    REQUIRE(std::filesystem::copy_file(fixture.decoded / "images/1.png", nested / "0.png"));
    auto result = read_metadata(fixture.decoded / "result.json");
    result["frames"][1]["file"] = "images/nested/0.png";
    write_metadata(fixture.decoded / "result.json", result);
    fixture.inventory(fixture.decoded);
    const auto before = fixture_tree(fixture.root);
    const auto rejected = fixture.load();
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(rejected.error().message == "Duplicate training image name");
    CHECK(fixture_tree(fixture.root) == before);
    CHECK(dataset_bytes(**retained) == bytes);
    REQUIRE(std::filesystem::remove(nested / "0.png"));
    REQUIRE(std::filesystem::remove(nested));
    restore_fixture_files(fixture, original);
    const auto repaired = fixture.load();
    REQUIRE(repaired);
    CHECK(dataset_bytes(**repaired) == bytes);
    CHECK(fixture_tree(fixture.root) == original);
}
