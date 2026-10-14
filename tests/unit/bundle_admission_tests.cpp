#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

using namespace world_explorer;
namespace {
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    std::string bytes{std::istreambuf_iterator<char>(file), {}};
    REQUIRE(!file.bad());
    return bytes;
}
void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    REQUIRE(file);
}
std::string digest(std::string_view bytes) { return sha256(std::as_bytes(std::span(bytes))); }
using file_tree = std::map<std::string, std::string>;
file_tree tree(const std::filesystem::path& root) {
    file_tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).generic_string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status))
            result.emplace(name,
                           "link:" + std::filesystem::read_symlink(entry.path()).generic_string());
        else if (std::filesystem::is_directory(status))
            result.emplace(name, "directory");
        else {
            REQUIRE(std::filesystem::is_regular_file(status));
            REQUIRE(entry.file_size() <= std::size_t{2} * 1024 * 1024);
            result.emplace(name, "file:" + read_bytes(entry.path()));
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
struct index_entry {
    std::size_t offset;
    std::string name;
};
std::vector<index_entry> index_entries(const std::string& bytes) {
    std::vector<index_entry> entries;
    for (std::size_t offset = 16; offset < bytes.size();) {
        REQUIRE(offset + 84 <= bytes.size());
        std::uint32_t length{};
        for (std::size_t index = 0; index < 4; ++index)
            length |=
                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 80 + index]))
                << (index * 8);
        REQUIRE(length <= bytes.size() - offset - 84);
        entries.push_back({offset, bytes.substr(offset + 84, length)});
        offset += 84 + length;
    }
    return entries;
}
std::shared_ptr<scene_state> synthetic_scene() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {42};
    scene->epoch = {3};
    scene->revision = {7};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"}, {"fixture", "bundle-admission"}};
    for (std::uint64_t id = 1; id <= 2; ++id) {
        auto chunk = std::make_shared<scene_chunk>();
        chunk->id = {id};
        chunk->generation = 4;
        chunk->pose_revision = {5};
        chunk->bounds = {{-10, -10, -10}, {10, 10, 10}};
        chunk->source_frames = {{11}, {12}};
        chunk->splats.emplace_back();
        chunk->splats[0].mean = {static_cast<float>(id), 2, 3};
        chunk->splats[0].sh_degree = 3;
        chunk->splats[0].sh[47] = .75F;
        scene->chunks.emplace(chunk->id, chunk);
    }
    return scene;
}
struct bundle_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_bundle_admission_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path directory = root / "synthetic.wexplore";
    scene_snapshot scene = synthetic_scene();
    bundle_fixture() {
        REQUIRE(write_bundle(directory, scene));
        write_bytes(root / "caller.txt", "unrelated caller data");
    }
    ~bundle_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    nlohmann::json manifest() const {
        return nlohmann::json::parse(read_bytes(directory / "manifest.json"));
    }
    void manifest(const nlohmann::json& value) const {
        write_bytes(directory / "manifest.json", value.dump());
    }
    std::string index() const {
        return read_bytes(directory / manifest().at("index").get<std::string>());
    }
    void index(std::string_view bytes) const {
        auto value = manifest();
        const auto hash = digest(bytes), name = "index_" + hash + ".bin";
        write_bytes(directory / name, bytes);
        value["index"] = name;
        value["index_sha256"] = hash;
        value["index_bytes"] = bytes.size();
        manifest(value);
    }
    void chunk(std::size_t number, std::string_view bytes) const {
        auto encoded = index();
        const auto entries = index_entries(encoded);
        REQUIRE(number < entries.size());
        const auto hash = digest(bytes), name = "chunk_" + hash + ".bin";
        REQUIRE(name.size() == entries[number].name.size());
        write_bytes(directory / name, bytes);
        put_integer(std::span(encoded).subspan(entries[number].offset + 8, 8), bytes.size());
        encoded.replace(entries[number].offset + 16, 64, hash);
        encoded.replace(entries[number].offset + 84, name.size(), name);
        index(encoded);
    }
    void provenance(const nlohmann::json& value) const {
        const auto bytes = value.dump(), hash = digest(bytes),
                   name = "provenance_" + hash + ".json";
        write_bytes(directory / name, bytes);
        auto metadata = manifest();
        metadata["provenance"] = name;
        metadata["provenance_sha256"] = hash;
        manifest(metadata);
    }
    void repair(const file_tree& original) const {
        // Explicit repair of this fresh, solely test-owned tree after checking reader preservation.
        std::filesystem::remove_all(root);
        std::filesystem::create_directory(root);
        for (const auto& [name, value] : original)
            if (value == "directory")
                std::filesystem::create_directories(root / name);
            else {
                REQUIRE(value.starts_with("file:"));
                std::filesystem::create_directories((root / name).parent_path());
                write_bytes(root / name, std::string_view(value).substr(5));
            }
    }
};
template <class mutation> void reject_and_repair(error_code code, mutation mutate) {
    bundle_fixture fixture;
    const auto retained = load_bundle(fixture.directory);
    REQUIRE(retained);
    const auto hash = scene_content_hash(**retained);
    const auto original = tree(fixture.root);
    bundle_limits limits;
    mutate(fixture, limits);
    const auto before = tree(fixture.root);
    const auto rejected = load_bundle(fixture.directory, limits);
    INFO((rejected ? "unexpected load" : rejected.error().message));
    REQUIRE(!rejected);
    CHECK(rejected.error().code == code);
    CHECK(tree(fixture.root) == before);
    CHECK(scene_content_hash(**retained) == hash);
    fixture.repair(original);
    const auto repaired = load_bundle(fixture.directory);
    REQUIRE(repaired);
    CHECK(scene_content_hash(**repaired) == hash);
    CHECK(scene_content_hash(*fixture.scene) == hash);
    CHECK(tree(fixture.root) == original);
}
} // namespace

TEST_CASE("bundle manifests reject unsupported conventions and invalid typed identities",
          "[bundle_admission][manifest]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    CAPTURE(field);
    reject_and_repair(
        field == 0 ? error_code::UNSUPPORTED_OPERATION : error_code::INVALID_DATA,
        [field](const bundle_fixture& fixture, bundle_limits&) {
            auto value = fixture.manifest();
            constexpr std::array KEYS{"handedness",      "world_up",         "camera_axes",
                                      "pose_convention", "quaternion_order", "pixel_centers"};
            if (field == 0)
                value["schema_version"] = 2;
            if (field >= 1 && field <= 6)
                value["coordinates"][KEYS[static_cast<std::size_t>(field - 1)]] = "unsupported";
            constexpr std::array APPEARANCE{"representation", "lighting", "color_domain",
                                            "sh_basis"};
            if (field >= 7 && field <= 10)
                value["appearance"][APPEARANCE[static_cast<std::size_t>(field - 7)]] =
                    "unsupported";
            if (field == 11)
                value["compression"] = "unknown";
            if (field == 12)
                value["epoch"] = -1;
            if (field == 13)
                value["revision"] = 1.5;
            if (field == 14)
                value["scene_id"] = 0;
            fixture.manifest(value);
        });
}

TEST_CASE("bundle referenced paths reject invalid names and linked assets without writes",
          "[bundle_admission][paths]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(field);
    reject_and_repair(error_code::INVALID_DATA, [field](const bundle_fixture& fixture,
                                                        bundle_limits&) {
        auto value = fixture.manifest();
        constexpr std::array NAMES{
            "", ".", "..", "../outside.bin", "/absolute.bin", "nested/index.bin"};
        if (field < 6)
            value["index"] = NAMES[static_cast<std::size_t>(field)];
        if (field == 6)
            value["index"] = std::string(129, 'a');
        if (field == 7)
            value["provenance"] = "bad\\path.json";
        fixture.manifest(value);
        if (field >= 8) {
            const auto name = field == 8 ? "manifest.json" : value.at("index").get<std::string>();
            const auto path = fixture.directory / name;
            std::filesystem::copy_file(path, fixture.root / "caller asset");
            std::filesystem::remove(path);
            create_link(fixture.root / "caller asset", path);
        }
    });
}

TEST_CASE("bundle provenance schema typed metadata and record limits survive rejected load",
          "[bundle_admission][provenance]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(field);
    reject_and_repair(field >= 4 ? error_code::RESOURCE_EXHAUSTED : error_code::INVALID_DATA,
                      [field](const bundle_fixture& fixture, bundle_limits&) {
                          auto value = nlohmann::json::parse(
                              read_bytes(fixture.directory /
                                         fixture.manifest().at("provenance").get<std::string>()));
                          if (field == 0)
                              value["schema_version"] = 2;
                          if (field == 1)
                              value.erase("schema_version");
                          if (field == 2)
                              value["metadata"] = nlohmann::json::array();
                          if (field == 3)
                              value["metadata"]["fixture"] = 42;
                          if (field == 4)
                              value["metadata"][""] = "empty key";
                          if (field == 5)
                              value["metadata"][std::string(129, 'x')] = "long key";
                          if (field == 6)
                              value["metadata"]["large"] = std::string(4097, 'x');
                          if (field == 7)
                              for (unsigned index = 0; index < 33; ++index)
                                  value["metadata"][std::to_string(index)] = "value";
                          fixture.provenance(value);
                      });
}

TEST_CASE("bundle rehashed indexes enforce size digest identity and aggregate budgets",
          "[bundle_admission][index]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(field);
    reject_and_repair(
        error_code::INVALID_DATA, [field](const bundle_fixture& fixture, bundle_limits& limits) {
            auto bytes = fixture.index();
            const auto entries = index_entries(bytes);
            REQUIRE(entries.size() == 2);
            if (field == 0)
                bytes[entries[0].offset + 16] = 'g';
            if (field == 1)
                limits.scene.max_payload_bytes = 1;
            if (field == 2)
                bytes[entries[0].offset + 16] = bytes[entries[0].offset + 16] == '0' ? '1' : '0';
            if (field == 3)
                put_integer(std::span(bytes).subspan(entries[0].offset + 8, 8),
                            read_bytes(fixture.directory / entries[0].name).size() + 1);
            if (field == 4)
                limits.scene.max_splats = 1;
            if (field == 5)
                limits.scene.max_payload_bytes =
                    read_bytes(fixture.directory / entries[0].name).size() +
                    read_bytes(fixture.directory / entries[1].name).size() - 1;
            fixture.index(bytes);
        });
}

TEST_CASE("bundle rehashed payload validation preserves leased snapshots on corrupt numerics",
          "[bundle_admission][payload]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    reject_and_repair(
        error_code::INVALID_DATA, [field](const bundle_fixture& fixture, bundle_limits&) {
            const auto entries = index_entries(fixture.index());
            auto bytes = read_bytes(fixture.directory / entries[0].name);
            constexpr std::size_t SPLAT_OFFSET = 168 + 16;
            if (field == 0)
                put_integer(std::span(bytes).subspan(40, 8), std::bit_cast<std::uint64_t>(0.0));
            if (field == 1)
                put_integer(std::span(bytes).subspan(SPLAT_OFFSET, 4),
                            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
            if (field == 2)
                put_integer(std::span(bytes).subspan(SPLAT_OFFSET + 12, 4), 0);
            if (field == 3)
                put_integer(std::span(bytes).subspan(SPLAT_OFFSET + 40, 4),
                            std::bit_cast<std::uint32_t>(1.1F));
            if (field == 4)
                put_integer(std::span(bytes).subspan(SPLAT_OFFSET + 44, 4),
                            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
            if (field == 5)
                put_integer(std::span(bytes).subspan(160, 8), 1000);
            if (field == 6)
                bytes.push_back('x');
            fixture.chunk(0, bytes);
        });
}

TEST_CASE("bundle exact encoded and decoded budgets retain scene content and provenance",
          "[bundle_admission][budget]") {
    bundle_fixture fixture;
    auto scene = std::make_shared<scene_state>(*fixture.scene);
    scene->scale = scale_status::METRIC_MEASURED;
    scene->provenance[std::string(128, 'k')] = std::string(4096, 'v');
    REQUIRE(write_bundle(fixture.directory, scene));
    bundle_limits limits;
    limits.scene.max_chunks = 2;
    limits.scene.max_splats = 2;
    limits.scene.max_source_frames = 2;
    const auto value = fixture.manifest();
    limits.max_manifest_bytes =
        std::max(read_bytes(fixture.directory / "manifest.json").size(),
                 read_bytes(fixture.directory / value.at("provenance").get<std::string>()).size());
    limits.scene.max_payload_bytes = 0;
    for (const auto& entry : index_entries(fixture.index()))
        limits.scene.max_payload_bytes += read_bytes(fixture.directory / entry.name).size();
    const auto before = tree(fixture.root);
    const auto loaded = load_bundle(fixture.directory, limits);
    REQUIRE(loaded);
    CHECK(scene_content_hash(**loaded) == scene_content_hash(*scene));
    CHECK((*loaded)->scale == scale_status::METRIC_MEASURED);
    --limits.scene.max_payload_bytes;
    const auto rejected = load_bundle(fixture.directory, limits);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(tree(fixture.root) == before);
}

TEST_CASE("bundle failed publication retains old manifest and permits corrected publication",
          "[bundle_admission][publication]") {
    const auto field = GENERATE(0, 1, 2, 3);
    CAPTURE(field);
    bundle_fixture fixture;
    const auto retained = load_bundle(fixture.directory);
    REQUIRE(retained);
    const auto hash = scene_content_hash(**retained);
    auto replacement = std::make_shared<scene_state>(*fixture.scene);
    replacement->revision = {8};
    auto chunk = std::make_shared<scene_chunk>(*replacement->chunks.at({1}));
    chunk->generation = 5;
    chunk->splats[0].mean[0] = 4;
    replacement->chunks[{1}] = chunk;
    const auto reference = fixture.root / "replacement reference";
    REQUIRE(write_bundle(reference, replacement));
    const auto expected = nlohmann::json::parse(read_bytes(reference / "manifest.json"));
    const auto entries =
        index_entries(read_bytes(reference / expected.at("index").get<std::string>()));
    const auto name = field == 0   ? entries[0].name
                      : field == 1 ? expected.at("index").get<std::string>()
                      : field == 2 ? expected.at("provenance").get<std::string>()
                                   : "manifest.json";
    const auto obstruction = fixture.directory / name;
    const auto saved = tree(fixture.root);
    if (std::filesystem::exists(obstruction))
        std::filesystem::rename(obstruction, fixture.root / "retained asset");
    std::filesystem::create_directory(obstruction);
    const auto before = tree(fixture.root);
    const auto failed = write_bundle(fixture.directory, replacement);
    REQUIRE(!failed);
    CHECK(failed.error().code == error_code::IO_ERROR);
    const auto after = tree(fixture.root);
    for (const auto& [path, bytes] : before) {
        REQUIRE(after.contains(path));
        CHECK(after.at(path) == bytes);
    }
    for (const auto& [path, bytes] : after) {
        if (before.contains(path))
            continue;
        const auto filename = std::filesystem::path(path).filename();
        CHECK(std::filesystem::path(path).parent_path() == fixture.directory.filename());
        CHECK(bytes == "file:" + read_bytes(reference / filename));
        CHECK(filename.string().find(".tmp.") == std::string::npos);
    }
    CHECK(scene_content_hash(**retained) == hash);
    std::filesystem::remove(obstruction);
    if (std::filesystem::exists(fixture.root / "retained asset"))
        std::filesystem::rename(fixture.root / "retained asset", obstruction);
    const auto old = load_bundle(fixture.directory);
    REQUIRE(old);
    CHECK(scene_content_hash(**old) == hash);
    REQUIRE(write_bundle(fixture.directory, replacement));
    const auto published = load_bundle(fixture.directory);
    REQUIRE(published);
    CHECK(scene_content_hash(**published) == scene_content_hash(*replacement));
    CHECK(scene_content_hash(**retained) == hash);
    const auto committed = tree(fixture.root);
    REQUIRE(write_bundle(fixture.directory, replacement));
    CHECK(tree(fixture.root) == committed);
    CHECK(read_bytes(fixture.root / "caller.txt") ==
          std::string_view(saved.at("caller.txt")).substr(5));
}

TEST_CASE("bundle rejected native snapshots do not alter caller files or consume publication",
          "[bundle_admission][publication]") {
    const auto field = GENERATE(0, 1, 2);
    bundle_fixture fixture;
    auto state = std::make_shared<scene_state>(*fixture.scene);
    scene_snapshot rejected = state;
    if (field == 0)
        rejected.reset();
    if (field == 1)
        state->id = {};
    if (field == 2)
        state->provenance[""] = "invalid key";
    const auto before = tree(fixture.root);
    const auto failed = write_bundle(fixture.directory, rejected);
    REQUIRE(!failed);
    CHECK(failed.error().code == (field == 0   ? error_code::INVALID_ARGUMENT
                                  : field == 1 ? error_code::INVALID_DATA
                                               : error_code::RESOURCE_EXHAUSTED));
    CHECK(tree(fixture.root) == before);
    REQUIRE(write_bundle(fixture.directory, fixture.scene));
    CHECK(tree(fixture.root) == before);
}
