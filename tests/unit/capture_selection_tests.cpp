#include "../fixtures/capture_selection_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::capture_selection_test;

namespace {

using Tree = std::map<std::string, std::string>;

Tree tree(const std::filesystem::path& root) {
    Tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            const auto K_SHA = sha256_file(entry.path(), 64uz * 1024uz * 1024uz);
            REQUIRE(K_SHA);
            result[entry.path().lexically_relative(root).generic_string()] = *K_SHA;
        }
    }
    return result;
}

}  // namespace

TEST_CASE("capture selections expose owned inference images without evaluation annotations",
          "[capture-selection]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedCaptureSelection, InferenceCaptureInputs>);
    STATIC_REQUIRE(!std::is_same_v<CaptureSelectionId, CaptureId>);
    const auto K_INPUTS = [] {
        SelectionFixture fixture;
        const auto K_RECEIPT = fixture.freeze();
        const auto K_BEFORE = tree(fixture.m_object.m_region.m_directory.m_root);
        const auto K_ADMITTED = fixture.inspect(K_RECEIPT);
        REQUIRE(K_ADMITTED);
        CHECK(K_ADMITTED->evaluation().size() == 1uz);
        CHECK(K_ADMITTED->evaluation()[0uz].m_annotations.size() == 1uz);
        CHECK(K_ADMITTED->evaluation()[0uz].m_annotations[0uz].record().m_classes[0uz].m_label ==
              "chair");
        CHECK(K_ADMITTED->record().m_selection.m_frames.size() == 2uz);
        auto inference = K_ADMITTED->inference();
        CHECK(inference.frames().size() == 1uz);
        CHECK(inference.frames()[0uz].m_view.m_capture == CaptureId { 17 });
        const auto K_TEXT = std::as_bytes(std::span("synthetic inference pixels", 26uz));
        CHECK(inference.frames()[0uz].m_image.size() == K_TEXT.size());
        CHECK(sha256(inference.frames()[0uz].m_image) == hash("synthetic inference pixels"));
        fixture.m_inference_map->m_calibrations.at({ 8 }).m_camera.m_fx = 1;
        CHECK(inference.frames()[0uz].m_calibration.m_camera.m_fx == 500);
        CHECK(tree(fixture.m_object.m_region.m_directory.m_root) == K_BEFORE);
        return inference;
    }();
    CHECK(K_INPUTS.frames()[0uz].m_pose.m_revision == MapRevisionId { 4 });
    CHECK(K_INPUTS.frames()[0uz].m_context.m_pose_revision == MapRevisionId { 5 });
    CHECK(K_INPUTS.frames()[0uz].m_context.m_world_epoch == WorldEpoch { 3 });
    CHECK(K_INPUTS.frames()[0uz].m_scale == ScaleStatus::ARBITRARY);
    CHECK(K_INPUTS.frames()[0uz].m_image.size() == 26uz);
}

TEST_CASE("capture selection rejects rehashed document identity role and source substitutions",
          "[capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    SelectionFixture fixture;
    fixture.m_change_record = [K_CASE](Json& doc) {
        switch (K_CASE) {
        case 0:
            doc["extra"] = true;
            break;
        case 1:
            doc["schema_version"] = 2;
            break;
        case 2:
            doc["selection_id"] = 82;
            break;
        case 3:
            doc["revision"] = 1.0;
            break;
        case 4:
            doc["world_epoch"] = 4;
            break;
        case 5:
            doc["frames"][0uz]["role"] = "HELD_OUT";
            break;
        case 6:
            doc["frames"][0uz]["role"] = "TRAIN";
            break;
        case 7:
            doc["frames"][0uz]["view"]["frame_id"] = 111;
            break;
        case 8:
            doc["sources"][0uz]["source_sha256"] = std::string(64uz, 'f');
            break;
        case 9:
            doc["sources"].push_back(doc["sources"][0uz]);
            break;
        case 10:
            doc["frames"].push_back(doc["frames"][0uz]);
            break;
        case 11:
            doc["frames"][1uz]["annotations"].clear();
            break;
        case 12:
            doc["manual_ancestor"] = true;
            break;
        case 13:
            doc["measurement"] = doc["sources"][0uz]["rights"];
            break;
        case 14:
            doc["recipe_sha256"] = std::string(64uz, 'a');
            break;
        default:
            throw std::logic_error("Unexpected case");
        }
    };
    const auto K_RECEIPT = fixture.freeze();
    const auto K_BEFORE = tree(fixture.m_object.m_region.m_directory.m_root);
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    CHECK(!K_RESULT);
    CHECK(tree(fixture.m_object.m_region.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("capture manifests reject duplicate keys NUL nesting and excessive byte tables",
          "[capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    SelectionFixture fixture;
    const auto K_BASE = fixture.freeze();
    (void)K_BASE;
    auto raw = fixture.m_record.dump();
    switch (K_CASE) {
    case 0:
        raw.insert(1uz, "\"selection_id\":81,");
        break;
    case 1:
        raw.push_back('\0');
        break;
    case 2:
        raw = std::string(17uz, '[') + "0" + std::string(17uz, ']');
        break;
    case 3:
        raw.assign(256uz * 1024uz + 1uz, ' ');
        break;
    case 4:
        fixture.m_change_record = [](Json& doc) {
            doc["frames"] = Json::array();
            for (std::size_t index { 0uz }; index < 65uz; ++index) {
                doc["frames"].push_back(Json::object());
            }
        };
        break;
    case 5:
        fixture.m_change_record = [](Json& doc) {
            doc["sources"] = Json::array();
            for (std::size_t index { 0uz }; index < 33uz; ++index) {
                doc["sources"].push_back(Json::object());
            }
        };
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    if (K_CASE <= 3) {
        fixture.m_raw = raw;
    }
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    CHECK(!K_RESULT);
}

TEST_CASE("capture selections reject role overlap annotations on inference and unused sources",
          "[capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    SelectionFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    auto selected = fixture.m_selection;
    switch (K_CASE) {
    case 0:
        selected.m_frames.push_back(selected.m_frames[0uz]);
        break;
    case 1:
        selected.m_frames[0uz].m_annotations = selected.m_frames[1uz].m_annotations;
        break;
    case 2:
        selected.m_frames[1uz].m_role = CaptureFrameRole::INFERENCE;
        selected.m_frames[1uz].m_annotations.clear();
        break;
    case 3:
        selected.m_frames[0uz].m_role = CaptureFrameRole::EVALUATION_ONLY;
        break;
    case 4:
        selected.m_sources.push_back(
            { { 99 }, hash("unused video"), selected.m_sources[0uz].m_rights });
        break;
    case 5:
        selected.m_frames[0uz].m_role = static_cast<CaptureFrameRole>(255);
        break;
    default:
        throw std::logic_error("Unexpected case");
    }
    const auto K_RESULT =
        inspect_capture_selection(K_RECEIPT, selected, fixture.annotations(K_RECEIPT), { { 3 }, { 5 } });
    CHECK(!K_RESULT);
}

TEST_CASE("capture selection scans inference dependency closure for evaluation byte aliases",
          "[capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    SelectionFixture fixture;
    fixture.m_change_graph = [K_CASE, &fixture](AssetDependencyGraph& graph) {
        auto& pose = graph.m_products[fixture.m_pose_index];
        const auto& annotation = graph.m_products[6uz];
        if (K_CASE == 0) {
            pose.m_dependencies.push_back(annotation.m_product);
        }
        if (K_CASE == 1) {
            pose.m_dependencies.push_back(graph.m_products[1uz].m_product);
            graph.m_products[1uz].m_dependencies = { annotation.m_product };
        }
        if (K_CASE == 2) {
            pose.m_recipe_sha256 = annotation.m_recipe_sha256;
        }
        if (K_CASE == 3) {
            auto alias = graph.m_products[5uz];
            alias.m_product.m_product = { 9000 };
            pose.m_dependencies.push_back(alias.m_product);
            graph.m_products.push_back(alias);
        }
    };
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    CHECK(!K_RESULT);
}

TEST_CASE("capture inference pose snapshots cannot include held out image observations",
          "[capture-selection]") {
    const auto K_ALIAS = GENERATE(0, 1, 2);
    SelectionFixture fixture;
    auto observed = fixture.m_object.m_map->m_observations.at({ 10 });
    if (K_ALIAS == 2) {
        observed = fixture.m_inference_map->m_observations.at({ 110 });
    }
    if (K_ALIAS) {
        observed.m_id = { 999 };
        observed.m_capture = { 999 };
    }
    fixture.m_inference_map->m_observations.emplace(observed.m_id, observed);
    const auto K_RECEIPT = fixture.freeze(false);
    const auto K_RESULT = fixture.inspect(K_RECEIPT, false);
    CHECK(!K_RESULT);
}

TEST_CASE("capture frames bind actual source image pose calibration and independent annotations",
          "[capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6);
    SelectionFixture fixture;
    if (K_CASE == 0) {
        fixture.m_inference_map->m_invalidated_frames.insert({ 110 });
    }
    if (K_CASE == 1) {
        fixture.m_inference_map->m_poses.at({ 110 }).m_tracking = TrackingState::LOST;
    }
    if (K_CASE == 2) {
        fixture.m_inference_map->m_observations.at({ 110 }).m_image_sha256.assign(64uz, 'a');
    }
    if (K_CASE == 3) {
        fixture.m_selection.m_sources[1uz].m_source_sha256 = hash("different video");
    }
    const auto K_RECEIPT = fixture.freeze();
    auto selected = fixture.m_selection;
    auto annotations = fixture.annotations(K_RECEIPT);
    if (K_CASE == 4) {
        selected.m_frames[0uz].m_view.m_calibration = { 9 };
    }
    if (K_CASE == 5) {
        annotations.clear();
    }
    if (K_CASE == 6) {
        selected.m_product.m_revision = 2;
    }
    const auto K_RESULT = inspect_capture_selection(K_RECEIPT, selected, annotations, { { 3 }, { 5 } });
    CHECK(!K_RESULT);
}

TEST_CASE(
    "capture annotation labels never enter inference and reverse source dependencies are allowed",
    "[capture-selection]") {
    SelectionFixture fixture;
    fixture.m_change_graph = [&fixture](AssetDependencyGraph& graph) {
        graph.m_products[4uz].m_dependencies.push_back(
            graph.m_products[fixture.m_image_index].m_product);
    };
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->inference().frames().size() == 1uz);
    CHECK(K_RESULT->evaluation()[0uz].m_annotations[0uz].record().m_classes.size() == 2uz);
}

TEST_CASE("capture selection publication preserves immutable revision history and retry bytes",
          "[capture-selection]") {
    SelectionFixture fixture;
    RegionFixture destination;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_RESULT);
    const auto K_ROOT = destination.m_directory.m_root / "capture";
    const auto K_PUBLISHED =
        publish_capture_selection(*K_RESULT, K_RECEIPT, K_ROOT, { 1 }, 0, { { 3 }, { 5 } });
    REQUIRE(K_PUBLISHED);
    const auto K_BEFORE = tree(K_ROOT);
    const auto K_RETRY = publish_capture_selection(
        *K_RESULT, K_RECEIPT, K_ROOT, { 1 }, 0, { { 3 }, { 5 } }, {}, [](std::size_t) {
            throw std::logic_error("retry observer");
        });
    REQUIRE(K_RETRY);
    CHECK(tree(K_ROOT) == K_BEFORE);
    auto snapshot = read_immutable_records(K_ROOT);
    REQUIRE(snapshot);
    CHECK(snapshot->m_records.at("capture-81-1") == K_RESULT->encoded_bytes());
    fixture.m_change_record = [](Json& doc) {
        std::reverse(doc["frames"].begin(), doc["frames"].end());
    };
    const auto K_CHANGED_RECEIPT = fixture.freeze();
    const auto K_CHANGED = fixture.inspect(K_CHANGED_RECEIPT);
    REQUIRE(K_CHANGED);
    const auto K_CONFLICT =
        publish_capture_selection(*K_CHANGED, K_CHANGED_RECEIPT, K_ROOT, { 2 }, 1, { { 3 }, { 5 } });
    CHECK(!K_CONFLICT);
    CHECK(tree(K_ROOT) == K_BEFORE);
}

TEST_CASE("capture selection cancellation stale context and output overlap preserve data",
          "[capture-selection]") {
    SelectionFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_ANNOTATIONS = fixture.annotations(K_RECEIPT);
    const auto K_BEFORE = tree(fixture.m_object.m_region.m_directory.m_root);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED = inspect_capture_selection(
        K_RECEIPT, fixture.m_selection, K_ANNOTATIONS, { { 3 }, { 5 } }, stop.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    const auto K_STALE =
        inspect_capture_selection(K_RECEIPT, fixture.m_selection, K_ANNOTATIONS, { { 3 }, { 6 } });
    CHECK(!K_STALE);
    const auto K_GOOD = fixture.inspect(K_RECEIPT);
    REQUIRE(K_GOOD);
    const auto K_OVERLAP =
        publish_capture_selection(*K_GOOD,
                                  K_RECEIPT,
                                  fixture.m_object.m_region.m_directory.m_root / "child",
                                  { 1 },
                                  0,
                                  { { 3 }, { 5 } });
    CHECK(!K_OVERLAP);
    CHECK(tree(fixture.m_object.m_region.m_directory.m_root) == K_BEFORE);
}

TEST_CASE("capture selection admits 64 frames across 32 sources and bounds excess selected tables",
          "[capture-selection]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    SelectionFixture fixture;
    const auto K_INPUT_BASE = fixture.m_selection.m_frames[0uz];
    const auto K_EVAL_BASE = fixture.m_selection.m_frames[1uz];
    for (std::size_t index { 1uz }; index < 32uz; ++index) {
        auto inference = K_INPUT_BASE;
        inference.m_view.m_frame = { 110uz + index };
        if (index < 31uz) {
            inference.m_view.m_capture = { 1000uz + index };
            fixture.m_selection.m_sources.push_back({ inference.m_view.m_capture,
                                                     hash("synthetic inference video"),
                                                     fixture.m_selection.m_sources[0uz].m_rights });
        }
        auto observed = fixture.m_inference_map->m_observations.at({ 110 });
        observed.m_id = inference.m_view.m_frame;
        observed.m_capture = inference.m_view.m_capture;
        auto pose = fixture.m_inference_map->m_poses.at({ 110 });
        pose.m_frame = observed.m_id;
        fixture.m_inference_map->m_observations.emplace(observed.m_id, observed);
        fixture.m_inference_map->m_poses.emplace(pose.m_frame, pose);
        fixture.m_selection.m_frames.push_back(inference);
        auto evaluation = K_EVAL_BASE;
        evaluation.m_view.m_frame = { 10uz + index };
        auto eval_observed = fixture.m_object.m_map->m_observations.at({ 10 });
        eval_observed.m_id = evaluation.m_view.m_frame;
        auto eval_pose = fixture.m_object.m_map->m_poses.at({ 10 });
        eval_pose.m_frame = eval_observed.m_id;
        fixture.m_object.m_map->m_observations.emplace(eval_observed.m_id, eval_observed);
        fixture.m_object.m_map->m_poses.emplace(eval_pose.m_frame, eval_pose);
        fixture.m_selection.m_frames.push_back(evaluation);
    }
    const auto K_RECEIPT = fixture.freeze(false);
    auto selection = fixture.m_selection;
    if (K_CASE == 1) {
        selection.m_frames.push_back(selection.m_frames[0uz]);
    }
    if (K_CASE == 2) {
        selection.m_sources.push_back(selection.m_sources[0uz]);
    }
    auto annotations = std::vector<AdmittedObjectObservation> {};
    if (K_CASE == 3) {
        const auto K_ANNOTATION = fixture.annotations(K_RECEIPT);
        annotations.assign(129uz, K_ANNOTATION[0uz]);
    }
    const auto K_RESULT = inspect_capture_selection(K_RECEIPT, selection, annotations, { { 3 }, { 5 } });
    if (K_CASE == 0) {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->inference().frames().size() == 32uz);
        CHECK(K_RESULT->evaluation().size() == 32uz);
        CHECK(K_RESULT->record().m_selection.m_sources.size() == 32uz);
    } else {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
}

TEST_CASE("capture image byte groups admit 32 MiB and refuse additional retained images",
          "[capture-selection]") {
    const auto K_EXCESS = GENERATE(false, true);
    SelectionFixture fixture;
    const std::string K_IMAGE(16uz * 1024uz * 1024uz, 'x');
    auto& node = fixture.m_object.m_region.m_graph.m_products[fixture.m_image_index];
    fixture.m_object.m_region.m_data[hash(K_IMAGE)] = K_IMAGE;
    node.m_product.m_content_sha256 = hash(K_IMAGE);
    fixture.m_selection.m_frames[0uz].m_view.m_image = node.m_product;
    fixture.m_inference_map->m_observations.at({ 110 }).m_image_sha256 = hash(K_IMAGE);
    for (std::size_t index { 1uz }; index < (K_EXCESS ? 3uz : 2uz); ++index) {
        auto selected = fixture.m_selection.m_frames[0uz];
        selected.m_view.m_frame = { 110uz + index };
        auto observed = fixture.m_inference_map->m_observations.at({ 110 });
        observed.m_id = selected.m_view.m_frame;
        auto pose = fixture.m_inference_map->m_poses.at({ 110 });
        pose.m_frame = observed.m_id;
        fixture.m_inference_map->m_observations.emplace(observed.m_id, observed);
        fixture.m_inference_map->m_poses.emplace(pose.m_frame, pose);
        fixture.m_selection.m_frames.push_back(selected);
    }
    const auto K_RECEIPT = fixture.freeze(false);
    const auto K_RESULT = fixture.inspect(K_RECEIPT, false);
    if (K_EXCESS) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    } else {
        REQUIRE(K_RESULT);
        CHECK(K_RESULT->inference().frames().size() == 2uz);
        CHECK(K_RESULT->inference().frames()[0uz].m_image.size() == 16uz * 1024uz * 1024uz);
    }
}

TEST_CASE("manual evaluation annotations do not become inference manual ancestry",
          "[capture-selection]") {
    SelectionFixture fixture;
    const std::string K_ACTION { "synthetic manual evaluation annotation; no authenticated act" };
    auto& region = fixture.m_object.m_region;
    region.m_data[hash(K_ACTION)] = K_ACTION;
    region.m_graph.m_products[6uz].m_manual_action_sha256 = hash(K_ACTION);
    region.m_graph.m_products[6uz].m_manual_ancestor = true;
    fixture.m_object.m_record["manual_ancestor"] = true;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_RESULT = fixture.inspect(K_RECEIPT);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->record().m_manual_ancestor);
    CHECK(K_RESULT->evaluation()[0uz].m_annotations[0uz].record().m_manual_ancestor);
    CHECK(!K_RESULT->inference().frames()[0uz].m_manual_ancestor);
}
