#include "../fixtures/completion_product_fixture.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <type_traits>

using namespace world_explorer;
using namespace world_explorer::completion_product_test;

TEST_CASE("completion admits actual filled geometry while protecting every observed source face",
          "[completion-product]") {
    STATIC_REQUIRE(!std::is_convertible_v<AdmittedCompletionProduct, SceneSnapshot>);
    const auto K_METHOD = GENERATE(
        CompletionMethod::INFERRED, CompletionMethod::GENERATED, CompletionMethod::LIBRARY);
    const auto K_SUBJECT =
        GENERATE(CompletionSubject::OBJECT_GEOMETRY, CompletionSubject::SHELL_GEOMETRY);
    CompletionFixture fixture({ .m_subject = K_SUBJECT, .m_method = K_METHOD });
    const auto K_RESULT = fixture.inspect();
    INFO((K_RESULT ? "admitted" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->selected_geometry().mesh().m_triangles.size() == 10uz);
    CHECK(K_RESULT->source_geometry() == fixture.m_source.selection().m_product);
    CHECK(K_RESULT->structure_product() == fixture.m_structure.selection().m_product);
    CHECK(K_RESULT->receipt_sha256() == fixture.m_receipt.receipt_sha256());
    REQUIRE(K_RESULT->candidates().size() == 2uz);
    CHECK(K_RESULT->candidates()[0uz].m_protected.size() == 8uz);
    CHECK(K_RESULT->candidates()[0uz].m_checks.m_observed_anchors_preserved);
    CHECK(K_RESULT->candidates()[0uz].m_checks.m_complete_region_partition);
    CHECK(K_RESULT->candidates()[0uz].m_regions[0uz].m_faces == std::vector<std::uint32_t> { 8, 9 });
    CHECK(K_RESULT->candidates()[1uz].m_rejection ==
          CompletionRejection::DECLARED_RANKED_ALTERNATIVE);
    CHECK(K_RESULT->synthetic_source());
    CHECK(!K_RESULT->manual_ancestor());
    fixture.m_record.clear();
    fixture.m_base.m_base.source_mesh().m_vertices.clear();
    CHECK(K_RESULT->selected_geometry().high_mesh().m_triangles.size() == 8uz);
}

TEST_CASE("completion observed protection accepts cyclic corner correspondence without vertex "
          "identity assumptions",
          "[completion-product]") {
    CompletionFixtureMutations changes;
    changes.m_geometry = [](Json& doc, std::size_t) {
        doc["mesh"]["triangles"][0uz] = { 1, 5, 0 };
        const auto K_ANCHORS = doc["support"][0uz]["anchors"];
        doc["support"][0uz]["anchors"] =
            Json::array({ K_ANCHORS[1uz], K_ANCHORS[2uz], K_ANCHORS[0uz] });
    };
    changes.m_root = [](Json& doc, auto&) {
        for (auto& row : doc["candidates"]) {
            row["protected"][0uz]["rotation"] = 2;
        }
    };
    CompletionFixture fixture({}, changes);
    CHECK(fixture.inspect());
}

TEST_CASE("completion can retain truthfully failed rejected alternatives without selecting them",
          "[completion-product]") {
    const bool K_ANCHOR_FAILURE = GENERATE(true, false);
    CompletionFixtureMutations changes;
    changes.m_root = [&](Json& doc, auto&) {
        auto& row = doc["candidates"][1uz];
        row["checks"]["complete_region_partition"] = false;
        if (K_ANCHOR_FAILURE) {
            row["protected"].erase(0uz);
            row["checks"]["observed_anchors_preserved"] = false;
            row["rejection"] = "ANCHOR_CHECK_FAILED";
        } else {
            row["regions"] = Json::array();
            row["rejection"] = "REGION_CHECK_FAILED";
        }
    };
    CompletionFixture fixture({}, changes);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(!K_RESULT->candidates()[1uz].m_checks.m_complete_region_partition);
    CHECK(K_RESULT->candidates()[0uz].m_checks.m_complete_region_partition);
}

TEST_CASE("completion cannot promote previously inferred geometry to observed selected output",
          "[completion-product]") {
    const bool K_SELECTED = GENERATE(true, false);
    CompletionFixtureMutations changes;
    changes.m_geometry = [&](Json& doc, std::size_t candidate) {
        if (candidate == (K_SELECTED ? 0uz : 1uz)) {
            doc["support"][7uz]["class"] = "OBSERVED";
        }
    };
    changes.m_root = [&](Json& doc, auto&) {
        auto& row = doc["candidates"][K_SELECTED ? 0uz : 1uz];
        row["checks"]["observed_anchors_preserved"] = false;
        row["checks"]["complete_region_partition"] = false;
        if (!K_SELECTED) {
            row["rejection"] = "ANCHOR_CHECK_FAILED";
        }
    };
    CompletionFixture fixture({ .m_inferred_source = true }, changes);
    const auto K_RESULT = fixture.inspect();
    if (K_SELECTED) {
        CHECK(!K_RESULT);
    } else {
        REQUIRE(K_RESULT);
        CHECK(!K_RESULT->candidates()[1uz].m_checks.m_observed_anchors_preserved);
    }
}

TEST_CASE(
    "completion rejects rehashed false protection partitions profile and ranking declarations",
    "[completion-product]") {
    const auto K_CASE =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20);
    CompletionFixtureMutations changes;
    changes.m_root = [&](Json& doc, auto&) {
        auto& row = doc["candidates"][0uz];
        switch (K_CASE) {
        case 0:
            row["protected"].erase(0uz);
            break;
        case 1:
            row["protected"][0uz]["result_face"] = 1;
            break;
        case 2:
            row["protected"][0uz]["rotation"] = 1;
            break;
        case 3:
            row["protected"][0uz]["source_face"] = 999;
            break;
        case 4:
            row["protected"].push_back(row["protected"][0uz]);
            break;
        case 5:
            row["regions"][0uz]["faces"] = { 8 };
            row["regions"][0uz]["bounds"] = { { "minimum", { 1, 1, 0 } }, { "maximum", { 3, 3, 0 } } };
            break;
        case 6:
            row["regions"].push_back(row["regions"][0uz]);
            row["regions"][1uz]["id"] = 2;
            break;
        case 7:
            row["regions"][0uz]["class"] = "OBSERVED";
            break;
        case 8:
            row["regions"][0uz]["confidence"] = 1.1;
            break;
        case 9:
            row["regions"][0uz]["bounds"]["minimum"] = { 0, 0, 0 };
            break;
        case 10:
            row["regions"][0uz]["surfaces"] = { 99 };
            break;
        case 11:
            row["checks"]["observed_anchors_preserved"] = false;
            break;
        case 12:
            doc["scope"] = "QUALIFIED_COMPLETION";
            break;
        case 13:
            doc["source"]["revision"] = 2;
            break;
        case 14:
            doc["capture"]["revision"] = 2;
            break;
        case 15:
            doc["structure"]["revision"] = 2;
            break;
        case 16:
            doc["selected_candidate"] = 99;
            break;
        case 17:
            doc["candidates"][1uz]["rank"] = 1;
            break;
        case 18:
            doc["candidates"][1uz]["rejection"] = "REGION_CHECK_FAILED";
            break;
        case 19:
            doc["manual_ancestor"] = true;
            break;
        case 20:
            doc["unknown"] = 1;
            break;
        default:
            throw std::logic_error("Unexpected completion root case");
        }
    };
    CompletionFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "completion selected candidate must pass even when its failed checks are truthfully declared",
    "[completion-product]") {
    const bool K_EXTENT = GENERATE(true, false);
    CompletionFixtureMutations changes;
    changes.m_root = [&](Json& doc, auto& selection) {
        if (K_EXTENT) {
            selection.m_max_region_extent_units = 1;
            for (auto& row : doc["candidates"]) {
                row["checks"]["region_extent_within_policy"] = false;
            }
        } else {
            doc["candidates"][0uz]["protected"].erase(0uz);
            doc["candidates"][0uz]["checks"]["observed_anchors_preserved"] = false;
            doc["candidates"][0uz]["checks"]["complete_region_partition"] = false;
        }
    };
    CompletionFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("completion preserves generated edits unknown regions and shared independent input bytes",
          "[completion-product]") {
    const bool K_MANUAL = GENERATE(true, false);
    CompletionFixtureMutations changes;
    if (!K_MANUAL) {
        changes.m_geometry = [](Json& doc, std::size_t) {
            doc["support"][8uz]["class"] = "UNKNOWN";
            doc["support"][9uz]["class"] = "UNKNOWN";
        };
    }
    CompletionFixture fixture({ .m_manual = K_MANUAL }, changes);
    const auto K_RESULT = fixture.inspect();
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->manual_ancestor() == K_MANUAL);
    CHECK(K_RESULT->candidates()[0uz].m_regions[0uz].m_class ==
          (K_MANUAL ? AssetEvidenceClass::MANUALLY_EDITED : AssetEvidenceClass::UNKNOWN));
    CHECK(K_RESULT->candidates()[0uz].m_regions[0uz].m_confidence.has_value() == K_MANUAL);
}

TEST_CASE(
    "completion method records require exact independent algorithm prompt seed and rights pins",
    "[completion-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CompletionFixtureMutations changes;
    changes.m_configuration = [&](Json& doc, auto&) {
        switch (K_CASE) {
        case 0:
            doc["seed"] = 1;
            break;
        case 1:
            doc["method"] = "INFERRED";
            break;
        case 2:
            doc["algorithm"]["revision"] = 2;
            break;
        case 3:
            doc["rights"]["revision"] = 2;
            break;
        case 4:
            doc["prompt"] = nullptr;
            break;
        case 5:
            doc["library"] = doc["algorithm"];
            break;
        case 6:
            doc["pose_revision"] = 6;
            break;
        case 7:
            doc["manual_ancestor"] = true;
            break;
        default:
            throw std::logic_error("Unexpected completion method case");
        }
    };
    CompletionFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE(
    "completion strict root and configuration bytes reject duplicate deep NUL and excess records",
    "[completion-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    const bool K_CONFIGURATION = GENERATE(true, false);
    CompletionFixtureMutations changes;
    auto mutation = [&](std::string& text) {
        if (K_CASE == 0) {
            text.insert(1uz, "\"schema_version\":1,");
        }
        if (K_CASE == 1) {
            text = std::string(17uz, '[') + "0" + std::string(17uz, ']');
        }
        if (K_CASE == 2) {
            text.push_back('\0');
        }
        if (K_CASE == 3) {
            text.resize((K_CONFIGURATION ? 16uz * 1024uz : 1024uz * 1024uz) + 1uz, ' ');
        }
    };
    if (K_CONFIGURATION) {
        changes.m_configuration_bytes = mutation;
    } else {
        changes.m_root_bytes = mutation;
    }
    CompletionFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("completion method input payloads validate prompt text and byte budgets before ownership",
          "[completion-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3);
    CompletionFixtureMutations changes;
    changes.m_input = [&](std::string& text, std::uint64_t id) {
        if (K_CASE == 0 && id == 15202) {
            text = Json { { "schema_version", 1 },
                         { "kind", "COMPLETION_PROMPT" },
                         { "text", std::string(4097uz, 'x') } }
                       .dump();
        }
        if (K_CASE == 1 && id == 15202) {
            text = Json { { "schema_version", 1 },
                         { "kind", "COMPLETION_PROMPT" },
                         { "text", std::string("x\0y", 3uz) } }
                       .dump();
        }
        if (K_CASE == 2 && id == 15200) {
            text.resize(64uz * 1024uz + 1uz, 'x');
        }
        if (K_CASE == 3 && id == 15201) {
            text.resize(64uz * 1024uz + 1uz, 'x');
        }
    };
    CompletionFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("completion rejects missing foreign stale and reordered independent prerequisites",
          "[completion-product]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CompletionFixture fixture;
    auto request = fixture.request();
    if (!request.m_selection) {
        throw std::logic_error("Missing completion selection");
    }
    switch (K_CASE) {
    case 0:
        request.m_selection.reset();
        break;
    case 1:
        request.m_source.reset();
        break;
    case 2:
        request.m_structure.reset();
        break;
    case 3:
        request.m_capture.reset();
        break;
    case 4:
        request.m_receipt.reset();
        break;
    case 5:
        request.m_candidates.clear();
        break;
    case 6:
        request.m_source = fixture.m_base.m_geometry;
        break;
    case 7:
        request.m_structure = checked(fixture.m_base.inspect());
        break;
    case 8:
        request.m_expected.m_pose_revision = { 6 };
        break;
    case 9:
        request.m_selection->m_max_region_extent_units = std::numeric_limits<double>::quiet_NaN();
        break;
    case 10:
        request.m_selection->m_candidates[0uz].m_geometry.m_revision = 2;
        break;
    default:
        throw std::logic_error("Unexpected completion prerequisite case");
    }
    CHECK(!inspect_completion_product(request));
}

TEST_CASE(
    "completion candidate selection order does not replace independent rank and product identity",
    "[completion-product]") {
    CompletionFixture fixture;
    auto request = fixture.request();
    if (!request.m_selection) {
        throw std::logic_error("Missing completion selection");
    }
    std::ranges::reverse(request.m_candidates);
    std::ranges::reverse(request.m_selection->m_candidates);
    CHECK(inspect_completion_product(request));
}

TEST_CASE("completion dependency closure includes every candidate and excludes evaluation aliases",
          "[completion-product]") {
    const auto K_CASE = GENERATE(0, 1, 2);
    CompletionFixtureMutations changes;
    changes.m_graph = [&](auto& graph, const auto& selected) {
        const auto K_EVALUATION = graph.m_products[0uz].m_product;
        for (auto& node : graph.m_products) {
            if (node.m_product == selected.m_product && K_CASE == 0) {
                node.m_dependencies.pop_back();
            }
            if (node.m_product == selected.m_product && K_CASE == 1) {
                node.m_dependencies.push_back(K_EVALUATION);
            }
            if (node.m_product == selected.m_candidates[0uz].m_method.m_configuration &&
                K_CASE == 2) {
                node.m_dependencies.pop_back();
            }
        }
    };
    CompletionFixture fixture({}, changes);
    CHECK(!fixture.inspect());
}

TEST_CASE("completion publication cancels across the owned method batch and exact retry reopens",
          "[completion-product]") {
    const auto K_STOP_AFTER = GENERATE(1uz, 3uz, 6uz);
    CompletionFixture fixture;
    const auto K_PRODUCT = checked(fixture.inspect());
    const auto K_CONTEXT = fixture.m_receipt.graph().m_context;
    std::stop_source stop;
    const auto K_CANCEL = publish_completion_product(K_PRODUCT,
                                                     fixture.m_receipt,
                                                     fixture.directory(),
                                                     { 251 },
                                                     0,
                                                     K_CONTEXT,
                                                     stop.get_token(),
                                                     [&](std::size_t count) {
                                                         if (count == K_STOP_AFTER) {
                                                             stop.request_stop();
                                                         }
                                                     });
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.empty());
    const auto K_COMMIT = checked(publish_completion_product(
        K_PRODUCT, fixture.m_receipt, fixture.directory(), { 251 }, 0, K_CONTEXT));
    CHECK(checked(read_immutable_records(fixture.directory())).m_records.size() == 6uz);
    CHECK(checked(publish_completion_product(
                      K_PRODUCT, fixture.m_receipt, fixture.directory(), { 251 }, 0, K_CONTEXT))
              .m_manifest_sha256 == K_COMMIT.m_manifest_sha256);
    CHECK(read_completion_product(fixture.directory(), K_COMMIT, fixture.request()));
    auto stale = K_COMMIT;
    ++stale.m_generation;
    CHECK(!read_completion_product(fixture.directory(), stale, fixture.request()));
    CHECK(!publish_completion_product(
        K_PRODUCT, fixture.m_receipt, fixture.directory(), { 252 }, 0, K_CONTEXT));
    CHECK(!publish_completion_product(
        K_PRODUCT, fixture.m_receipt, fixture.m_receipt.source_directory(), { 251 }, 0, K_CONTEXT));
}

TEST_CASE(
    "completion reload requires committed configuration and actual input payloads as well as root",
    "[completion-product]") {
    const auto K_ID = GENERATE(15100, 15200, 15201, 15202);
    CompletionFixture fixture;
    const auto K_PRODUCT = checked(fixture.inspect());
    const auto K_COMMIT = checked(publish_completion_product(K_PRODUCT,
                                                             fixture.m_receipt,
                                                             fixture.directory(),
                                                             { 253 },
                                                             0,
                                                             fixture.m_receipt.graph().m_context));
    const auto K_SNAPSHOT = checked(read_immutable_records(fixture.directory()));
    std::vector<ImmutableRecord> records;
    for (const auto& [K_KEY, K_BYTES] : K_SNAPSHOT.m_records) {
        if (K_KEY != "completion-input-" + std::to_string(K_ID) + "-1") {
            records.push_back({ K_KEY, K_BYTES });
        }
    }
    REQUIRE(records.size() + 1uz == K_SNAPSHOT.m_records.size());
    const auto K_PARTIAL = fixture.directory().parent_path() / "partial";
    const auto K_OTHER = checked(publish_immutable_records(K_PARTIAL, { 254 }, 0, records, {}));
    CHECK(!read_completion_product(K_PARTIAL, K_OTHER, fixture.request()));
    CHECK(K_COMMIT.m_generation == 1);
}

TEST_CASE("completion exact root configuration and library limits retain publishable owned bytes",
          "[completion-product]") {
    const bool K_LIBRARY = GENERATE(true, false);
    CompletionFixtureMutations changes;
    changes.m_root_bytes = [](std::string& text) {
        text.resize(1024uz * 1024uz, ' ');
    };
    changes.m_configuration_bytes = [](std::string& text) {
        text.resize(16uz * 1024uz, ' ');
    };
    changes.m_input = [&](std::string& text, std::uint64_t id) {
        if (id == 15203) {
            text.resize(1024uz * 1024uz + (K_LIBRARY ? 0uz : 1uz), 'x');
        }
    };
    CompletionFixture fixture({ .m_method = CompletionMethod::LIBRARY }, changes);
    const auto K_RESULT = fixture.inspect();
    if (!K_LIBRARY) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
        return;
    }
    REQUIRE(K_RESULT);
    const auto K_COMMIT = checked(publish_completion_product(*K_RESULT,
                                                             fixture.m_receipt,
                                                             fixture.directory(),
                                                             { 255 },
                                                             0,
                                                             fixture.m_receipt.graph().m_context));
    CHECK(read_completion_product(fixture.directory(), K_COMMIT, fixture.request()));
}

TEST_CASE(
    "completion candidate count is bounded with exact maximum and pre cancellation owns no token",
    "[completion-product]") {
    const auto K_COUNT = GENERATE(8uz, 9uz);
    CompletionFixture fixture({ .m_candidates = K_COUNT });
    const auto K_RESULT = fixture.inspect();
    if (K_COUNT == 9uz) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
        return;
    }
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->candidates().size() == 8uz);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCEL = fixture.inspect(stop.get_token());
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    CHECK(!inspect_completion_product({}));
}

TEST_CASE(
    "completion aggregate byte budget accounts for distinct library inputs before publication",
    "[completion-product]") {
    const auto K_COUNT = GENERATE(6uz, 8uz);
    CompletionFixtureMutations changes;
    changes.m_root_bytes = [](std::string& text) {
        text.resize(1024uz * 1024uz, ' ');
    };
    changes.m_input = [](std::string& text, std::uint64_t id) {
        if (id >= 15400 && id < 15408) {
            text.resize(1024uz * 1024uz, 'x');
        }
    };
    CompletionFixture fixture({ .m_method = CompletionMethod::LIBRARY,
                               .m_candidates = K_COUNT,
                               .m_distinct_libraries = true },
                              changes);
    const auto K_RESULT = fixture.inspect();
    if (K_COUNT == 8uz) {
        REQUIRE(!K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
        CHECK(!std::filesystem::exists(fixture.directory()));
        return;
    }
    REQUIRE(K_RESULT);
    CHECK(publish_completion_product(*K_RESULT,
                                     fixture.m_receipt,
                                     fixture.directory(),
                                     { 256 },
                                     0,
                                     fixture.m_receipt.graph().m_context));
}
