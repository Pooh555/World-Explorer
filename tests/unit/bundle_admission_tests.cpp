#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <bit>
#include <chrono>
#include <fstream>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    std::string bytes {std::istreambuf_iterator<char>(file), {}};
    REQUIRE(!file.bad());
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    REQUIRE(file);
}

std::string digest(std::string_view bytes) {
    return sha256(std::as_bytes(std::span(bytes)));
}

using FileTree = std::map<std::string, std::string>;

FileTree tree(const std::filesystem::path& root) {
    FileTree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = entry.path().lexically_relative(root).generic_string();
        const auto K_STATUS = entry.symlink_status();
        if (std::filesystem::is_symlink(K_STATUS))
            result.emplace(K_NAME,
                           "link:" + std::filesystem::read_symlink(entry.path()).generic_string());
        else if (std::filesystem::is_directory(K_STATUS)) result.emplace(K_NAME, "directory");
        else {
            REQUIRE(std::filesystem::is_regular_file(K_STATUS));
            REQUIRE(entry.file_size() <= 2uz * 1024 * 1024);
            result.emplace(K_NAME, "file:" + read_bytes(entry.path()));
        }
    }
    return result;
}

void create_link(const std::filesystem::path& target, const std::filesystem::path& link) {
    std::error_code error;
    std::filesystem::create_symlink(target, link, error);
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied)
        SKIP("Native symbolic-link creation is unavailable to this Windows account");
#endif
    INFO(error.message());
    REQUIRE(!error);
}

void put_integer(std::span<char> bytes, std::uint64_t value) {
    REQUIRE(bytes.size() <= 8);
    for (auto& byte : bytes) {
        byte = static_cast<char>(value & 255U);
        value >>= 8U;
    }
}

struct IndexEntry {
    std::size_t m_offset;
    std::string m_name;
};

std::vector<IndexEntry> index_entries(const std::string& bytes) {
    std::vector<IndexEntry> entries;
    for (std::size_t offset { 16uz }; offset < bytes.size();) {
        REQUIRE(offset + 84 <= bytes.size());
        std::uint32_t length {};
        for (std::size_t index { 0uz }; index < 4uz; ++index)
            length |=
                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 80 + index]))
                << (index * 8);
        REQUIRE(length <= bytes.size() - offset - 84);
        entries.push_back({offset, bytes.substr(offset + 84, length)});
        offset += 84 + length;
    }
    return entries;
}

std::shared_ptr<SceneState> synthetic_scene() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 42 };
    scene->m_epoch = { 3 };
    scene->m_revision = { 7 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "bundle-admission" } };
    for (std::uint64_t id { 1 }; id <= 2; ++id) {
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { id };
        chunk->m_generation = 4;
        chunk->m_pose_revision = { 5 };
        chunk->m_bounds = { { -10, -10, -10 }, { 10, 10, 10 } };
        chunk->m_source_frames = { { 11 }, { 12 } };
        chunk->m_splats.emplace_back();
        chunk->m_splats[0].m_mean = { static_cast<float>(id), 2, 3 };
        chunk->m_splats[0].m_sh_degree = 3;
        chunk->m_splats[0].m_sh[47] = .75F;
        scene->m_chunks.emplace(chunk->m_id, chunk);
    }
    return scene;
}

struct BundleFixture {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_bundle_admission_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path m_directory = m_root / "synthetic.wexplore";
    SceneSnapshot m_scene = synthetic_scene();

    BundleFixture() {
        REQUIRE(write_bundle(m_directory, m_scene));
        write_bytes(m_root / "caller.txt", "unrelated caller data");
    }

    ~BundleFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    nlohmann::json manifest() const {
        return nlohmann::json::parse(read_bytes(m_directory / "manifest.json"));
    }

    void manifest(const nlohmann::json& value) const {
        write_bytes(m_directory / "manifest.json", value.dump());
    }

    std::string index() const {
        return read_bytes(m_directory / manifest().at("index").get<std::string>());
    }

    void index(std::string_view bytes) const {
        auto value = manifest();
        const auto K_HASH = digest(bytes), K_NAME = "index_" + K_HASH + ".bin";
        write_bytes(m_directory / K_NAME, bytes);
        value["index"] = K_NAME;
        value["index_sha256"] = K_HASH;
        value["index_bytes"] = bytes.size();
        manifest(value);
    }

    void chunk(std::size_t number, std::string_view bytes) const {
        auto encoded = index();
        const auto K_ENTRIES = index_entries(encoded);
        REQUIRE(number < K_ENTRIES.size());
        const auto K_HASH = digest(bytes), K_NAME = "chunk_" + K_HASH + ".bin";
        REQUIRE(K_NAME.size() == K_ENTRIES[number].m_name.size());
        write_bytes(m_directory / K_NAME, bytes);
        put_integer(std::span(encoded).subspan(K_ENTRIES[number].m_offset + 8, 8), bytes.size());
        encoded.replace(K_ENTRIES[number].m_offset + 16, 64, K_HASH);
        encoded.replace(K_ENTRIES[number].m_offset + 84, K_NAME.size(), K_NAME);
        index(encoded);
    }

    void provenance(const nlohmann::json& value) const {
        const auto K_BYTES = value.dump(), K_HASH = digest(K_BYTES),
                   K_NAME = "provenance_" + K_HASH + ".json";
        write_bytes(m_directory / K_NAME, K_BYTES);
        auto metadata = manifest();
        metadata["provenance"] = K_NAME;
        metadata["provenance_sha256"] = K_HASH;
        manifest(metadata);
    }

    void repair(const FileTree& original) const {
        // Explicit repair of this fresh, solely test-owned tree after checking reader preservation.
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directory(m_root);
        for (const auto& [K_NAME, K_VALUE] : original)
            if (K_VALUE == "directory") std::filesystem::create_directories(m_root / K_NAME);
            else {
                REQUIRE(K_VALUE.starts_with("file:"));
                std::filesystem::create_directories((m_root / K_NAME).parent_path());
                write_bytes(m_root / K_NAME, std::string_view(K_VALUE).substr(5));
            }
    }
};

template <class Mutation> void reject_and_repair(ErrorCode code, Mutation mutate) {
    BundleFixture fixture;
    const auto K_RETAINED = load_bundle(fixture.m_directory);
    REQUIRE(K_RETAINED);
    const auto K_HASH = scene_content_hash(**K_RETAINED);
    const auto K_ORIGINAL = tree(fixture.m_root);
    BundleLimits limits;
    mutate(fixture, limits);
    const auto K_BEFORE = tree(fixture.m_root);
    const auto K_REJECTED = load_bundle(fixture.m_directory, limits);
    INFO((K_REJECTED ? "unexpected load" : K_REJECTED.error().m_message));
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == code);
    CHECK(tree(fixture.m_root) == K_BEFORE);
    CHECK(scene_content_hash(**K_RETAINED) == K_HASH);
    fixture.repair(K_ORIGINAL);
    const auto K_REPAIRED = load_bundle(fixture.m_directory);
    REQUIRE(K_REPAIRED);
    CHECK(scene_content_hash(**K_REPAIRED) == K_HASH);
    CHECK(scene_content_hash(*fixture.m_scene) == K_HASH);
    CHECK(tree(fixture.m_root) == K_ORIGINAL);
}

}  // namespace

TEST_CASE("bundle manifests reject unsupported conventions and invalid typed identities",
          "[bundle_admission][manifest]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    CAPTURE(K_FIELD);
    reject_and_repair(
        K_FIELD == 0 ? ErrorCode::UNSUPPORTED_OPERATION : ErrorCode::INVALID_DATA,
        [K_FIELD](const BundleFixture& fixture, BundleLimits&) {
            auto value = fixture.manifest();
            constexpr std::array K_KEYS {"handedness",
                                         "world_up",
                                         "camera_axes",
                                         "pose_convention",
                                         "quaternion_order",
                                         "pixel_centers"};
            if (K_FIELD == 0) value["schema_version"] = 2;
            if (K_FIELD >= 1 && K_FIELD <= 6)
                value["coordinates"][K_KEYS[static_cast<std::size_t>(K_FIELD - 1)]] = "unsupported";
            constexpr std::array K_APPEARANCE {
                "representation", "lighting", "color_domain", "sh_basis"};
            if (K_FIELD >= 7 && K_FIELD <= 10)
                value["appearance"][K_APPEARANCE[static_cast<std::size_t>(K_FIELD - 7)]] =
                    "unsupported";
            if (K_FIELD == 11) value["compression"] = "unknown";
            if (K_FIELD == 12) value["epoch"] = -1;
            if (K_FIELD == 13) value["revision"] = 1.5;
            if (K_FIELD == 14) value["scene_id"] = 0;
            fixture.manifest(value);
        });
}

TEST_CASE("bundle referenced paths reject invalid names and linked assets without writes",
          "[bundle_admission][paths]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(K_FIELD);
    reject_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](const BundleFixture& fixture, BundleLimits&) {
            auto value = fixture.manifest();
            constexpr std::array K_NAMES {
                "", ".", "..", "../outside.bin", "/absolute.bin", "nested/index.bin"};
            if (K_FIELD < 6) value["index"] = K_NAMES[static_cast<std::size_t>(K_FIELD)];
            if (K_FIELD == 6) value["index"] = std::string(129, 'a');
            if (K_FIELD == 7) value["provenance"] = "bad\\path.json";
            fixture.manifest(value);
            if (K_FIELD >= 8) {
                const auto K_NAME =
                    K_FIELD == 8 ? "manifest.json" : value.at("index").get<std::string>();
                const auto K_PATH = fixture.m_directory / K_NAME;
                std::filesystem::copy_file(K_PATH, fixture.m_root / "caller asset");
                std::filesystem::remove(K_PATH);
                create_link(fixture.m_root / "caller asset", K_PATH);
            }
        });
}

TEST_CASE("bundle provenance schema typed metadata and record limits survive rejected load",
          "[bundle_admission][provenance]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(K_FIELD);
    reject_and_repair(K_FIELD >= 4 ? ErrorCode::RESOURCE_EXHAUSTED : ErrorCode::INVALID_DATA,
                      [K_FIELD](const BundleFixture& fixture, BundleLimits&) {
                          auto value = nlohmann::json::parse(
                              read_bytes(fixture.m_directory /
                                         fixture.manifest().at("provenance").get<std::string>()));
                          if (K_FIELD == 0) value["schema_version"] = 2;
                          if (K_FIELD == 1) value.erase("schema_version");
                          if (K_FIELD == 2) value["metadata"] = nlohmann::json::array();
                          if (K_FIELD == 3) value["metadata"]["fixture"] = 42;
                          if (K_FIELD == 4) value["metadata"][""] = "empty key";
                          if (K_FIELD == 5) value["metadata"][std::string(129, 'x')] = "long key";
                          if (K_FIELD == 6) value["metadata"]["large"] = std::string(4097, 'x');
                          if (K_FIELD == 7)
                              for (unsigned index { 0 }; index < 33; ++index)
                                  value["metadata"][std::to_string(index)] = "value";
                          fixture.provenance(value);
                      });
}

TEST_CASE("bundle rehashed indexes enforce size digest identity and aggregate budgets",
          "[bundle_admission][index]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    reject_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](const BundleFixture& fixture, BundleLimits& limits) {
            auto bytes = fixture.index();
            const auto K_ENTRIES = index_entries(bytes);
            REQUIRE(K_ENTRIES.size() == 2);
            if (K_FIELD == 0) bytes[K_ENTRIES[0].m_offset + 16] = 'g';
            if (K_FIELD == 1) limits.m_scene.m_max_payload_bytes = 1;
            if (K_FIELD == 2)
                bytes[K_ENTRIES[0].m_offset + 16] =
                    bytes[K_ENTRIES[0].m_offset + 16] == '0' ? '1' : '0';
            if (K_FIELD == 3)
                put_integer(std::span(bytes).subspan(K_ENTRIES[0].m_offset + 8, 8),
                            read_bytes(fixture.m_directory / K_ENTRIES[0].m_name).size() + 1);
            if (K_FIELD == 4) limits.m_scene.m_max_splats = 1;
            if (K_FIELD == 5)
                limits.m_scene.m_max_payload_bytes =
                    read_bytes(fixture.m_directory / K_ENTRIES[0].m_name).size() +
                    read_bytes(fixture.m_directory / K_ENTRIES[1].m_name).size() - 1;
            fixture.index(bytes);
        });
}

TEST_CASE("bundle rehashed payload validation preserves leased snapshots on corrupt numerics",
          "[bundle_admission][payload]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    reject_and_repair(
        ErrorCode::INVALID_DATA, [K_FIELD](const BundleFixture& fixture, BundleLimits&) {
            const auto K_ENTRIES = index_entries(fixture.index());
            auto bytes = read_bytes(fixture.m_directory / K_ENTRIES[0].m_name);
            constexpr std::size_t K_SPLAT_OFFSET { 168uz + 16uz };
            if (K_FIELD == 0)
                put_integer(std::span(bytes).subspan(40, 8), std::bit_cast<std::uint64_t>(0.0));
            if (K_FIELD == 1)
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET, 4),
                            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
            if (K_FIELD == 2) put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET + 12, 4), 0);
            if (K_FIELD == 3)
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET + 40, 4),
                            std::bit_cast<std::uint32_t>(1.1F));
            if (K_FIELD == 4)
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET + 44, 4),
                            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
            if (K_FIELD == 5) put_integer(std::span(bytes).subspan(160, 8), 1000);
            if (K_FIELD == 6) bytes.push_back('x');
            fixture.chunk(0, bytes);
        });
}

TEST_CASE("bundle exact encoded and decoded budgets retain scene content and provenance",
          "[bundle_admission][budget]") {
    BundleFixture fixture;
    auto scene = std::make_shared<SceneState>(*fixture.m_scene);
    scene->m_scale = ScaleStatus::METRIC_MEASURED;
    scene->m_provenance[std::string(128, 'k')] = std::string(4096, 'v');
    REQUIRE(write_bundle(fixture.m_directory, scene));
    BundleLimits limits;
    limits.m_scene.m_max_chunks = 2;
    limits.m_scene.m_max_splats = 2;
    limits.m_scene.m_max_source_frames = 2;
    const auto K_VALUE = fixture.manifest();
    limits.m_max_manifest_bytes = std::max(
        read_bytes(fixture.m_directory / "manifest.json").size(),
        read_bytes(fixture.m_directory / K_VALUE.at("provenance").get<std::string>()).size());
    limits.m_scene.m_max_payload_bytes = 0;
    for (const auto& entry : index_entries(fixture.index()))
        limits.m_scene.m_max_payload_bytes += read_bytes(fixture.m_directory / entry.m_name).size();
    const auto K_BEFORE = tree(fixture.m_root);
    const auto K_LOADED = load_bundle(fixture.m_directory, limits);
    REQUIRE(K_LOADED);
    CHECK(scene_content_hash(**K_LOADED) == scene_content_hash(*scene));
    CHECK((*K_LOADED)->m_scale == ScaleStatus::METRIC_MEASURED);
    --limits.m_scene.m_max_payload_bytes;
    const auto K_REJECTED = load_bundle(fixture.m_directory, limits);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("bundle failed publication retains old manifest and permits corrected publication",
          "[bundle_admission][publication]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3);
    CAPTURE(K_FIELD);
    BundleFixture fixture;
    const auto K_RETAINED = load_bundle(fixture.m_directory);
    REQUIRE(K_RETAINED);
    const auto K_HASH = scene_content_hash(**K_RETAINED);
    auto replacement = std::make_shared<SceneState>(*fixture.m_scene);
    replacement->m_revision = { 8 };
    auto chunk = std::make_shared<SceneChunk>(*replacement->m_chunks.at({1}));
    chunk->m_generation = 5;
    chunk->m_splats[0].m_mean[0] = 4;
    replacement->m_chunks[{1}] = chunk;
    const auto K_REFERENCE = fixture.m_root / "replacement reference";
    REQUIRE(write_bundle(K_REFERENCE, replacement));
    const auto K_EXPECTED = nlohmann::json::parse(read_bytes(K_REFERENCE / "manifest.json"));
    const auto K_ENTRIES =
        index_entries(read_bytes(K_REFERENCE / K_EXPECTED.at("index").get<std::string>()));
    const auto K_NAME = K_FIELD == 0   ? K_ENTRIES[0].m_name
                        : K_FIELD == 1 ? K_EXPECTED.at("index").get<std::string>()
                        : K_FIELD == 2 ? K_EXPECTED.at("provenance").get<std::string>()
                                       : "manifest.json";
    const auto K_OBSTRUCTION = fixture.m_directory / K_NAME;
    const auto K_SAVED = tree(fixture.m_root);
    if (std::filesystem::exists(K_OBSTRUCTION))
        std::filesystem::rename(K_OBSTRUCTION, fixture.m_root / "retained asset");
    std::filesystem::create_directory(K_OBSTRUCTION);
    const auto K_BEFORE = tree(fixture.m_root);
    const auto K_FAILED = write_bundle(fixture.m_directory, replacement);
    REQUIRE(!K_FAILED);
    CHECK(K_FAILED.error().m_code == ErrorCode::IO_ERROR);
    const auto K_AFTER = tree(fixture.m_root);
    for (const auto& [K_PATH, K_BYTES] : K_BEFORE) {
        REQUIRE(K_AFTER.contains(K_PATH));
        CHECK(K_AFTER.at(K_PATH) == K_BYTES);
    }
    for (const auto& [K_PATH, K_BYTES] : K_AFTER) {
        if (K_BEFORE.contains(K_PATH)) continue;
        const auto K_FILENAME = std::filesystem::path(K_PATH).filename();
        CHECK(std::filesystem::path(K_PATH).parent_path() == fixture.m_directory.filename());
        CHECK(K_BYTES == "file:" + read_bytes(K_REFERENCE / K_FILENAME));
        CHECK(K_FILENAME.string().find(".tmp.") == std::string::npos);
    }
    CHECK(scene_content_hash(**K_RETAINED) == K_HASH);
    std::filesystem::remove(K_OBSTRUCTION);
    if (std::filesystem::exists(fixture.m_root / "retained asset"))
        std::filesystem::rename(fixture.m_root / "retained asset", K_OBSTRUCTION);
    const auto K_OLD = load_bundle(fixture.m_directory);
    REQUIRE(K_OLD);
    CHECK(scene_content_hash(**K_OLD) == K_HASH);
    REQUIRE(write_bundle(fixture.m_directory, replacement));
    const auto K_PUBLISHED = load_bundle(fixture.m_directory);
    REQUIRE(K_PUBLISHED);
    CHECK(scene_content_hash(**K_PUBLISHED) == scene_content_hash(*replacement));
    CHECK(scene_content_hash(**K_RETAINED) == K_HASH);
    const auto K_COMMITTED = tree(fixture.m_root);
    REQUIRE(write_bundle(fixture.m_directory, replacement));
    CHECK(tree(fixture.m_root) == K_COMMITTED);
    CHECK(read_bytes(fixture.m_root / "caller.txt") ==
          std::string_view(K_SAVED.at("caller.txt")).substr(5));
}

TEST_CASE("bundle rejected native snapshots do not alter caller files or consume publication",
          "[bundle_admission][publication]") {
    const auto K_FIELD = GENERATE(0, 1, 2);
    BundleFixture fixture;
    auto state = std::make_shared<SceneState>(*fixture.m_scene);
    SceneSnapshot rejected = state;
    if (K_FIELD == 0) rejected.reset();
    if (K_FIELD == 1) state->m_id = {};
    if (K_FIELD == 2) state->m_provenance[""] = "invalid key";
    const auto K_BEFORE = tree(fixture.m_root);
    const auto K_FAILED = write_bundle(fixture.m_directory, rejected);
    REQUIRE(!K_FAILED);
    CHECK(K_FAILED.error().m_code == (K_FIELD == 0   ? ErrorCode::INVALID_ARGUMENT
                                      : K_FIELD == 1 ? ErrorCode::INVALID_DATA
                                                     : ErrorCode::RESOURCE_EXHAUSTED));
    CHECK(tree(fixture.m_root) == K_BEFORE);
    REQUIRE(write_bundle(fixture.m_directory, fixture.m_scene));
    CHECK(tree(fixture.m_root) == K_BEFORE);
}
