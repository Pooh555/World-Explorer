#include "../../src/application/color_candidates.hpp"
#include "../../src/application/color_inputs.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <fstream>
#include <map>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

struct CacheFixture {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_color_sequence_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Json m_receipt, m_manifest;
    std::vector<Json> m_children;

    std::string save(const std::filesystem::path& path, const std::string& text) const {
        std::ofstream(path, std::ios::binary) << text;
        return sha256(std::as_bytes(std::span(text)));
    }

    explicit CacheFixture(std::size_t count = 19uz, bool sequence = true) {
        std::filesystem::create_directories(m_root);
        m_root = std::filesystem::canonical(m_root);
        const auto K_NATIVE = Json { { "input_profile", "colmap_native_dense_input_v1" } }.dump();
        const auto K_RECIPE = Json { { "input_profile", "colmap_resized_dense_input_v1" } }.dump();
        const auto K_HASH = [](const std::string& text) {
            return sha256(std::as_bytes(std::span(text)));
        };
        m_manifest = { { "kind", "DENSE_INPUTS_RESIZED_GRID" },
                      { "preparation_recipe_sha256", K_HASH(K_NATIVE) },
                      { "depth_recipe_sha256", K_HASH(K_RECIPE) },
                      { "pose_asset_sha256", K_HASH("synthetic opaque poses") },
                      { "world_epoch", 0U },
                      { "pose_revision", 1U },
                      { "frames", Json::array() } };
        for (std::size_t i { 0uz }; i < count; ++i) {
            m_manifest["frames"].push_back(
                { { "frame", 9007199254740993ULL + i },
                 { "calibration", 1U },
                 { "width", 2U },
                 { "height", 2U },
                 { "image", "input/" + std::to_string(i) + ".png" },
                 { "calibrated_image_sha256", K_HASH("synthetic rgb " + std::to_string(i)) } });
        }
        m_receipt = {
            { "schema_version", 1U },
            { "kind",
             sequence ? "POSE_ADMITTED_DEPTH_SEQUENCE_V1" : "POSE_ADMITTED_DEPTH_VISIBILITY_V1" },
            { "frames", Json::array() },
            { "batches", Json::array() },
            { "input_manifest_sha256", K_HASH(m_manifest.dump()) },
            { "depth_recipe_sha256", K_HASH(K_RECIPE) },
            { "pose_asset_sha256", K_HASH("synthetic opaque poses") },
            { "world_epoch", 0U },
            { "pose_revision", 1U },
            { "valid_pixels", count * 3uz },
            { "unknown_pixels", count } };
        const auto K_BATCHES = sequence ? (count + 17uz) / 18uz : 1uz;
        for (std::size_t batch { 0uz }; batch < K_BATCHES; ++batch) {
            const auto K_CHILD_ROOT =
                sequence ? m_root / ("batch-" + std::to_string(batch)) : m_root;
            std::filesystem::create_directories(K_CHILD_ROOT / "images");
            std::filesystem::create_directories(K_CHILD_ROOT / "assets");
            Json child = m_receipt;
            child["kind"] = "POSE_ADMITTED_DEPTH_VISIBILITY_V1";
            child["units"] = "ARBITRARY_POSE_CAMERA_UNITS";
            child["depth_convention"] = "AXIAL_CAMERA_Z";
            child["pixel_centers"] = "integer";
            child["camera_axes"] = "+x_right,+y_down,+z_forward";
            child["confidence"] = "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY";
            child["frames"] = Json::array();
            child["files"] = Json::object();
            const auto K_FILE = [&](const std::string& name, const std::string& text) {
                child["files"][name] = { { "bytes", text.size() },
                                        { "sha256", save(K_CHILD_ROOT / name, text) } };
            };
            K_FILE("poses.weposes", "synthetic opaque poses");
            K_FILE("native-recipe.json", K_NATIVE);
            K_FILE("depth-recipe.json", K_RECIPE);
            K_FILE("dense-inputs.json", m_manifest.dump());
            const auto K_FIRST = sequence ? batch * 18uz : 0uz;
            const auto K_END = sequence ? std::min(count, K_FIRST + 18uz) : count;
            for (auto i = K_FIRST; i < K_END; ++i) {
                const auto K_NAME = std::to_string(i) + ".png";
                K_FILE("images/" + K_NAME, "synthetic rgb " + std::to_string(i));
                K_FILE("assets/" + K_NAME + ".wedepth", "synthetic depth " + std::to_string(i));
                auto frame = m_manifest["frames"][i];
                frame["image"] = "images/" + K_NAME;
                frame["depth_asset"] = "assets/" + K_NAME + ".wedepth";
                frame["depth_sha256"] =
                    child["files"][frame["depth_asset"].get<std::string>()]["sha256"];
                frame["manifest_index"] = i;
                frame["valid_pixels"] = 3U;
                frame["unknown_pixels"] = 1U;
                frame["uncertainty"] = "UNAVAILABLE";
                child["frames"].push_back(frame);
                m_receipt["frames"].push_back(frame);
            }
            m_children.push_back(child);
            m_receipt["batches"].push_back({ { "cache", "batch-" + std::to_string(batch) },
                                            { "first_manifest_index", K_FIRST },
                                            { "frame_count", K_END - K_FIRST },
                                            { "sha256", "" } });
        }
        if (!sequence) {
            m_receipt = m_children[0];
        }
        publish(sequence);
    }

    ~CacheFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    void publish(bool sequence = true) {
        if (sequence) {
            for (std::size_t i { 0uz }; i < m_children.size(); ++i) {
                const auto K_PIN = save(m_root / ("batch-" + std::to_string(i)) / "manifest.json",
                                        m_children[i].dump());
                if (i < m_receipt["batches"].size()) {
                    m_receipt["batches"][i]["sha256"] = K_PIN;
                }
            }
        }
        save(m_root / "manifest.json", m_receipt.dump());
    }

    std::map<std::filesystem::path, std::string> snapshot() const {
        std::map<std::filesystem::path, std::string> result;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_root)) {
            if (entry.is_regular_file()) {
                result.emplace(entry.path(), sha256_file(entry.path(), 1024uz * 1024uz).value());
            }
        }
        return result;
    }
};

}  // namespace

TEST_CASE("sequence color admission preserves every child and ordered reference",
          "[color-sequence]") {
    const auto K_COUNT = GENERATE(19uz, 63uz, 120uz);
    const CacheFixture K_FIXTURE(K_COUNT);
    const auto K_BEFORE = K_FIXTURE.snapshot();
    const auto K_CACHES = color_inputs::admit_caches(K_FIXTURE.m_receipt, K_FIXTURE.m_root, true);
    REQUIRE(K_CACHES);
    std::size_t seen {};
    for (const auto& cache : *K_CACHES) {
        for (const auto& frame : cache.m_receipt["frames"]) {
            CHECK(frame == K_FIXTURE.m_receipt["frames"][seen++]);
        }
    }
    CHECK(seen == K_COUNT);
    CHECK(color_inputs::admit_caches(K_FIXTURE.m_receipt, K_FIXTURE.m_root, true));
    CHECK(K_FIXTURE.snapshot() == K_BEFORE);
}

TEST_CASE("sequence color admission rejects rehashed incomplete stale and oversized graphs",
          "[color-sequence]") {
    CacheFixture fixture;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    switch (K_MUTATION) {
    case 0:
        fixture.m_receipt["batches"].erase(1uz);
        break;
    case 1:
        fixture.m_receipt["batches"][1]["first_manifest_index"] = 17U;
        break;
    case 2:
        fixture.m_receipt["batches"][1]["cache"] = "../caller";
        break;
    case 3:
        fixture.m_children[1]["pose_revision"] = 2U;
        break;
    case 4:
        fixture.m_children[1]["frames"][0]["calibration"] = 2U;
        break;
    case 5:
        fixture.m_children[1]["files"]["poses.weposes"]["bytes"] = 0U;
        break;
    case 6:
        fixture.m_children[1]["files"]["caller"] = { { "bytes", 0U },
                                                    { "sha256", std::string(64uz, 'a') } };
        break;
    case 7:
        fixture.m_receipt["unknown_pixels"] = 0U;
        break;
    case 8:
        fixture.m_receipt["batches"][1]["frame_count"] = 1.0;
        break;
    case 9:
        fixture.m_children[1]["schema_version"] = true;
        break;
    case 10:
        fixture.m_children[1]["frames"][0]["width"] = 4097U;
        fixture.m_receipt["frames"][18]["width"] = 4097U;
        break;
    case 11:
        fixture.m_children[1]["world_epoch"] = false;
        break;
    default:
        FAIL("Unknown color sequence mutation");
    }
    fixture.publish();
    const auto K_BEFORE = fixture.snapshot();
    CHECK(color_inputs::admit_caches(fixture.m_receipt, fixture.m_root, true).has_value() == false);
    CHECK(fixture.snapshot() == K_BEFORE);
}

TEST_CASE("color cache admission retains legacy limits cancellation and late file validation",
          "[color-sequence]") {
    SECTION("legacy 18 passes and 19 rejects") {
        const CacheFixture K_VALID(18uz, false), K_EXCESS(19uz, false);
        CHECK(color_inputs::admit_caches(K_VALID.m_receipt, K_VALID.m_root, false));
        CHECK(color_inputs::admit_caches(K_EXCESS.m_receipt, K_EXCESS.m_root, false).has_value() ==
              false);
        CHECK(color_inputs::admit_caches(K_VALID.m_receipt, K_VALID.m_root, true).has_value() ==
              false);
    }
    SECTION("sequence budget") {
        const CacheFixture K_EXCESS(121uz);
        CHECK(color_inputs::admit_caches(K_EXCESS.m_receipt, K_EXCESS.m_root, true).has_value() ==
              false);
    }
    SECTION("late depth corruption") {
        CacheFixture fixture;
        fixture.save(fixture.m_root / "batch-1/assets/18.png.wedepth", "corrupted late depth");
        const auto K_BEFORE = fixture.snapshot();
        CHECK(color_inputs::admit_caches(fixture.m_receipt, fixture.m_root, true).has_value() ==
              false);
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto K_CANCELLED = color_inputs::admit_caches(
            fixture.m_receipt, fixture.m_root, true, cancellation.get_token());
        REQUIRE(K_CANCELLED.has_value() == false);
        CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
        CHECK(fixture.snapshot() == K_BEFORE);
    }
}

TEST_CASE("candidate evidence spans all 120 views without aliasing or wire padding",
          "[color-sequence]") {
    color_inputs::CandidateViews candidates;
    for (auto index : { 0uz, 31uz, 32uz, 63uz, 64uz, 95uz, 96uz, 119uz }) {
        candidates.add(index);
        candidates.add(index);
    }
    CHECK(candidates.count() == 8uz);
    CHECK(candidates.words() ==
          std::array<std::uint32_t, 4> { 0x80000001U, 0x80000001U, 0x80000001U, 0x00800001U });
    for (std::size_t index { 0uz }; index < 120uz; ++index) {
        candidates.add(index);
    }
    CHECK(candidates.count() == 120uz);
    CHECK(candidates.words()[3] == 0x00ffffffU);
    CHECK_THROWS_AS(candidates.add(120uz), std::out_of_range);
    CHECK_THROWS_AS(candidates.add(SIZE_MAX), std::out_of_range);
}

TEST_CASE("color admission enforces each child's original pixel budget before decoding",
          "[color-sequence]") {
    CacheFixture fixture;
    for (std::size_t i { 0uz }; i < 3uz; ++i) {
        auto& frame = fixture.m_children[0]["frames"][i];
        frame["width"] = frame["height"] = 4096U;
        frame["valid_pixels"] = 0U;
        frame["unknown_pixels"] = 4096U * 4096U;
        fixture.m_receipt["frames"][i] = frame;
    }
    fixture.publish();
    const auto K_REJECTED = color_inputs::admit_caches(fixture.m_receipt, fixture.m_root, true);
    REQUIRE(K_REJECTED.has_value() == false);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(K_REJECTED.error().m_message == "Color child pixel budget exceeded");
}
