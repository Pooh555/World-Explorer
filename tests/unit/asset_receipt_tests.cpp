#include "world_explorer/asset_catalog/dependency_receipt.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

using namespace world_explorer;

namespace {

void write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    output.close();
    REQUIRE(output);
}

std::string hash_text(const std::string& value) {
    return sha256(std::as_bytes(std::span(value)));
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

struct ReceiptFixture {
    std::filesystem::path m_root;
    AssetDependencyContext m_context { { 3 }, { 5 } };
    AssetDependencyGraph m_graph;
    std::map<std::string, std::string> m_data;
    nlohmann::json m_document;

    ReceiptFixture() {
        static std::atomic<std::uint64_t> g_sequence {};
        const auto K_PARENT = std::filesystem::canonical(std::filesystem::temp_directory_path());
        for (std::size_t attempt { 0uz }; attempt < 1024uz; ++attempt) {
            m_root = K_PARENT / ("we-asset-receipt-" + std::to_string(g_sequence.fetch_add(1)));
            if (std::filesystem::create_directory(m_root)) {
                break;
            }
            m_root.clear();
        }
        REQUIRE_FALSE(m_root.empty());
        std::filesystem::create_directory(m_root / "source");
        std::filesystem::create_directory(m_root / "source/blobs");
        const auto K_PRODUCT = add("synthetic source bytes");
        const auto K_CHILD = add("synthetic derived bytes");
        const auto K_RECIPE = add("opaque synthetic recipe receipt");
        const auto K_ACTION = add("opaque synthetic manual action receipt");
        const AssetRevisionRef K_REF { { 1 }, 1, K_PRODUCT };
        m_graph = { 1,
                   m_context,
                   { { K_REF, {}, K_RECIPE, K_ACTION, true },
                    { { { 2 }, 1, K_CHILD }, { K_REF }, K_RECIPE, {}, true } } };
        document();
    }

    ~ReceiptFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    ReceiptFixture(const ReceiptFixture&) = delete;
    ReceiptFixture& operator=(const ReceiptFixture&) = delete;

    std::string add(const std::string& bytes) {
        const auto K_SHA = hash_text(bytes);
        m_data[K_SHA] = bytes;
        write(m_root / "source/blobs" / (K_SHA + ".bin"), bytes);
        return K_SHA;
    }

    void document() {
        const auto K_CHECKED = validate_asset_dependencies(m_graph, m_context);
        REQUIRE(K_CHECKED);
        auto products = nlohmann::json::array();
        std::set<std::string> required;
        const auto K_REF_JSON = [](const AssetRevisionRef& ref) {
            return nlohmann::json { { "product_id", ref.m_product.m_value },
                                   { "revision", ref.m_revision },
                                   { "content_sha256", ref.m_content_sha256 } };
        };
        for (const auto& node : m_graph.m_products) {
            const auto K_ACTION = node.m_manual_action_sha256;
            auto parents = nlohmann::json::array();
            for (const auto& parent : node.m_dependencies) {
                parents.push_back(K_REF_JSON(parent));
            }
            products.push_back({ { "product", K_REF_JSON(node.m_product) },
                                { "dependencies", parents },
                                { "recipe_sha256", node.m_recipe_sha256 },
                                { "manual_action_sha256",
                                 K_ACTION.has_value() ? nlohmann::json(K_ACTION.value())
                                                      : nlohmann::json(nullptr) },
                                { "manual_ancestor", node.m_manual_ancestor } });
            required.insert(node.m_product.m_content_sha256);
            required.insert(node.m_recipe_sha256);
            if (K_ACTION.has_value()) {
                required.insert(K_ACTION.value());
            }
        }
        auto files = nlohmann::json::array();
        for (const auto& sha : required) {
            files.push_back({ { "sha256", sha },
                             { "bytes", m_data.at(sha).size() },
                             { "file", "blobs/" + sha + ".bin" } });
        }
        m_document = { { "schema_version", 1 },
                      { "operation_id", 9 },
                      { "graph_sha256", K_CHECKED->m_graph_sha256 },
                      { "graph",
                       { { "schema_version", 1 },
                        { "world_epoch", 3 },
                        { "pose_revision", 5 },
                        { "products", products } } },
                      { "files", files } };
        save();
    }

    void save() const {
        write(m_root / "source/receipt.json", m_document.dump());
    }

    AssetReceiptSelection selection() const {
        return { m_root / "source",
                sha256_file(m_root / "source/receipt.json", 2uz * 1024uz * 1024uz).value() };
    }
};

void link_file(const std::filesystem::path& target,
               const std::filesystem::path& link,
               bool directory = false) {
    std::error_code error;
    if (directory) {
        std::filesystem::create_directory_symlink(target, link, error);
    } else {
        std::filesystem::create_symlink(target, link, error);
    }
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied) {
        SKIP("Native symbolic-link creation unavailable for this account");
    }
#endif
    REQUIRE_FALSE(error);
}

}  // namespace

TEST_CASE(
    "asset receipt snapshots own verified bytes and completed publication reopens idempotently",
    "[asset-receipt]") {
    ReceiptFixture fixture;
    const auto K_BEFORE = tree(fixture.m_root);
    const auto K_ADMITTED = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->operation() == OperationId { 9 });
    CHECK(K_ADMITTED->file_count() == 4uz);
    CHECK(tree(fixture.m_root) == K_BEFORE);
    // Admission owns the selected bytes; subsequent changes cannot alter them.
    write(fixture.m_root / "source/blobs" / (fixture.m_data.begin()->first + ".bin"),
          "changed after admission");
    const auto K_SOURCE = tree(fixture.m_root / "source");
    const auto K_OUTPUT = fixture.m_root / "published";
    const auto K_WRITTEN =
        publish_asset_receipt(*K_ADMITTED, K_OUTPUT, fixture.m_context, {}, [&](std::size_t count) {
            CHECK(count <= 4uz);
            CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "receipt.json"));
        });
    REQUIRE(K_WRITTEN);
    CHECK(K_WRITTEN->m_receipt_sha256 == K_ADMITTED->receipt_sha256());
    REQUIRE(read_asset_receipt(*K_WRITTEN, fixture.m_context));
    const auto K_PUBLISHED = tree(K_OUTPUT);
    REQUIRE(publish_asset_receipt(*K_ADMITTED, K_OUTPUT, fixture.m_context, {}, [](std::size_t) {
        throw std::runtime_error("Completed reuse must not invoke progress");
    }));
    CHECK(tree(K_OUTPUT) == K_PUBLISHED);
    CHECK(tree(fixture.m_root / "source") == K_SOURCE);
}

TEST_CASE("rehashing an asset receipt cannot bypass schema inventory graph and integer checks",
          "[asset-receipt]") {
    const auto K_FIELD =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22);
    CAPTURE(K_FIELD);
    ReceiptFixture fixture;
    auto& value = fixture.m_document;
    if (K_FIELD == 0) {
        value["operation_id"] = 0;
    }
    if (K_FIELD == 1) {
        value["operation_id"] = true;
    }
    if (K_FIELD == 2) {
        value["extra"] = 1;
    }
    if (K_FIELD == 3) {
        value["schema_version"] = 2;
    }
    if (K_FIELD == 4) {
        value["graph_sha256"] = std::string(64uz, '0');
    }
    if (K_FIELD == 5) {
        value["graph"]["pose_revision"] = 6;
    }
    if (K_FIELD == 6) {
        value["graph"]["products"][1uz]["manual_ancestor"] = false;
    }
    if (K_FIELD == 7) {
        value["files"][0uz]["file"] = "../outside.bin";
    }
    if (K_FIELD == 8) {
        value["files"][0uz]["file"] = "/tmp/outside.bin";
    }
    if (K_FIELD == 9) {
        value["files"][0uz]["bytes"] = 0;
    }
    if (K_FIELD == 10) {
        value["files"][0uz]["bytes"] = 16uz * 1024uz * 1024uz + 1uz;
    }
    if (K_FIELD == 11) {
        value["files"][0uz]["bytes"] = std::numeric_limits<std::uint64_t>::max();
    }
    if (K_FIELD == 12) {
        value["files"][0uz]["bytes"] = 1.5;
    }
    if (K_FIELD == 13) {
        value["files"][0uz] = value["files"][1uz];
    }
    if (K_FIELD == 14) {
        value["files"].erase(0uz);
    }
    if (K_FIELD == 15) {
        value["files"].push_back(value["files"][0uz]);
    }
    if (K_FIELD == 16) {
        value["graph"]["products"][1uz]["dependencies"][0uz]["revision"] = 2;
    }
    if (K_FIELD == 17) {
        value["graph"]["products"][0uz]["product"]["product_id"] = 1.0;
    }
    if (K_FIELD == 18) {
        value["graph"]["products"][0uz]["recipe_sha256"] = "invalid";
    }
    if (K_FIELD == 19) {
        value["files"][0uz]["sha256"] = std::string(64uz, 'z');
    }
    fixture.save();
    if (K_FIELD == 20) {
        write(fixture.m_root / "source/receipt.json",
              "{\"schema_version\":1," + value.dump().substr(1uz));
    }
    if (K_FIELD == 21) {
        write(fixture.m_root / "source/receipt.json", std::string(1024uz * 1024uz + 1uz, ' '));
    }
    if (K_FIELD == 22) {
        write(fixture.m_root / "source/receipt.json",
              std::string(20uz, '[') + "0" + std::string(20uz, ']'));
    }
    const auto K_BEFORE = tree(fixture.m_root);
    CHECK_FALSE(read_asset_receipt(fixture.selection(), fixture.m_context));
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("asset receipt pins and opaque file bytes are checked without changing inputs",
          "[asset-receipt]") {
    ReceiptFixture fixture;
    auto selection = fixture.selection();
    auto expected = fixture.m_context;
    std::stop_source stop;
    SECTION("wrong selected digest") {
        selection.m_receipt_sha256 = std::string(64uz, '0');
    }
    SECTION("unselected pose") {
        expected.m_pose_revision = {};
    }
    SECTION("wrong epoch") {
        ++expected.m_world_epoch.m_value;
    }
    SECTION("pre-cancelled") {
        stop.request_stop();
    }
    SECTION("changed blob") {
        write(fixture.m_root / "source/blobs" / (fixture.m_data.begin()->first + ".bin"), "bad");
    }
    SECTION("missing blob") {
        std::filesystem::remove(fixture.m_root / "source/blobs" /
                                (fixture.m_data.begin()->first + ".bin"));
    }
    const auto K_BEFORE = tree(fixture.m_root);
    CHECK_FALSE(read_asset_receipt(selection, expected, stop.get_token()));
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("asset receipt publication preserves incomplete installs and resumes after cancellation "
          "or exceptions",
          "[asset-receipt]") {
    const auto K_THROW = GENERATE(false, true);
    ReceiptFixture fixture;
    const auto K_SNAPSHOT = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE(K_SNAPSHOT);
    const auto K_OUTPUT = fixture.m_root / "published";
    std::stop_source stop;
    const auto K_FAILED = publish_asset_receipt(
        *K_SNAPSHOT, K_OUTPUT, fixture.m_context, stop.get_token(), [&](std::size_t count) {
            REQUIRE(count == 1uz);
            if (K_THROW) {
                throw std::runtime_error("synthetic observer failure");
            }
            stop.request_stop();
        });
    REQUIRE_FALSE(K_FAILED);
    CHECK(K_FAILED.error().m_code == (K_THROW ? ErrorCode::BACKEND_FAILURE : ErrorCode::CANCELLED));
    CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "receipt.json"));
    CHECK(std::filesystem::exists(K_OUTPUT / "owner.json"));
    const auto K_PARTIAL = tree(K_OUTPUT / "blobs");
    REQUIRE(K_PARTIAL.size() == 1uz);
    const auto K_RETRY = publish_asset_receipt(*K_SNAPSHOT, K_OUTPUT, fixture.m_context);
    REQUIRE(K_RETRY);
    for (const auto& [K_NAME, K_SHA] : K_PARTIAL) {
        CHECK(sha256_file(K_OUTPUT / "blobs" / K_NAME, 1024uz).value() == K_SHA);
    }
    REQUIRE(read_asset_receipt(*K_RETRY, fixture.m_context));
}

TEST_CASE("asset receipt writer rejects foreign corrupt changed-context and colliding requests",
          "[asset-receipt]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    ReceiptFixture fixture;
    const auto K_ADMITTED = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE(K_ADMITTED);
    const auto K_OUTPUT = fixture.m_root / "published";
    if (K_FIELD == 0) {
        std::filesystem::create_directory(K_OUTPUT);
        write(K_OUTPUT / "user.txt", "preserve this caller file");
    } else {
        REQUIRE(publish_asset_receipt(*K_ADMITTED, K_OUTPUT, fixture.m_context));
    }
    auto selected = *K_ADMITTED;
    auto expected = fixture.m_context;
    if (K_FIELD == 1) {
        write(K_OUTPUT / "owner.json", "foreign owner");
    }
    if (K_FIELD == 2) {
        write(K_OUTPUT / "receipt.json", "corrupt completion");
    }
    if (K_FIELD == 3) {
        std::filesystem::remove(K_OUTPUT / "blobs" / (fixture.m_data.begin()->first + ".bin"));
    }
    if (K_FIELD == 4) {
        fixture.m_document["operation_id"] = 10;
        fixture.save();
        const auto K_OTHER = read_asset_receipt(fixture.selection(), fixture.m_context);
        REQUIRE(K_OTHER);
        selected = *K_OTHER;
    }
    if (K_FIELD == 5) {
        write(fixture.m_root / "source/receipt.json", fixture.m_document.dump(2));
        const auto K_OTHER = read_asset_receipt(fixture.selection(), fixture.m_context);
        REQUIRE(K_OTHER);
        selected = *K_OTHER;
    }
    if (K_FIELD == 6) {
        ++expected.m_pose_revision.m_value;
    }
    if (K_FIELD == 7) {
        write(K_OUTPUT / "blobs" / (fixture.m_data.begin()->first + ".bin"), "corrupt blob");
    }
    const auto K_BEFORE = tree(fixture.m_root);
    CHECK_FALSE(publish_asset_receipt(selected, K_OUTPUT, expected));
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("asset receipt symlinks and overlapping output trees never redirect file access",
          "[asset-receipt]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    ReceiptFixture fixture;
    const auto K_SELECTED = fixture.selection();
    const auto K_ADMITTED = read_asset_receipt(K_SELECTED, fixture.m_context);
    REQUIRE(K_ADMITTED);
    auto source = K_SELECTED;
    auto output = fixture.m_root / "published";
    if (K_FIELD == 0) {
        link_file(K_SELECTED.m_directory, fixture.m_root / "alias", true);
        source.m_directory = fixture.m_root / "alias";
    }
    if (K_FIELD == 1) {
        std::filesystem::rename(K_SELECTED.m_directory / "blobs", fixture.m_root / "outside");
        link_file(fixture.m_root / "outside", K_SELECTED.m_directory / "blobs", true);
    }
    if (K_FIELD == 2) {
        const auto K_PATH =
            K_SELECTED.m_directory / "blobs" / (fixture.m_data.begin()->first + ".bin");
        std::filesystem::rename(K_PATH, fixture.m_root / "outside.bin");
        link_file(fixture.m_root / "outside.bin", K_PATH);
    }
    if (K_FIELD == 3) {
        std::filesystem::rename(K_SELECTED.m_directory / "receipt.json",
                                fixture.m_root / "outside.json");
        link_file(fixture.m_root / "outside.json", K_SELECTED.m_directory / "receipt.json");
    }
    if (K_FIELD == 4) {
        std::filesystem::create_directory(fixture.m_root / "outside");
        link_file(fixture.m_root / "outside", output, true);
    }
    if (K_FIELD == 5) {
        output = K_SELECTED.m_directory / "nested";
    }
    if (K_FIELD == 6) {
        output = fixture.m_root;
    }
    const auto K_BEFORE = tree(fixture.m_root);
    if (K_FIELD < 4) {
        CHECK_FALSE(read_asset_receipt(source, fixture.m_context));
    } else {
        CHECK_FALSE(publish_asset_receipt(*K_ADMITTED, output, fixture.m_context));
    }
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("asset receipt writer survives an owned process crash after the first immutable blob",
          "[asset-receipt]") {
    ReceiptFixture fixture;
    const auto K_SELECTION = fixture.selection();
    const auto K_OUTPUT = fixture.m_root / "published";
    const auto K_BEFORE = tree(K_SELECTION.m_directory);
    std::string diagnostic;
    const auto K_EXIT = execute_process({ WE_TEST_ASSET_RECEIPT_PROBE,
                                         K_SELECTION.m_directory.string(),
                                         K_SELECTION.m_receipt_sha256,
                                         K_OUTPUT.string(),
                                         "crash" },
                                        {},
                                        {},
                                        [&](const std::string& value) {
                                            diagnostic += value;
                                        });
    INFO(diagnostic);
    REQUIRE(K_EXIT);
    CHECK(*K_EXIT == 77);
    CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "receipt.json"));
    CHECK(tree(K_OUTPUT / "blobs").size() == 1uz);
    const auto K_ADMITTED = read_asset_receipt(K_SELECTION, fixture.m_context);
    REQUIRE(K_ADMITTED);
    REQUIRE(publish_asset_receipt(*K_ADMITTED, K_OUTPUT, fixture.m_context));
    CHECK(tree(K_SELECTION.m_directory) == K_BEFORE);
}

TEST_CASE("asset receipt file and aggregate byte bounds admit their exact limits",
          "[asset-receipt]") {
    ReceiptFixture fixture;
    constexpr std::size_t K_LIMIT { 16uz * 1024uz * 1024uz };
    fixture.m_graph.m_products[0uz].m_product.m_content_sha256 =
        fixture.add(std::string(K_LIMIT, 'a'));
    fixture.m_graph.m_products[1uz].m_product.m_content_sha256 =
        fixture.add(std::string(K_LIMIT, 'b'));
    const auto K_RECIPE = fixture.add(std::string(K_LIMIT, 'c'));
    const auto K_ACTION = fixture.add(std::string(K_LIMIT, 'd'));
    fixture.m_graph.m_products[0uz].m_recipe_sha256 = K_RECIPE;
    fixture.m_graph.m_products[1uz].m_recipe_sha256 = K_RECIPE;
    fixture.m_graph.m_products[0uz].m_manual_action_sha256 = K_ACTION;
    fixture.m_graph.m_products[1uz].m_dependencies = { fixture.m_graph.m_products[0uz].m_product };
    fixture.document();
    auto text = fixture.m_document.dump();
    text.resize(1024uz * 1024uz, ' ');
    write(fixture.m_root / "source/receipt.json", text);
    REQUIRE(read_asset_receipt(fixture.selection(), fixture.m_context));
    SECTION("per-file excess") {
        fixture.m_document["files"][0uz]["bytes"] = K_LIMIT + 1uz;
        fixture.save();
    }
    SECTION("aggregate excess") {
        const auto K_CONTENT = fixture.add("fifth referenced blob exceeds aggregate limit");
        fixture.m_graph.m_products.push_back({ { { 3 }, 1, K_CONTENT }, {}, K_RECIPE, {}, false });
        fixture.document();
    }
    const auto K_REJECTED = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("asset receipt exact file cardinality is bounded independently of declared bytes",
          "[asset-receipt]") {
    ReceiptFixture fixture;
    fixture.m_graph.m_products.clear();
    for (std::size_t index { 0uz }; index < 256uz; ++index) {
        const auto K_SUFFIX = std::to_string(index);
        const auto K_CONTENT = fixture.add("synthetic content " + K_SUFFIX);
        const auto K_RECIPE = fixture.add("synthetic recipe " + K_SUFFIX);
        const auto K_ACTION = fixture.add("synthetic action " + K_SUFFIX);
        fixture.m_graph.m_products.push_back(
            { { { index + 1uz }, 1, K_CONTENT }, {}, K_RECIPE, K_ACTION, true });
    }
    fixture.document();
    const auto K_ADMITTED = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE(K_ADMITTED);
    CHECK(K_ADMITTED->file_count() == 768uz);
    fixture.m_document["files"].push_back(fixture.m_document["files"][0uz]);
    fixture.save();
    const auto K_EXCESS = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE_FALSE(K_EXCESS);
    CHECK(K_EXCESS.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("asset receipt cancellation context changes and writer exclusion stop completion",
          "[asset-receipt]") {
    ReceiptFixture fixture;
    const auto K_ADMITTED = read_asset_receipt(fixture.selection(), fixture.m_context);
    REQUIRE(K_ADMITTED);
    const auto K_OUTPUT = fixture.m_root / "published";
    std::stop_source stop;
    SECTION("cancellation before output ownership") {
        stop.request_stop();
        const auto K_REJECTED =
            publish_asset_receipt(*K_ADMITTED, K_OUTPUT, fixture.m_context, stop.get_token());
        REQUIRE_FALSE(K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::CANCELLED);
        CHECK_FALSE(std::filesystem::exists(K_OUTPUT));
    }
    SECTION("context changed after an installed blob and concurrent writer refused") {
        auto current = fixture.m_context;
        const auto K_REJECTED =
            publish_asset_receipt(*K_ADMITTED, K_OUTPUT, current, {}, [&](std::size_t count) {
                if (count == 1uz) {
                    const auto K_OTHER = publish_asset_receipt(*K_ADMITTED, K_OUTPUT, current);
                    REQUIRE_FALSE(K_OTHER);
                    CHECK(K_OTHER.error().m_code == ErrorCode::WOULD_BLOCK);
                    ++current.m_pose_revision.m_value;
                }
            });
        REQUIRE_FALSE(K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::RESYNC_REQUIRED);
        CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "receipt.json"));
        REQUIRE(publish_asset_receipt(*K_ADMITTED, K_OUTPUT, fixture.m_context));
    }
}

TEST_CASE("capture receipt profile requires an independent version pin and bounds declarations",
          "[asset-receipt][capture-v2]") {
    ReceiptFixture fixture;
    fixture.m_document["schema_version"] = 2;
    fixture.save();
    auto selected = fixture.selection();
    CHECK(!read_asset_receipt(selected, fixture.m_context));
    selected.m_profile = AssetReceiptProfile::CAPTURE_V2;
    const auto K_RECEIPT = read_asset_receipt(selected, fixture.m_context);
    REQUIRE(K_RECEIPT);
    CHECK(K_RECEIPT->profile() == AssetReceiptProfile::CAPTURE_V2);
    const auto K_HASH = fixture.m_graph.m_products[0uz].m_product.m_content_sha256;
    const auto K_SHARED = K_RECEIPT->share_blob(K_HASH, 1024uz);
    REQUIRE(K_SHARED);
    CHECK(K_SHARED->data() == K_RECEIPT->share_blob(K_HASH, 1024uz)->data());
    CHECK(sha256(*K_SHARED) == K_HASH);
    CHECK(!K_RECEIPT->share_blob(K_HASH, 1uz));
    CHECK(!K_RECEIPT->share_blob(hash_text("unknown"), 1024uz));
    std::stop_source stop;
    stop.request_stop();
    CHECK(!K_RECEIPT->share_blob(K_HASH, 1024uz, stop.get_token()));
    const auto K_OUTPUT =
        publish_asset_receipt(*K_RECEIPT, fixture.m_root / "v2", fixture.m_context);
    REQUIRE(K_OUTPUT);
    CHECK(K_OUTPUT->m_profile == AssetReceiptProfile::CAPTURE_V2);
    CHECK(read_asset_receipt(*K_OUTPUT, fixture.m_context));
    CHECK(publish_asset_receipt(*K_RECEIPT, fixture.m_root / "v2", fixture.m_context));
    fixture.m_document["files"][0uz]["bytes"] = 16uz * 1024uz * 1024uz + 1uz;
    fixture.save();
    selected.m_receipt_sha256 = fixture.selection().m_receipt_sha256;
    const auto K_BAD = read_asset_receipt(selected, fixture.m_context);
    REQUIRE(!K_BAD);
    CHECK(K_BAD.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}

TEST_CASE("shared receipt blob owns only its immutable bytes after source and receipt destruction",
          "[asset-receipt][capture-v2]") {
    const auto K_BLOB = [] {
        ReceiptFixture fixture;
        const auto K_RECEIPT = read_asset_receipt(fixture.selection(), fixture.m_context);
        REQUIRE(K_RECEIPT);
        const auto K_SHARED = K_RECEIPT->share_blob(
            fixture.m_graph.m_products[0uz].m_product.m_content_sha256, 1024uz);
        REQUIRE(K_SHARED);
        write(fixture.m_root / "source/blobs" /
                  (fixture.m_graph.m_products[0uz].m_product.m_content_sha256 + ".bin"),
              "changed");
        return *K_SHARED;
    }();
    CHECK(sha256(K_BLOB) == hash_text("synthetic source bytes"));
}

TEST_CASE("receipt profiles reject aggregate declarations before any oversized payload read",
          "[asset-receipt][capture-v2]") {
    const auto K_PROFILE = GENERATE(AssetReceiptProfile::SMALL_V1, AssetReceiptProfile::CAPTURE_V2);
    ReceiptFixture fixture;
    for (std::size_t index { 0uz }; index < 49uz; ++index) {
        const auto K_HASH = fixture.add("synthetic aggregate declaration " + std::to_string(index));
        fixture.m_graph.m_products.push_back({ { { 100uz + index }, 1, K_HASH },
                                              {},
                                              fixture.m_graph.m_products[0uz].m_recipe_sha256,
                                              {},
                                              false });
    }
    fixture.document();
    fixture.m_document["schema_version"] = static_cast<std::uint32_t>(K_PROFILE);
    for (auto& file : fixture.m_document["files"]) {
        file["bytes"] = 16uz * 1024uz * 1024uz;
    }
    fixture.save();
    auto selected = fixture.selection();
    selected.m_profile = K_PROFILE;
    const auto K_RESULT = read_asset_receipt(selected, fixture.m_context);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}
