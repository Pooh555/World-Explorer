#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <bit>
#include <chrono>
#include <fstream>
#include <limits>

using namespace world_explorer;

namespace {

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    std::string bytes { std::istreambuf_iterator<char>(file), {} };
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
        if (std::filesystem::is_symlink(K_STATUS)) {
            result.emplace(K_NAME,
                           "link:" + std::filesystem::read_symlink(entry.path()).generic_string());
        } else if (std::filesystem::is_directory(K_STATUS)) {
            result.emplace(K_NAME, "directory");
        } else {
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
        for (std::size_t index { 0uz }; index < 4uz; ++index) {
            length |=
                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 80 + index]))
                << (index * 8);
        }
        REQUIRE(length <= bytes.size() - offset - 84);
        entries.push_back({ offset, bytes.substr(offset + 84, length) });
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

    explicit BundleFixture(SceneSnapshot scene = synthetic_scene()) : m_scene(std::move(scene)) {
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
        for (const auto& [K_NAME, K_VALUE] : original) {
            if (K_VALUE == "directory") {
                std::filesystem::create_directories(m_root / K_NAME);
            } else {
                REQUIRE(K_VALUE.starts_with("file:"));
                std::filesystem::create_directories((m_root / K_NAME).parent_path());
                write_bytes(m_root / K_NAME, std::string_view(K_VALUE).substr(5));
            }
        }
    }
};

template <class Mutation>
void reject_and_repair(ErrorCode code, Mutation mutate, SceneSnapshot scene = synthetic_scene()) {
    BundleFixture fixture(std::move(scene));
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
            constexpr std::array K_KEYS { "handedness",
                                         "world_up",
                                         "camera_axes",
                                         "pose_convention",
                                         "quaternion_order",
                                         "pixel_centers" };
            if (K_FIELD == 0) {
                value["schema_version"] = 6;
            }
            if (K_FIELD >= 1 && K_FIELD <= 6) {
                value["coordinates"][K_KEYS[static_cast<std::size_t>(K_FIELD - 1)]] = "unsupported";
            }
            constexpr std::array K_APPEARANCE {
                "representation", "lighting", "color_domain", "sh_basis" };
            if (K_FIELD >= 7 && K_FIELD <= 10) {
                value["appearance"][K_APPEARANCE[static_cast<std::size_t>(K_FIELD - 7)]] =
                    "unsupported";
            }
            if (K_FIELD == 11) {
                value["compression"] = "unknown";
            }
            if (K_FIELD == 12) {
                value["epoch"] = -1;
            }
            if (K_FIELD == 13) {
                value["revision"] = 1.5;
            }
            if (K_FIELD == 14) {
                value["scene_id"] = 0;
            }
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
                "", ".", "..", "../outside.bin", "/absolute.bin", "nested/index.bin" };
            if (K_FIELD < 6) {
                value["index"] = K_NAMES[static_cast<std::size_t>(K_FIELD)];
            }
            if (K_FIELD == 6) {
                value["index"] = std::string(129, 'a');
            }
            if (K_FIELD == 7) {
                value["provenance"] = "bad\\path.json";
            }
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
                          if (K_FIELD == 0) {
                              value["schema_version"] = 2;
                          }
                          if (K_FIELD == 1) {
                              value.erase("schema_version");
                          }
                          if (K_FIELD == 2) {
                              value["metadata"] = nlohmann::json::array();
                          }
                          if (K_FIELD == 3) {
                              value["metadata"]["fixture"] = 42;
                          }
                          if (K_FIELD == 4) {
                              value["metadata"][""] = "empty key";
                          }
                          if (K_FIELD == 5) {
                              value["metadata"][std::string(129, 'x')] = "long key";
                          }
                          if (K_FIELD == 6) {
                              value["metadata"]["large"] = std::string(4097, 'x');
                          }
                          if (K_FIELD == 7) {
                              for (unsigned index { 0 }; index < 33; ++index) {
                                  value["metadata"][std::to_string(index)] = "value";
                              }
                          }
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
            if (K_FIELD == 0) {
                bytes[K_ENTRIES[0].m_offset + 16] = 'g';
            }
            if (K_FIELD == 1) {
                limits.m_scene.m_max_payload_bytes = 1;
            }
            if (K_FIELD == 2) {
                bytes[K_ENTRIES[0].m_offset + 16] =
                    bytes[K_ENTRIES[0].m_offset + 16] == '0' ? '1' : '0';
            }
            if (K_FIELD == 3) {
                put_integer(std::span(bytes).subspan(K_ENTRIES[0].m_offset + 8, 8),
                            read_bytes(fixture.m_directory / K_ENTRIES[0].m_name).size() + 1);
            }
            if (K_FIELD == 4) {
                limits.m_scene.m_max_splats = 1;
            }
            if (K_FIELD == 5) {
                limits.m_scene.m_max_payload_bytes =
                    read_bytes(fixture.m_directory / K_ENTRIES[0].m_name).size() +
                    read_bytes(fixture.m_directory / K_ENTRIES[1].m_name).size() - 1;
            }
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
            if (K_FIELD == 0) {
                put_integer(std::span(bytes).subspan(40, 8), std::bit_cast<std::uint64_t>(0.0));
            }
            if (K_FIELD == 1) {
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET, 4),
                            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
            }
            if (K_FIELD == 2) {
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET + 12, 4), 0);
            }
            if (K_FIELD == 3) {
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET + 40, 4),
                            std::bit_cast<std::uint32_t>(1.1F));
            }
            if (K_FIELD == 4) {
                put_integer(std::span(bytes).subspan(K_SPLAT_OFFSET + 44, 4),
                            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
            }
            if (K_FIELD == 5) {
                put_integer(std::span(bytes).subspan(160, 8), 1000);
            }
            if (K_FIELD == 6) {
                bytes.push_back('x');
            }
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
    for (const auto& entry : index_entries(fixture.index())) {
        limits.m_scene.m_max_payload_bytes += read_bytes(fixture.m_directory / entry.m_name).size();
    }
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
    auto chunk = std::make_shared<SceneChunk>(*replacement->m_chunks.at({ 1 }));
    chunk->m_generation = 5;
    chunk->m_splats[0].m_mean[0] = 4;
    replacement->m_chunks[{ 1 }] = chunk;
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
    if (std::filesystem::exists(K_OBSTRUCTION)) {
        std::filesystem::rename(K_OBSTRUCTION, fixture.m_root / "retained asset");
    }
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
        if (K_BEFORE.contains(K_PATH)) {
            continue;
        }
        const auto K_FILENAME = std::filesystem::path(K_PATH).filename();
        CHECK(std::filesystem::path(K_PATH).parent_path() == fixture.m_directory.filename());
        CHECK(K_BYTES == "file:" + read_bytes(K_REFERENCE / K_FILENAME));
        CHECK(K_FILENAME.string().find(".tmp.") == std::string::npos);
    }
    CHECK(scene_content_hash(**K_RETAINED) == K_HASH);
    std::filesystem::remove(K_OBSTRUCTION);
    if (std::filesystem::exists(fixture.m_root / "retained asset")) {
        std::filesystem::rename(fixture.m_root / "retained asset", K_OBSTRUCTION);
    }
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
    if (K_FIELD == 0) {
        rejected.reset();
    }
    if (K_FIELD == 1) {
        state->m_id = {};
    }
    if (K_FIELD == 2) {
        state->m_provenance[""] = "invalid key";
    }
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

namespace {

SceneSnapshot mesh_bundle_scene(int source_kind, bool hybrid = false, bool invalidated = false) {
    auto scene = synthetic_scene();
    if (source_kind) {
        scene->m_provenance["source_kind"] = "partial_observed_geometry";
    }
    for (auto& [K_ID, value] : scene->m_chunks) {
        static_cast<void>(K_ID);
        auto chunk = std::make_shared<SceneChunk>(*value);
        if (!hybrid) {
            chunk->m_splats.clear();
        }
        chunk->m_geometry_valid = !invalidated;
        chunk->m_world_from_chunk.m_translation = { 1, 2, -3 };
        chunk->m_world_from_chunk.m_scale = 2;
        SceneMesh mesh;
        mesh.m_vertices = {
            { { -1, -1, 0 }, { 0, 0, 1 } }, { { 1, -1, 0 }, { 0, 0, 1 } }, { { 0, 1, 0 }, { 0, 0, 0 } } };
        mesh.m_triangles = { { 0, 1, 2 } };
        if (source_kind) {
            auto& source = mesh.m_observed.emplace();
            source.m_context.m_epoch = scene->m_epoch;
            source.m_context.m_pose_revision = { 5 };
            source.m_manifest_json = "{\"synthetic_coverage_fixture\":1}";
            source.m_receipt_json = "{\"synthetic_pose_receipt_fixture\":2}";
            source.m_context.m_manifest_sha256 = digest(source.m_manifest_json);
            source.m_context.m_receipt_sha256 = digest(source.m_receipt_json);
            source.m_context.m_recipe_sha256 = std::string(64uz, 'a');
            source.m_context.m_pose_sha256 = std::string(64uz, 'b');
            source.m_asset_sha256 = std::string(64uz, 'c');
            if (source_kind == 2) {
                // Bundle admission retains authoring provenance; it does not replay
                // a source proof from these deliberately synthetic opaque documents.
                source.m_cleanup_json = "{\"synthetic_cleanup_fixture\":true}";
                source.m_cleanup_sha256 = digest(source.m_cleanup_json);
            }
        }
        chunk->m_mesh = std::move(mesh);
        if (invalidated) {
            chunk->m_pose_revision = { 6 };
        }
        value = std::move(chunk);
    }
    return scene;
}

std::uint64_t get_integer(std::string_view bytes, std::size_t offset, std::size_t length = 8uz) {
    REQUIRE(offset <= bytes.size());
    REQUIRE(length <= bytes.size() - offset);
    std::uint64_t value {};
    for (std::size_t index { 0uz }; index < length; ++index) {
        value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + index])) << (index * 8uz);
    }
    return value;
}

std::size_t mesh_text_offset(std::string_view bytes, std::size_t number) {
    const auto K_START = bytes.find("WEXMSH01");
    REQUIRE(K_START != bytes.npos);
    auto offset = K_START + 48uz;
    for (std::size_t index { 0uz }; index < number; ++index) {
        offset += 4uz + static_cast<std::size_t>(get_integer(bytes, offset, 4uz));
    }
    return offset;
}

}  // namespace

TEST_CASE("mesh bundles preserve exact raw derived synthetic and hybrid snapshots",
          "[bundle_admission][mesh_bundle]") {
    const auto K_SOURCE = GENERATE(0, 1, 2);
    const auto K_HYBRID = GENERATE(false, true);
    const auto K_INVALIDATED = GENERATE(false, true);
    BundleFixture fixture(mesh_bundle_scene(K_SOURCE, K_HYBRID, K_INVALIDATED));
    CHECK(fixture.manifest().at("schema_version") == 2);
    const auto K_LOADED = load_bundle(fixture.m_directory);
    REQUIRE(K_LOADED);
    CHECK(scene_content_hash(**K_LOADED) == scene_content_hash(*fixture.m_scene));
    const auto& chunk = *(*K_LOADED)->m_chunks.at({ 1 });
    if (!chunk.m_mesh) {
        throw std::runtime_error("Round trip lost mesh payload");
    }
    const auto& mesh = *chunk.m_mesh;
    CHECK(mesh.m_vertices.size() == 3uz);
    CHECK(mesh.m_triangles.size() == 1uz);
    CHECK(chunk.m_splats.size() == (K_HYBRID ? 1uz : 0uz));
    CHECK(chunk.m_geometry_valid == !K_INVALIDATED);
    CHECK(mesh.m_observed.has_value() == (K_SOURCE != 0));
    const auto K_BEFORE = tree(fixture.m_root);
    REQUIRE(write_bundle(fixture.m_directory, *K_LOADED));
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("mesh bundles keep legacy chunks in mixed snapshots and reject schema downgrades",
          "[bundle_admission][mesh_bundle]") {
    auto scene = std::make_shared<SceneState>(*mesh_bundle_scene(0));
    scene->m_chunks.at({ 2 }) = synthetic_scene()->m_chunks.at({ 2 });
    BundleFixture fixture(scene);
    const auto K_ENTRIES = index_entries(fixture.index());
    CHECK(read_bytes(fixture.m_directory / K_ENTRIES[0].m_name).starts_with("WEXCHK02"));
    CHECK(read_bytes(fixture.m_directory / K_ENTRIES[1].m_name).starts_with("WEXCHK01"));
    const auto K_LOADED = load_bundle(fixture.m_directory);
    REQUIRE(K_LOADED);
    CHECK(scene_content_hash(**K_LOADED) == scene_content_hash(*scene));
    auto manifest = fixture.manifest();
    manifest["schema_version"] = 1;
    fixture.manifest(manifest);
    CHECK(load_bundle(fixture.m_directory).has_value() == false);
    BundleFixture legacy;
    auto promoted = legacy.manifest();
    promoted["schema_version"] = 2;
    promoted["geometry"] = manifest.at("geometry");
    legacy.manifest(promoted);
    const auto K_BEFORE = tree(legacy.m_root);
    CHECK(load_bundle(legacy.m_directory).has_value() == false);
    CHECK(tree(legacy.m_root) == K_BEFORE);
}

TEST_CASE("rehashed mesh bundles reject malformed sizes numerics indices and dependency pins",
          "[bundle_admission][mesh_bundle]") {
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    CAPTURE(K_MUTATION);
    reject_and_repair(
        ErrorCode::INVALID_DATA,
        [K_MUTATION](const BundleFixture& fixture, BundleLimits&) {
            const auto K_ENTRY = index_entries(fixture.index())[0];
            auto bytes = read_bytes(fixture.m_directory / K_ENTRY.m_name);
            const auto K_MESH = bytes.find("WEXMSH01");
            REQUIRE(K_MESH != bytes.npos);
            const auto K_LAST_TEXT = mesh_text_offset(bytes, 8uz);
            const auto K_GEOMETRY =
                K_LAST_TEXT + 4uz + static_cast<std::size_t>(get_integer(bytes, K_LAST_TEXT, 4uz));
            if (K_MUTATION == 0) {
                bytes[K_MESH] = 'X';
            }
            if (K_MUTATION == 1) {
                put_integer(std::span(bytes).subspan(K_MESH + 8uz, 8uz), UINT64_MAX);
            }
            if (K_MUTATION == 2) {
                put_integer(std::span(bytes).subspan(K_MESH + 16uz, 8uz), UINT64_MAX);
            }
            if (K_MUTATION == 3) {
                put_integer(std::span(bytes).subspan(K_MESH + 24uz, 8uz), 2);
            }
            if (K_MUTATION == 4) {
                bytes.pop_back();
            }
            if (K_MUTATION == 5) {
                bytes.push_back('\0');
            }
            if (K_MUTATION == 6) {
                put_integer(std::span(bytes).subspan(K_GEOMETRY, 4uz), 0x7fc00000);
            }
            if (K_MUTATION == 7) {
                put_integer(std::span(bytes).subspan(K_GEOMETRY + 12uz, 4uz), 0x40000000);
            }
            if (K_MUTATION == 8) {
                put_integer(std::span(bytes).subspan(bytes.size() - 12uz, 4uz), 3);
            }
            if (K_MUTATION == 9) {
                put_integer(std::span(bytes).subspan(bytes.size() - 8uz, 4uz), 0);
            }
            if (K_MUTATION == 10) {
                put_integer(std::span(bytes).subspan(mesh_text_offset(bytes, 5uz), 4uz),
                            UINT32_MAX);
            }
            if (K_MUTATION == 11) {
                bytes[mesh_text_offset(bytes, 5uz) + 4uz] = 'X';
            }
            if (K_MUTATION == 12) {
                bytes[mesh_text_offset(bytes, 6uz) + 4uz] = 'X';
            }
            if (K_MUTATION == 13) {
                bytes[mesh_text_offset(bytes, 8uz) + 4uz] = 'X';
            }
            if (K_MUTATION == 14) {
                bytes[mesh_text_offset(bytes, 4uz) + 4uz] = 'z';
            }
            if (K_MUTATION == 15) {
                put_integer(std::span(bytes).subspan(K_MESH + 40uz, 8uz), 0);
            }
            if (K_MUTATION == 16) {
                put_integer(std::span(bytes).subspan(mesh_text_offset(bytes, 7uz), 4uz), 0);
            }
            if (K_MUTATION == 17) {
                bytes.resize(K_MESH + 20uz);
            }
            fixture.chunk(0uz, bytes);
        },
        mesh_bundle_scene(2));
}

TEST_CASE("mesh bundle metadata conventions and world pose context cannot be promoted",
          "[bundle_admission][mesh_bundle]") {
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6);
    reject_and_repair(
        K_MUTATION >= 4 ? ErrorCode::WRONG_EPOCH : ErrorCode::INVALID_DATA,
        [K_MUTATION](const BundleFixture& fixture, BundleLimits&) {
            auto manifest = fixture.manifest();
            constexpr std::array K_KEYS {
                "collision_status", "representation", "encoding", "source_validation" };
            if (K_MUTATION < 4) {
                manifest["geometry"][K_KEYS[static_cast<std::size_t>(K_MUTATION)]] = "unsupported";
            }
            if (K_MUTATION == 4) {
                manifest["coordinates"]["scale_status"] = "METRIC_MEASURED";
            }
            if (K_MUTATION == 5) {
                manifest["epoch"] = 4;
            }
            if (K_MUTATION == 6) {
                auto bytes =
                    read_bytes(fixture.m_directory / index_entries(fixture.index())[0].m_name);
                const auto K_MESH = bytes.find("WEXMSH01");
                put_integer(std::span(bytes).subspan(K_MESH + 32uz, 8uz), 4);
                fixture.chunk(0uz, bytes);
                return;
            }
            fixture.manifest(manifest);
        },
        mesh_bundle_scene(2));
}

TEST_CASE("mesh bundle budgets bound aggregate allocation and exact encoded byte sizes",
          "[bundle_admission][mesh_bundle]") {
    BundleFixture fixture(mesh_bundle_scene(2, true));
    const auto K_ENTRIES = index_entries(fixture.index());
    const auto K_BYTES = read_bytes(fixture.m_directory / K_ENTRIES[0].m_name).size() +
                         read_bytes(fixture.m_directory / K_ENTRIES[1].m_name).size();
    BundleLimits limits;
    limits.m_scene.m_max_mesh_vertices = 6uz;
    limits.m_scene.m_max_mesh_triangles = 2uz;
    limits.m_scene.m_max_splats = 2uz;
    limits.m_scene.m_max_payload_bytes = K_BYTES;
    REQUIRE(load_bundle(fixture.m_directory, limits));
    const auto K_LIMIT = GENERATE(0, 1, 2, 3);
    if (K_LIMIT == 0) {
        --limits.m_scene.m_max_mesh_vertices;
    }
    if (K_LIMIT == 1) {
        --limits.m_scene.m_max_mesh_triangles;
    }
    if (K_LIMIT == 2) {
        --limits.m_scene.m_max_payload_bytes;
    }
    if (K_LIMIT == 3) {
        --limits.m_scene.m_max_splats;
    }
    const auto K_BEFORE = tree(fixture.m_root);
    CHECK(load_bundle(fixture.m_directory, limits).has_value() == false);
    CHECK(tree(fixture.m_root) == K_BEFORE);
}

TEST_CASE("failed mesh publication preserves the old manifest and retained reader",
          "[bundle_admission][mesh_bundle]") {
    BundleFixture fixture;
    const auto K_OLD = load_bundle(fixture.m_directory);
    REQUIRE(K_OLD);
    const auto K_MANIFEST = read_bytes(fixture.m_directory / "manifest.json");
    const auto K_NEW = mesh_bundle_scene(2);
    const auto K_STAGED = fixture.m_root / "staged.wexplore";
    REQUIRE(write_bundle(K_STAGED, K_NEW));
    const auto K_STAGED_MANIFEST = nlohmann::json::parse(read_bytes(K_STAGED / "manifest.json"));
    const auto K_INDEX_NAME = K_STAGED_MANIFEST.at("index").get<std::string>();
    REQUIRE(std::filesystem::create_directory(fixture.m_directory / K_INDEX_NAME));
    CHECK(write_bundle(fixture.m_directory, K_NEW).has_value() == false);
    CHECK(read_bytes(fixture.m_directory / "manifest.json") == K_MANIFEST);
    const auto K_REOPENED = load_bundle(fixture.m_directory);
    REQUIRE(K_REOPENED);
    CHECK(scene_content_hash(**K_REOPENED) == scene_content_hash(**K_OLD));
    REQUIRE(std::filesystem::remove(fixture.m_directory / K_INDEX_NAME));
    REQUIRE(write_bundle(fixture.m_directory, K_NEW));
    const auto K_REPLACED = load_bundle(fixture.m_directory);
    REQUIRE(K_REPLACED);
    CHECK(scene_content_hash(**K_REPLACED) == scene_content_hash(*K_NEW));
    CHECK(scene_content_hash(**K_OLD) == scene_content_hash(*fixture.m_scene));
}

TEST_CASE("bundle JSON rejects duplicate keys deep nesting and raw NUL without mutation",
          "[bundle_admission][mesh_bundle]") {
    const auto K_PROVENANCE = GENERATE(false, true);
    const auto K_MUTATION = GENERATE(0, 1, 2);
    reject_and_repair(
        ErrorCode::INVALID_DATA,
        [K_PROVENANCE, K_MUTATION](const BundleFixture& fixture, BundleLimits&) {
            auto manifest = fixture.manifest();
            const auto K_PATH =
                fixture.m_directory /
                (K_PROVENANCE ? manifest.at("provenance").get<std::string>() : "manifest.json");
            auto bytes = read_bytes(K_PATH);
            if (K_MUTATION == 0) {
                bytes.insert(1uz, "\"schema_version\":2,");
            }
            if (K_MUTATION == 1) {
                bytes += std::string(1uz, '\0');
            }
            if (K_MUTATION == 2) {
                bytes.insert(
                    1uz, "\"deep\":" + std::string(40uz, '[') + "0" + std::string(40uz, ']') + ",");
            }
            write_bytes(K_PATH, bytes);
            if (K_PROVENANCE) {
                manifest["provenance_sha256"] = digest(bytes);
                fixture.manifest(manifest);
            }
        },
        mesh_bundle_scene(2));
}
