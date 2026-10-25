#include "../fixtures/evaluation_paths_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <span>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::evaluation_paths_test;

TEST_CASE("evaluation paths bind complete temporal blocks independent inspection and declared "
          "uncertainty",
          "[evaluation-paths]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedEvaluationPaths, InferenceCaptureInputs>);
    PathsFixture fixture;
    const auto K_PATHS = fixture.inspect();
    REQUIRE(K_PATHS);
    CHECK(K_PATHS->selection().m_blocks.size() == 4uz);
    CHECK(K_PATHS->selection().m_blocks[1uz].m_frames.size() == 2uz);
    CHECK(K_PATHS->selection().m_path.size() == 2uz);
    CHECK(K_PATHS->has_unassessed_or_uncertain_poses());
    CHECK(!K_PATHS->manual_ancestor());
    CHECK(K_PATHS->capture_product() == fixture.m_views.m_selection.m_product);
    CHECK(K_PATHS->receipt_sha256() == fixture.m_receipt.receipt_sha256());
    CHECK(fixture.m_capture.inference().frames().size() == 1uz);
    CHECK(!fixture.m_capture.validate_inference_dependency(
        fixture.m_receipt, fixture.m_selected.m_temporal, fixture.m_receipt.graph().m_context));
}

TEST_CASE("evaluation paths reject stale schema camera timestamps and world poses after rehashing",
          "[evaluation-paths]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    PathsFixture fixture([K_CASE](Json& temporal, auto&, Json& inspection) {
        auto& camera = temporal["blocks"][1uz]["frames"][0uz];
        switch (K_CASE) {
        case 0:
            temporal["extra"] = true;
            break;
        case 1:
            temporal["schema_version"] = 2;
            break;
        case 2:
            temporal["world_epoch"] = 4;
            break;
        case 3:
            temporal["manual_ancestor"] = true;
            break;
        case 4:
            temporal["blocks"][0uz]["source_sha256"] = hash("foreign source");
            break;
        case 5:
            camera["pts"] = 2.0;
            break;
        case 6:
            camera["pts"] = std::numeric_limits<std::uint64_t>::max();
            break;
        case 7:
            camera["time_base"][1uz] = 60;
            break;
        case 8:
            camera["capture_time_ns"] = 19;
            break;
        case 9:
            camera["world_from_camera"]["rotation"][0uz] = 1;
            break;
        case 10:
            camera["world_from_camera"]["translation"][0uz] = 0;
            break;
        case 11:
            camera["world_from_camera"]["scale"] = 1;
            break;
        case 12:
            camera["view"]["pose_asset"]["revision"] = 2;
            break;
        case 13:
            camera["view"]["calibration_id"] = 9;
            break;
        case 14:
            inspection["path"]["entries"][0uz]["camera"]["pts"] = -1;
            break;
        case 15:
            inspection["path"]["entries"][0uz]["confidence"] = "CERTIFIED";
            break;
        case 16:
            inspection["pose_revision"] = 6;
            break;
        case 17:
            inspection["path"]["entries"][0uz]["camera"]["view"]["frame_id"] = 110;
            break;
        case 18:
            temporal["blocks"][1uz]["role"] = "FIT_TEST_APPEARANCE";
            break;
        case 19:
            camera["time_base"].push_back(0);
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("temporal partition rejects missing reused split misordered and foreign selected frames",
          "[evaluation-paths]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    PathsFixture fixture([K_CASE](Json& temporal, auto& selected, Json&) {
        auto& blocks = temporal["blocks"];
        switch (K_CASE) {
        case 0:
            blocks.erase(3uz);
            selected.m_blocks.pop_back();
            break;
        case 1:
            blocks[1uz]["frames"].push_back(blocks[1uz]["frames"][0uz]);
            selected.m_blocks[1uz].m_frames.push_back({ 120 });
            break;
        case 2:
            std::swap(blocks[1uz]["frames"][0uz], blocks[1uz]["frames"][1uz]);
            std::ranges::reverse(selected.m_blocks[1uz].m_frames);
            break;
        case 3:
            blocks[1uz]["block_id"] = 1;
            selected.m_blocks[1uz].m_id = 1;
            break;
        case 4:
            blocks[1uz]["role"] = "INFERENCE";
            selected.m_blocks[1uz].m_role = CaptureFrameRole::INFERENCE;
            break;
        case 5:
            blocks[1uz]["frames"].erase(1uz);
            selected.m_blocks[1uz].m_frames.pop_back();
            break;
        case 6: {
            auto row = blocks[1uz];
            row["block_id"] = 5;
            row["frames"].erase(0uz);
            blocks[1uz]["frames"].erase(1uz);
            selected.m_blocks[1uz].m_frames.pop_back();
            blocks.insert(blocks.begin() + 2z, row);
            selected.m_blocks.insert(selected.m_blocks.begin() + 2z,
                                     { 5, { 17 }, CaptureFrameRole::HELD_OUT, { { 121 } } });
            break;
        }
        case 7:
            blocks[1uz]["frames"][0uz]["view"]["frame_id"] = 999;
            selected.m_blocks[1uz].m_frames[0uz] = { 999 };
            break;
        case 8:
            blocks[0uz]["block_id"] = 0;
            selected.m_blocks[0uz].m_id = 0;
            break;
        case 9:
            selected.m_blocks[0uz].m_capture = { 99 };
            blocks[0uz]["capture_id"] = 99;
            break;
        case 10:
            std::swap(blocks[0uz], blocks[1uz]);
            std::swap(selected.m_blocks[0uz], selected.m_blocks[1uz]);
            break;
        case 11:
            blocks[1uz]["frames"] = Json::array();
            selected.m_blocks[1uz].m_frames.clear();
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "independent inspection refuses omitted reused reordered training and evaluation-only entries",
    "[evaluation-paths]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    PathsFixture fixture([K_CASE](Json& temporal, auto& selected, Json& inspection) {
        auto& entries = inspection["path"]["entries"];
        switch (K_CASE) {
        case 0:
            entries.erase(1uz);
            selected.m_path.pop_back();
            break;
        case 1:
            entries.push_back(entries[0uz]);
            selected.m_path.push_back(selected.m_path[0uz]);
            break;
        case 2:
            std::swap(entries[0uz], entries[1uz]);
            std::ranges::reverse(selected.m_path);
            break;
        case 3:
            entries[0uz]["camera"] = temporal["blocks"][0uz]["frames"][0uz];
            selected.m_path[0uz].m_capture = { 17 };
            selected.m_path[0uz].m_frame = { 110 };
            break;
        case 4:
            entries[0uz]["camera"] = temporal["blocks"][1uz]["frames"][0uz];
            selected.m_path[0uz].m_capture = { 17 };
            selected.m_path[0uz].m_frame = { 120 };
            break;
        case 5:
            entries[0uz]["camera"] = temporal["blocks"][3uz]["frames"][0uz];
            selected.m_path[0uz].m_frame = { 30 };
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation path metadata rejects content recipe aliases and incomplete dependencies",
          "[evaluation-paths]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    PathsFixture fixture({}, [K_CASE](AssetDependencyGraph& graph, const auto& selected) {
        switch (K_CASE) {
        case 0:
            graph.m_products[9uz].m_recipe_sha256 = selected.m_temporal.m_content_sha256;
            break;
        case 1:
            graph.m_products[9uz].m_recipe_sha256 = selected.m_inspection.m_content_sha256;
            break;
        case 2:
            graph.m_products.back().m_dependencies.pop_back();
            break;
        case 3:
            graph.m_products[graph.m_products.size() - 2uz].m_dependencies.clear();
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(!fixture.inspect());
}

TEST_CASE("evaluation path role checks retain native inference pose-fit exclusion",
          "[evaluation-paths]") {
    CHECK_THROWS_AS(PathsFixture({},
                                 {},
                                 {},
                                 [](SelectionFixture& views) {
                                     auto observation =
                                         views.m_object.m_map->m_observations.at({ 10 });
                                     auto pose = views.m_object.m_map->m_poses.at({ 10 });
                                     views.m_inference_map->m_observations.emplace(observation.m_id,
                                                                                   observation);
                                     views.m_inference_map->m_poses.emplace(pose.m_frame, pose);
                                 }),
                    std::runtime_error);
}

TEST_CASE("evaluation path admission bounds independent tables and rejects stale exact pins",
          "[evaluation-paths]") {
    PathsFixture fixture;
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    auto selected = fixture.m_selected;
    auto context = fixture.m_receipt.graph().m_context;
    switch (K_CASE) {
    case 0:
        selected.m_blocks.resize(65uz);
        break;
    case 1:
        selected.m_path.resize(65uz);
        break;
    case 2:
        selected.m_blocks[0uz].m_frames.resize(65uz);
        break;
    case 3:
        selected.m_temporal.m_revision++;
        break;
    case 4:
        selected.m_inspection.m_content_sha256 = std::string(65uz, 'a');
        break;
    case 5:
        selected.m_path[0uz].m_confidence = static_cast<EvaluationPoseConfidence>(255);
        break;
    case 6:
        context.m_pose_revision = { 6 };
        break;
    case 7:
        selected.m_blocks[0uz].m_frames[0uz] = { 120 };
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    CHECK(!inspect_evaluation_paths(selected, fixture.m_capture, fixture.m_receipt, context));
}

TEST_CASE(
    "evaluation path bytes reject duplicate nul depth and excess but retain pinned whitespace",
    "[evaluation-paths]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    PathsFixture::ByteMutation bytes;
    bytes.m_temporal = [K_CASE](std::string& text) {
        switch (K_CASE) {
        case 0:
            text.insert(1uz, "\"schema_version\":1,");
            break;
        case 1:
            text.push_back('\0');
            break;
        case 2:
            text.resize(256uz * 1024uz + 1uz, ' ');
            break;
        case 3:
            text = std::string(18uz, '[') + "0" + std::string(18uz, ']');
            break;
        case 4:
            text.resize(256uz * 1024uz, ' ');
            break;
        default:
            break;
        }
    };
    bytes.m_inspection = [K_CASE](std::string& text) {
        if (K_CASE == 5) {
            text.resize(256uz * 1024uz + 1uz, ' ');
        }
        if (K_CASE == 6) {
            text.resize(256uz * 1024uz, ' ');
        }
    };
    PathsFixture fixture({}, {}, bytes);
    CHECK(fixture.inspect().has_value() == (K_CASE == 4 || K_CASE == 6));
}

TEST_CASE(
    "evaluation path confidence changes preserve inference pixels without manufacturing certainty",
    "[evaluation-paths]") {
    PathsFixture original;
    const auto K_CASE = GENERATE(0, 1);
    PathsFixture changed([K_CASE](Json&, auto& selected, Json& inspection) {
        for (std::size_t index { 0uz }; index < selected.m_path.size(); ++index) {
            selected.m_path[index].m_confidence = K_CASE == 0
                                                      ? EvaluationPoseConfidence::DECLARED_RELIABLE
                                                      : EvaluationPoseConfidence::UNASSESSED;
            inspection["path"]["entries"][index]["confidence"] =
                K_CASE == 0 ? "DECLARED_RELIABLE" : "UNASSESSED";
        }
    });
    const auto K_PATHS = checked(changed.inspect());
    CHECK(K_PATHS.has_unassessed_or_uncertain_poses() == (K_CASE != 0));
    CHECK(sha256(original.m_capture.inference().frames()[0uz].m_image) ==
          sha256(changed.m_capture.inference().frames()[0uz].m_image));
}

TEST_CASE(
    "evaluation path cancellation hides staged records exact replay and selected reload recover",
    "[evaluation-paths]") {
    const auto K_POINT = GENERATE(1uz, 2uz);
    PathsFixture fixture;
    const auto K_PATHS = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_evaluation_paths(K_PATHS,
                                                   fixture.m_receipt,
                                                   fixture.directory(),
                                                   { 11 },
                                                   0,
                                                   K_CONTEXT,
                                                   stop.get_token(),
                                                   [&](std::size_t count) {
                                                       if (count == K_POINT) {
                                                           stop.request_stop();
                                                       }
                                                   });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    const auto K_BEFORE = checked(read_immutable_records(fixture.directory()));
    CHECK(K_BEFORE.m_records.empty());
    const auto K_COMMIT = checked(publish_evaluation_paths(
        K_PATHS, fixture.m_receipt, fixture.directory(), { 11 }, 0, K_CONTEXT));
    CHECK(checked(publish_evaluation_paths(
                      K_PATHS, fixture.m_receipt, fixture.directory(), { 11 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    const auto K_READ = read_evaluation_paths(fixture.directory(),
                                              K_COMMIT,
                                              fixture.m_selected,
                                              fixture.m_capture,
                                              fixture.m_receipt,
                                              K_CONTEXT);
    REQUIRE(K_READ);
    CHECK(K_READ->selection().m_blocks == fixture.m_selected.m_blocks);
    CHECK(K_READ->selection().m_path == fixture.m_selected.m_path);
    auto stale = K_COMMIT;
    stale.m_generation++;
    CHECK(!read_evaluation_paths(fixture.directory(),
                                 stale,
                                 fixture.m_selected,
                                 fixture.m_capture,
                                 fixture.m_receipt,
                                 K_CONTEXT));
    CHECK(!publish_evaluation_paths(
        K_PATHS, fixture.m_receipt, fixture.m_receipt.source_directory(), { 12 }, 0, K_CONTEXT));
    CHECK(!publish_evaluation_paths(
        K_PATHS, fixture.m_receipt, fixture.directory(), { 12 }, 0, K_CONTEXT));
    auto selection = fixture.m_selected;
    selection.m_temporal.m_content_sha256 = hash("not committed");
    CHECK(!read_evaluation_paths(
        fixture.directory(), K_COMMIT, selection, fixture.m_capture, fixture.m_receipt, K_CONTEXT));
}

TEST_CASE("evaluation path ownership survives caller mutation and source destruction with explicit "
          "cancellation",
          "[evaluation-paths]") {
    const auto K_PATHS = [] {
        PathsFixture fixture;
        std::stop_source stop;
        stop.request_stop();
        const auto K_CANCEL = fixture.inspect(stop.get_token());
        REQUIRE(!K_CANCEL);
        CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
        auto result = checked(fixture.inspect());
        fixture.m_selected.m_path.clear();
        fixture.m_selected.m_blocks.clear();
        return result;
    }();
    CHECK(K_PATHS.selection().m_blocks.size() == 4uz);
    CHECK(K_PATHS.selection().m_path.size() == 2uz);
}

TEST_CASE(
    "corpus schema four requires exact evaluation path handles and preserves earlier encodings",
    "[evaluation-paths][asset-corpus]") {
    const bool K_SCALE = GENERATE(false, true);
    const bool K_INVENTORY = GENERATE(false, true);
    PathsFixture fixture;
    RecipeFixture recipe;
    const auto K_RECIPE = recipe.save();
    const auto K_REGISTRY = fixture.m_output.m_root / "capture";
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    const auto K_CAPTURE_COMMIT = checked(publish_capture_selection(
        fixture.m_capture, fixture.m_receipt, K_REGISTRY, { 31 }, 0, K_CONTEXT));
    const auto& selection = fixture.m_views.m_selection;
    const auto& node = K_RECIPE.graph().m_products.back();
    AssetCorpusDefinition definition {
        "synthetic-paths",
        1,
        AssetCorpusTrack::SYNTHETIC,
        { { "corner",
          selection.m_product,
          selection.m_selection,
          selection.m_revision,
          fixture.m_receipt.receipt_sha256(),
          K_CAPTURE_COMMIT,
          { { AssetCorpusEvidence::TEMPORAL_BLOCKS, fixture.m_selected.m_temporal },
           { AssetCorpusEvidence::INSPECTION_PATH, fixture.m_selected.m_inspection } } } },
        { { "import",
          node.m_product,
          K_RECIPE.receipt_sha256(),
          node.m_recipe_sha256,
          recipe.m_context,
          recipe.m_host } } };
    definition.m_require_typed_scale = K_SCALE;
    definition.m_require_typed_inventory = K_INVENTORY;
    std::vector<AssetCorpusCaptureInput> captures {
        { "corner", fixture.m_capture, fixture.m_receipt, K_REGISTRY } };
    const std::vector<AssetCorpusRecipeInput> K_RECIPES { { "import", K_RECIPE } };
    const auto K_OLD = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(Json::parse(K_OLD.canonical_json()).at("schema_version") == (K_INVENTORY ? 3
                                                                       : K_SCALE   ? 2
                                                                                   : 1));
    definition.m_require_typed_paths = true;
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    captures[0uz].m_paths = checked(fixture.inspect());
    const auto K_CORPUS = checked(admit_asset_corpus(definition, captures, K_RECIPES));
    CHECK(Json::parse(K_CORPUS.canonical_json()).at("schema_version") == 4);
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "EVALUATION_POSE_UNCERTAINTY_DECLARED";
    }));
    CHECK(std::ranges::any_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "PRETUNING_FREEZE_UNVERIFIED";
    }));
    CHECK(std::ranges::none_of(K_CORPUS.blockers(), [](const auto& blocker) {
        return blocker.m_code == "TEMPORAL_BLOCKS_UNVERIFIED" ||
               blocker.m_code == "INSPECTION_PATH_UNVERIFIED";
    }));
    PathsFixture foreign([](Json&, auto& selected, Json& inspection) {
        selected.m_path[0uz].m_confidence = EvaluationPoseConfidence::UNASSESSED;
        inspection["path"]["entries"][0uz]["confidence"] = "UNASSESSED";
    });
    captures[0uz].m_paths = checked(foreign.inspect());
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    captures[0uz].m_paths = checked(fixture.inspect());
    const auto K_SEAL = fixture.m_output.m_root / "seal";
    const auto K_COMMIT = checked(publish_asset_corpus(K_CORPUS, K_SEAL, { 32 }));
    CHECK(read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), captures, K_RECIPES));
    for (const auto& flag : { Json(false), Json(1), Json(nullptr) }) {
        auto document = Json::parse(K_CORPUS.canonical_json());
        document["typed_paths"] = flag;
        const auto K_TEXT = document.dump();
        CHECK(!decode_asset_corpus(std::as_bytes(std::span(K_TEXT)),
                                   { definition.m_corpus, definition.m_revision, hash(K_TEXT) },
                                   captures,
                                   K_RECIPES));
    }
    captures[0uz].m_paths.reset();
    CHECK(!read_asset_corpus(K_SEAL, K_COMMIT, K_CORPUS.reference(), captures, K_RECIPES));
    definition.m_require_typed_paths = false;
    CHECK(checked(admit_asset_corpus(definition, captures, K_RECIPES)).canonical_json() ==
          K_OLD.canonical_json());
    captures[0uz].m_paths = checked(fixture.inspect());
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    definition.m_require_typed_paths = true;
    definition.m_captures[0uz].m_evidence.erase(AssetCorpusEvidence::INSPECTION_PATH);
    CHECK(!admit_asset_corpus(definition, captures, K_RECIPES));
    definition.m_captures[0uz].m_evidence.clear();
    captures[0uz].m_paths.reset();
    CHECK(admit_asset_corpus(definition, captures, K_RECIPES));
}

TEST_CASE(
    "temporal ordering uses native signed pts and rejects regressions or source time-base changes",
    "[evaluation-paths]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    PathsFixture fixture({}, {}, {}, [K_CASE](SelectionFixture& views) {
        auto& map = *views.m_object.m_map;
        auto& second = map.m_observations.at({ 20 });
        switch (K_CASE) {
        case 0:
            second.m_timestamp.m_pts = 1;
            break;
        case 1:
            second.m_timestamp.m_time_base_denominator = 60;
            break;
        case 2:
            second.m_capture_time = std::chrono::nanoseconds { 16 };
            map.m_poses.at({ 20 }).m_capture_time = second.m_capture_time;
            break;
        case 3:
            second.m_timestamp.m_pts = -1;
            break;
        case 4:
            map.m_observations.at({ 10 }).m_timestamp.m_pts = -3;
            second.m_timestamp.m_pts = -2;
            map.m_observations.at({ 30 }).m_timestamp.m_pts = -1;
            break;
        case 5:
            map.m_observations.at({ 10 }).m_timestamp.m_pts =
                std::numeric_limits<std::int64_t>::min();
            second.m_timestamp.m_pts = std::numeric_limits<std::int64_t>::max() - 1;
            map.m_observations.at({ 30 }).m_timestamp.m_pts =
                std::numeric_limits<std::int64_t>::max();
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    });
    CHECK(fixture.inspect().has_value() == (K_CASE >= 4));
}

TEST_CASE("evaluation paths require both a training-source holdout and distinct full inspection "
          "source bytes",
          "[evaluation-paths]") {
    const auto K_CASE = GENERATE(0, 1);
    PathsFixture fixture(
        [K_CASE](Json& temporal, auto& selected, Json&) {
            if (K_CASE == 0) {
                selected.m_blocks[1uz].m_role = CaptureFrameRole::EVALUATION_ONLY;
                temporal["blocks"][1uz]["role"] = "EVALUATION_ONLY";
            }
        },
        {},
        {},
        [K_CASE](SelectionFixture& views) {
            if (K_CASE == 0) {
                for (auto& frame : views.m_selection.m_frames) {
                    if (frame.m_view.m_capture == CaptureId { 17 } &&
                        frame.m_role == CaptureFrameRole::HELD_OUT) {
                        frame.m_role = CaptureFrameRole::EVALUATION_ONLY;
                    }
                }
            } else {
                views.m_selection.m_sources[0uz].m_source_sha256 =
                    hash("synthetic inference video");
                const auto K_POSE = checked(encode_pose_asset({ views.m_object.m_map,
                                                               { hash("synthetic inference video"),
                                                                hash("synthetic decode"),
                                                                hash("synthetic solve"),
                                                                {} } }));
                views.m_object.m_pose_bytes =
                    std::string(reinterpret_cast<const char*>(K_POSE.data()), K_POSE.size());
            }
        });
    CHECK(!fixture.inspect());
}
