#include "../fixtures/asset_schema_fixture.hpp"
#include "world_explorer/asset_catalog/definition_io.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include "world_explorer/scene_store/asset_instances_io.hpp"

#include <catch2/generators/catch_generators.hpp>

#include <stdexcept>

using namespace world_explorer;
using namespace world_explorer::asset_schema_test;

namespace {

std::span<const std::byte> view(const std::string& text) {
    return std::as_bytes(std::span(text));
}

std::vector<std::byte> owned(const std::string& text) {
    const auto K_VIEW = view(text);
    return { K_VIEW.begin(), K_VIEW.end() };
}

std::vector<ImmutableRecord> records() {
    return { { "asset-1-1", owned("synthetic definition") },
            { "scene-1-1", owned("synthetic instances") } };
}

using Tree = std::map<std::string, std::string>;

Tree tree(const std::filesystem::path& root) {
    Tree result;
    if (!std::filesystem::exists(root)) {
        return result;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = entry.path().lexically_relative(root).generic_string();
        if (entry.is_symlink()) {
            result[K_NAME] = "link:" + std::filesystem::read_symlink(entry.path()).generic_string();
        } else if (entry.is_directory()) {
            result[K_NAME] = "directory";
        } else {
            const auto K_HASH = sha256_file(entry.path(), 128uz * 1024uz * 1024uz);
            REQUIRE(K_HASH);
            result[K_NAME] = *K_HASH;
        }
    }
    return result;
}

}  // namespace

TEST_CASE("metadata byte readers round trip independently selected immutable records",
          "[asset-metadata-io]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    const auto K_SET = fixture.instances(*K_DEFINITION);
    const auto K_INSTANCES =
        admit_asset_instances(K_SET, { *K_DEFINITION }, K_RECEIPT, K_SET.m_context);
    REQUIRE(K_INSTANCES);
    const auto K_SOURCE = tree(K_RECEIPT.source_directory());
    const auto K_DECODED = decode_asset_definition(view(K_DEFINITION->canonical_json()),
                                                   K_DEFINITION->reference(),
                                                   K_RECEIPT,
                                                   fixture.m_provenance,
                                                   K_SET.m_context);
    REQUIRE(K_DECODED);
    const auto K_DECODED_SET = decode_asset_instances(view(K_INSTANCES->canonical_json()),
                                                      K_SET.m_scene,
                                                      K_SET.m_revision,
                                                      K_INSTANCES->sha256(),
                                                      { *K_DECODED },
                                                      K_RECEIPT,
                                                      K_SET.m_context);
    REQUIRE(K_DECODED_SET);
    const auto K_OWNED_OUTPUT = K_RECEIPT.source_directory() / "unused";
    const auto K_OVERLAP =
        publish_asset_instances(*K_INSTANCES, K_RECEIPT, K_OWNED_OUTPUT, { 1 }, 0, K_SET.m_context);
    REQUIRE(!K_OVERLAP);
    CHECK(tree(K_RECEIPT.source_directory()) == K_SOURCE);
    RegionFixture destination;
    const auto K_PATH = destination.m_directory.m_root / "metadata";
    const auto K_PUBLISHED =
        publish_asset_instances(*K_INSTANCES, K_RECEIPT, K_PATH, { 1 }, 0, K_SET.m_context);
    REQUIRE(K_PUBLISHED);
    auto snapshot = read_immutable_records(K_PATH);
    REQUIRE(snapshot);
    CHECK(snapshot->m_commit.m_generation == 1);
    CHECK(snapshot->m_records.size() == 2uz);
    CHECK(snapshot->m_records.at("asset-42-1") == owned(K_DEFINITION->canonical_json()));
    CHECK(snapshot->m_records.at("scene-90-1") == owned(K_INSTANCES->canonical_json()));
    const auto K_BEFORE = tree(K_PATH);
    const auto K_REPLAY = publish_asset_instances(
        *K_INSTANCES, K_RECEIPT, K_PATH, { 1 }, 0, K_SET.m_context, {}, [](std::size_t) {
            throw std::runtime_error("replay must not call observer");
        });
    REQUIRE(K_REPLAY);
    CHECK(tree(K_PATH) == K_BEFORE);
    CHECK(tree(K_RECEIPT.source_directory()) == K_SOURCE);
    std::filesystem::remove_all(K_RECEIPT.source_directory());
    fixture.m_region.m_directory.m_root.clear();
    const auto K_REOPENED = decode_asset_definition(snapshot->m_records.at("asset-42-1"),
                                                    K_DEFINITION->reference(),
                                                    K_RECEIPT,
                                                    fixture.m_provenance,
                                                    K_SET.m_context);
    REQUIRE(K_REOPENED);
    const auto K_REOPENED_SET = decode_asset_instances(snapshot->m_records.at("scene-90-1"),
                                                       K_SET.m_scene,
                                                       K_SET.m_revision,
                                                       K_INSTANCES->sha256(),
                                                       { *K_REOPENED },
                                                       K_RECEIPT,
                                                       K_SET.m_context);
    REQUIRE(K_REOPENED_SET);
}

TEST_CASE(
    "definition reader rejects malformed rehashed metadata and independent selection mismatch",
    "[asset-metadata-io]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_DEFINITION = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEFINITION);
    const auto K_BEFORE = tree(K_RECEIPT.source_directory());
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    auto text = K_DEFINITION->canonical_json();
    auto document = Json::parse(text);
    auto selected = K_DEFINITION->reference();
    auto provenance = fixture.m_provenance;
    auto context = fixture.m_definition.m_context;
    switch (K_CASE) {
    case 0:
        text += ' ';
        break;
    case 1:
        text.insert(1uz, "\"asset_id\":42,");
        break;
    case 2:
        text.push_back('\0');
        break;
    case 3:
        document["extra"] = 1;
        break;
    case 4:
        document["schema_version"] = 2;
        break;
    case 5:
        document["asset_id"] = 43;
        break;
    case 6:
        document["sources"][0uz]["content_sha256"] = std::string(64uz, 'f');
        break;
    case 7:
        document["manual_ancestor"] = true;
        break;
    case 8:
        document["pivot"]["asset_from_geometry"]["scale"] = -1;
        break;
    case 9:
        document["materials"][0uz]["lighting"] = "CAPTURED_APPEARANCE";
        break;
    case 10:
        document["materials"] = Json::array();
        for (std::size_t index { 0uz }; index < 17uz; ++index) {
            document["materials"].push_back(Json::object());
        }
        break;
    case 11:
        provenance.clear();
        break;
    case 12:
        context.m_world_epoch = { 4 };
        break;
    case 13:
        document["receipt_sha256"] = std::string(64uz, 'a');
        break;
    case 14:
        document["sources"][0uz]["revision"] = 1.0;
        break;
    case 15:
        text.assign(64uz * 1024uz + 1uz, ' ');
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    if (K_CASE >= 3 && K_CASE <= 14) {
        text = document.dump();
    }
    selected.m_sha256 = hash(text);
    const auto K_RESULT =
        decode_asset_definition(view(text), selected, K_RECEIPT, provenance, context);
    CHECK(!K_RESULT);
    CHECK(tree(K_RECEIPT.source_directory()) == K_BEFORE);
    const auto K_AGAIN = fixture.admit(K_RECEIPT);
    REQUIRE(K_AGAIN);
    CHECK(K_DEFINITION->canonical_json() == K_AGAIN->canonical_json());
}

TEST_CASE("instance reader rejects untrusted definition selection and malformed canonical records",
          "[asset-metadata-io]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_DEF = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEF);
    const auto K_SET = fixture.instances(*K_DEF);
    const auto K_ADMITTED = admit_asset_instances(K_SET, { *K_DEF }, K_RECEIPT, K_SET.m_context);
    REQUIRE(K_ADMITTED);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    auto doc = Json::parse(K_ADMITTED->canonical_json());
    auto definitions = std::vector { *K_DEF };
    auto scene = K_SET.m_scene;
    switch (K_CASE) {
    case 0:
        doc["definitions"][0uz]["sha256"] = std::string(64uz, 'f');
        break;
    case 1:
        definitions.clear();
        break;
    case 2:
        doc["instances"][1uz]["instance_id"] = 1;
        break;
    case 3:
        doc["instances"][0uz]["material_variant_id"] = 8;
        break;
    case 4:
        doc["instances"][0uz]["world_from_asset"]["scale"] = 0;
        break;
    case 5:
        doc["instances"][0uz]["visible"] = 1;
        break;
    case 6:
        doc["manual_ancestor"] = true;
        break;
    case 7:
        scene = { 91 };
        break;
    case 8:
        doc["instances"][0uz]["room"] = {
            { "product_id", 999 }, { "revision", 1 }, { "content_sha256", std::string(64uz, 'a') } };
        break;
    case 9:
        std::swap(doc["instances"][0uz], doc["instances"][1uz]);
        break;
    case 10:
        doc["instances"][0uz]["lineage"] = Json::array();
        for (std::size_t index { 0uz }; index < 17uz; ++index) {
            doc["instances"][0uz]["lineage"].push_back(Json::object());
        }
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_TEXT = doc.dump();
    const auto K_RESULT = decode_asset_instances(view(K_TEXT),
                                                 scene,
                                                 K_SET.m_revision,
                                                 hash(K_TEXT),
                                                 definitions,
                                                 K_RECEIPT,
                                                 K_SET.m_context);
    CHECK(!K_RESULT);
}

TEST_CASE(
    "record publication rejects revision collisions and binds operation intent across retries",
    "[asset-metadata-io]") {
    RegionFixture fixture;
    const auto K_ROOT = fixture.m_directory.m_root / "records";
    const auto K_FIRST = publish_immutable_records(K_ROOT, { 4 }, 0, records(), {});
    REQUIRE(K_FIRST);
    auto changed = records();
    changed[0uz].m_bytes = owned("changed synthetic definition");
    const auto K_BEFORE = tree(K_ROOT);
    const auto K_COLLISION = publish_immutable_records(K_ROOT, { 4 }, 0, changed, {});
    REQUIRE(!K_COLLISION);
    CHECK(K_COLLISION.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    const auto K_REVISION = publish_immutable_records(K_ROOT, { 5 }, 1, changed, {});
    REQUIRE(!K_REVISION);
    CHECK(K_REVISION.error().m_code == ErrorCode::RESYNC_REQUIRED);
    const auto K_STALE =
        publish_immutable_records(K_ROOT, { 5 }, 0, { { "asset-2-1", owned("new") } }, {});
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(tree(K_ROOT) == K_BEFORE);
    const auto K_NEXT =
        publish_immutable_records(K_ROOT, { 2 }, 1, { { "asset-2-1", owned("new") } }, {});
    REQUIRE(K_NEXT);
    CHECK(K_NEXT->m_generation == 2);
    const auto K_CURRENT = tree(K_ROOT);
    const auto K_RETRY = publish_immutable_records(K_ROOT, { 4 }, 0, records(), {});
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->m_generation == 2);
    CHECK(tree(K_ROOT) == K_CURRENT);
}

TEST_CASE("record cancellation preserves last commit and resumes matching intent",
          "[asset-metadata-io]") {
    const auto K_THROW = GENERATE(false, true);
    RegionFixture fixture;
    const auto K_ROOT = fixture.m_directory.m_root / "records";
    const auto K_FIRST = publish_immutable_records(K_ROOT, { 1 }, 0, records(), {});
    REQUIRE(K_FIRST);
    auto batch = std::vector<ImmutableRecord> { { "asset-2-1", owned("second definition") },
                                               { "scene-1-2", owned("second set") } };
    std::stop_source cancel;
    const auto K_INTERRUPTED = publish_immutable_records(
        K_ROOT, { 2 }, 1, batch, {}, cancel.get_token(), [&](std::size_t count) {
            if (count == 1uz) {
                if (K_THROW) {
                    throw std::runtime_error("synthetic interruption");
                }
                cancel.request_stop();
            }
        });
    REQUIRE(!K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code ==
          (K_THROW ? ErrorCode::BACKEND_FAILURE : ErrorCode::CANCELLED));
    const auto K_PARTIAL = read_immutable_records(K_ROOT);
    REQUIRE(K_PARTIAL);
    CHECK(K_PARTIAL->m_commit.m_manifest_sha256 == K_FIRST->m_manifest_sha256);
    CHECK(K_PARTIAL->m_records.size() == 2uz);
    const auto K_BEFORE = tree(K_ROOT);
    auto altered = batch;
    altered[0uz].m_bytes = owned("altered");
    const auto K_BAD_RETRY = publish_immutable_records(K_ROOT, { 2 }, 1, altered, {});
    REQUIRE(!K_BAD_RETRY);
    CHECK(K_BAD_RETRY.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(tree(K_ROOT) == K_BEFORE);
    const auto K_RESUMED = publish_immutable_records(K_ROOT, { 2 }, 1, batch, {});
    REQUIRE(K_RESUMED);
    const auto K_COMPLETE = read_immutable_records(K_ROOT);
    REQUIRE(K_COMPLETE);
    CHECK(K_COMPLETE->m_records.size() == 4uz);
    CHECK(K_COMPLETE->m_commit.m_generation == 2);
}

TEST_CASE("record stores refuse foreign corrupt linked and locked output without repair",
          "[asset-metadata-io]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    RegionFixture fixture;
    const auto K_ROOT = fixture.m_directory.m_root / "records";
    const auto K_FIRST = publish_immutable_records(K_ROOT, { 1 }, 0, records(), {});
    REQUIRE(K_FIRST);
    switch (K_CASE) {
    case 0:
        write(K_ROOT / "foreign.txt", "caller data");
        break;
    case 1:
        write(K_ROOT / "owner.json", "wrong owner");
        break;
    case 2:
        write(K_ROOT / "manifest.json", "{broken");
        break;
    case 3:
        write(K_ROOT / "intents/1.json", "{}");
        break;
    case 4:
        write(K_ROOT / "blobs" / (hash("synthetic definition") + ".bin"), "corrupted");
        break;
    case 5:
        std::filesystem::remove(K_ROOT / "blobs" / (hash("synthetic instances") + ".bin"));
        break;
    case 6:
        std::filesystem::create_directory(K_ROOT / "blobs/foreign");
        break;
    case 7:
        std::filesystem::create_hard_link(K_ROOT / "owner.json",
                                          fixture.m_directory.m_root / "linked-owner");
        break;
    case 8:
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_BEFORE = tree(K_ROOT);
    std::unique_ptr<FileLock> lock;
    if (K_CASE == 8) {
        // Windows denies hash readers while this exclusive handle is held.
        // Snapshot every file before acquisition and again after release.
        auto acquired = FileLock::acquire(K_ROOT / ".writer.lock");
        REQUIRE(acquired);
        lock = std::move(*acquired);
    }
    const auto K_RESULT =
        publish_immutable_records(K_ROOT, { 2 }, 1, { { "asset-2-1", owned("new") } }, {});
    REQUIRE(!K_RESULT);
    if (K_CASE == 8) {
        CHECK(K_RESULT.error().m_code == ErrorCode::WOULD_BLOCK);
    }
    lock.reset();
    CHECK(tree(K_ROOT) == K_BEFORE);
    if (K_CASE != 8) {
        const auto K_READ = read_immutable_records(K_ROOT);
        CHECK(!K_READ);
    } else {
        const auto K_READ = read_immutable_records(K_ROOT);
        REQUIRE(K_READ);
        CHECK(K_READ->m_commit.m_manifest_sha256 == K_FIRST->m_manifest_sha256);
        const auto K_RETRY =
            publish_immutable_records(K_ROOT, { 2 }, 1, { { "asset-2-1", owned("new") } }, {});
        REQUIRE(K_RETRY);
        CHECK(K_RETRY->m_generation == 2);
    }
}

TEST_CASE("record store batch byte and count bounds refuse before output mutation",
          "[asset-metadata-io]") {
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5);
    RegionFixture fixture;
    const auto K_ROOT = fixture.m_directory.m_root / "records";
    auto batch = records();
    switch (K_CASE) {
    case 0:
        batch.clear();
        break;
    case 1:
        batch[0uz].m_key = "../foreign";
        break;
    case 2:
        batch[0uz].m_bytes.clear();
        break;
    case 3:
        batch.push_back(batch[0uz]);
        break;
    case 4:
        batch[0uz].m_bytes.resize(4uz * 1024uz * 1024uz + 1uz);
        break;
    case 5:
        for (std::size_t index { 0uz }; index < 64uz; ++index) {
            batch.push_back({ "extra-" + std::to_string(index), owned("x") });
        }
        break;
    default:
        throw std::logic_error("Unexpected test case");
    }
    const auto K_RESULT = publish_immutable_records(K_ROOT, { 1 }, 0, batch, {});
    CHECK(!K_RESULT);
    CHECK(!std::filesystem::exists(K_ROOT));
}

TEST_CASE("record store recovers durable intent after controlled child process exit",
          "[asset-metadata-io]") {
    RegionFixture fixture;
    const auto K_ROOT = fixture.m_directory.m_root / "records";
    std::string diagnostic;
    const auto K_EXIT = execute_process(
        { WE_TEST_ASSET_RECEIPT_PROBE, "unused", "unused", K_ROOT.string(), "record-crash" },
        {},
        {},
        [&](const std::string& text) {
            diagnostic += text;
        });
    INFO(diagnostic);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 77);
    const auto K_PARTIAL = read_immutable_records(K_ROOT);
    REQUIRE(K_PARTIAL);
    CHECK(K_PARTIAL->m_commit.m_generation == 0);
    CHECK(K_PARTIAL->m_records.empty());
    CHECK(std::filesystem::exists(K_ROOT / "intents/23.json"));
    const auto K_RETRY = publish_immutable_records(K_ROOT, { 23 }, 0, records(), {});
    REQUIRE(K_RETRY);
    const auto K_COMPLETE = read_immutable_records(K_ROOT);
    REQUIRE(K_COMPLETE);
    CHECK(K_COMPLETE->m_records.size() == 2uz);
}

TEST_CASE("metadata publishers freeze definition revisions and reject stale context without writes",
          "[asset-metadata-io]") {
    SchemaFixture fixture;
    RegionFixture destination;
    const auto K_RECEIPT = fixture.freeze();
    const auto K_FIRST = fixture.admit(K_RECEIPT);
    REQUIRE(K_FIRST);
    const auto K_ROOT = destination.m_directory.m_root / "records";
    const auto K_PUBLISHED = publish_asset_definition(
        *K_FIRST, K_RECEIPT, K_ROOT, { 1 }, 0, fixture.m_definition.m_context);
    REQUIRE(K_PUBLISHED);
    fixture.m_definition.m_bounds.m_maximum.m_y = 3;
    const auto K_CHANGED = fixture.admit(K_RECEIPT);
    REQUIRE(K_CHANGED);
    const auto K_BEFORE = tree(K_ROOT);
    const auto K_CONFLICT = publish_asset_definition(
        *K_CHANGED, K_RECEIPT, K_ROOT, { 2 }, 1, fixture.m_definition.m_context);
    REQUIRE(!K_CONFLICT);
    CHECK(K_CONFLICT.error().m_code == ErrorCode::RESYNC_REQUIRED);
    auto context = fixture.m_definition.m_context;
    context.m_pose_revision = { 6 };
    const auto K_STALE = publish_asset_definition(*K_FIRST, K_RECEIPT, K_ROOT, { 2 }, 1, context);
    REQUIRE(!K_STALE);
    CHECK(tree(K_ROOT) == K_BEFORE);
    fixture.m_definition.m_revision = 2;
    const auto K_NEW = fixture.admit(K_RECEIPT);
    REQUIRE(K_NEW);
    const auto K_NEXT =
        publish_asset_definition(*K_NEW, K_RECEIPT, K_ROOT, { 2 }, 1, fixture.m_definition.m_context);
    REQUIRE(K_NEXT);
    const auto K_SNAPSHOT = read_immutable_records(K_ROOT);
    REQUIRE(K_SNAPSHOT);
    CHECK(K_SNAPSHOT->m_records.size() == 2uz);
}

TEST_CASE("metadata decoders retain declared units and manual pivots without qualifying them",
          "[asset-metadata-io]") {
    SchemaFixture fixture;
    const auto K_RECEIPT = fixture.freeze();
    fixture.m_definition.m_units = { AssetUnitStatus::DECLARED_METRIC, 0.01, fixture.m_measurement };
    fixture.m_definition.m_pivot.m_policy = AssetPivotPolicy::OVERRIDE;
    fixture.m_definition.m_pivot.m_override_action = fixture.m_action;
    fixture.m_definition.m_manual_ancestor = true;
    const auto K_DEF = fixture.admit(K_RECEIPT);
    REQUIRE(K_DEF);
    const auto K_DECODED = decode_asset_definition(view(K_DEF->canonical_json()),
                                                   K_DEF->reference(),
                                                   K_RECEIPT,
                                                   fixture.m_provenance,
                                                   fixture.m_definition.m_context);
    REQUIRE(K_DECODED);
    CHECK(K_DECODED->definition().m_manual_ancestor);
    CHECK(K_DECODED->definition().m_units.m_status == AssetUnitStatus::DECLARED_METRIC);
    const auto K_SET = fixture.instances(*K_DEF);
    const auto K_ADMITTED = admit_asset_instances(K_SET, { *K_DEF }, K_RECEIPT, K_SET.m_context);
    REQUIRE(K_ADMITTED);
    const auto K_RESULT = decode_asset_instances(view(K_ADMITTED->canonical_json()),
                                                 K_SET.m_scene,
                                                 K_SET.m_revision,
                                                 K_ADMITTED->sha256(),
                                                 { *K_DECODED },
                                                 K_RECEIPT,
                                                 K_SET.m_context);
    REQUIRE(K_RESULT);
    CHECK(K_RESULT->canonical_json() == K_ADMITTED->canonical_json());
    auto wrong = K_DEF->reference();
    wrong.m_sha256.assign(64uz, 'a');
    const auto K_WRONG_HASH = decode_asset_definition(
        view(K_DEF->canonical_json()), wrong, K_RECEIPT, fixture.m_provenance, K_SET.m_context);
    CHECK(!K_WRONG_HASH);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED = decode_asset_instances(view(K_ADMITTED->canonical_json()),
                                                    K_SET.m_scene,
                                                    K_SET.m_revision,
                                                    K_ADMITTED->sha256(),
                                                    { *K_DEF },
                                                    K_RECEIPT,
                                                    K_SET.m_context,
                                                    stop.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("record store accepts exact per-record and aggregate byte limits",
          "[asset-metadata-io]") {
    RegionFixture fixture;
    const auto K_ROOT = fixture.m_directory.m_root / "records";
    const std::string K_FIRST(4uz * 1024uz * 1024uz, 'a');
    const std::string K_SECOND(4uz * 1024uz * 1024uz, 'b');
    const auto K_RESULT = publish_immutable_records(
        K_ROOT, { 1 }, 0, { { "asset-1-1", owned(K_FIRST) }, { "asset-2-1", owned(K_SECOND) } }, {});
    REQUIRE(K_RESULT);
    const auto K_SNAPSHOT = read_immutable_records(K_ROOT);
    REQUIRE(K_SNAPSHOT);
    CHECK(K_SNAPSHOT->m_records.at("asset-1-1") == owned(K_FIRST));
    CHECK(K_SNAPSHOT->m_records.at("asset-2-1") == owned(K_SECOND));
    const auto K_BEFORE = tree(K_ROOT);
    const auto K_TOO_LARGE = publish_immutable_records(
        K_ROOT,
        { 2 },
        1,
        { { "asset-1-1", owned(K_FIRST) }, { "asset-2-1", owned(K_SECOND) }, { "extra", owned("x") } },
        {});
    REQUIRE(!K_TOO_LARGE);
    CHECK(tree(K_ROOT) == K_BEFORE);
}
