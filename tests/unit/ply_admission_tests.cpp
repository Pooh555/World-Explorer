#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace world_explorer;
namespace {
struct ply_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_ply_admission_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = root / "synthetic.ply";
    std::vector<std::string> properties{"x",      "y",       "z",       "f_dc_0",  "f_dc_1",
                                        "f_dc_2", "opacity", "scale_0", "scale_1", "scale_2",
                                        "rot_0",  "rot_1",   "rot_2",   "rot_3"};
    std::string header() const {
        std::string value = "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment "
                            "color_domain srgb\nelement vertex 1\n";
        for (const auto& property : properties)
            value += "property float " + property + "\n";
        return value + "end_header\n";
    }
    static std::string row() { return "1 2 3 0.25 0.5 0.75 0 0 0 0 1 0 0 0\n"; }
    ply_fixture() { std::filesystem::create_directory(root); }
    ~ply_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    void write(std::string_view bytes) const {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        REQUIRE(file);
    }
};
} // namespace

TEST_CASE("PLY header byte budget includes magic line and every raw CRLF byte",
          "[bundle_admission][ply][budget]") {
    const auto crlf = GENERATE(false, true);
    ply_fixture fixture;
    auto header = fixture.header();
    if (crlf) {
        std::string raw;
        for (char value : header) {
            if (value == '\n')
                raw += '\r';
            raw += value;
        }
        header = std::move(raw);
    }
    fixture.write(header + ply_fixture::row());
    const auto original = sha256_file(fixture.path, 4096);
    REQUIRE(original);
    bundle_limits limits;
    limits.max_ply_header_bytes = header.size();
    const auto exact = load_gsplat_ply(fixture.path, {42}, limits);
    REQUIRE(exact);
    const auto retained_hash = scene_content_hash(**exact);
    --limits.max_ply_header_bytes;
    const auto rejected = load_gsplat_ply(fixture.path, {42}, limits);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(scene_content_hash(**exact) == retained_hash);
    const auto after = sha256_file(fixture.path, 4096);
    REQUIRE(after);
    CHECK(*after == *original);
}

TEST_CASE("gsplat PLY degree zero through three preserve channel major SH and decoded values",
          "[bundle_admission][ply][roundtrip]") {
    const auto degree = GENERATE(0, 1, 2, 3);
    const auto reverse = GENERATE(false, true);
    CAPTURE(degree, reverse);
    ply_fixture fixture;
    const auto rest = static_cast<std::size_t>((degree + 1) * (degree + 1) - 1) * 3;
    for (std::size_t index = 0; index < rest; ++index)
        fixture.properties.push_back("f_rest_" + std::to_string(index));
    for (const auto* normal : {"nx", "ny", "nz"})
        fixture.properties.emplace_back(normal);
    if (reverse)
        std::ranges::reverse(fixture.properties);
    std::ostringstream row;
    row.imbue(std::locale::classic());
    row << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& name : fixture.properties) {
        double value{};
        if (name == "x")
            value = 1;
        if (name == "y")
            value = 2;
        if (name == "z")
            value = 3;
        if (name == "rot_0")
            value = 2;
        if (name == "f_dc_0")
            value = .25;
        if (name == "f_dc_1")
            value = .5;
        if (name == "f_dc_2")
            value = .75;
        if (name == "opacity")
            value = reverse ? -2 : 2;
        if (name.starts_with("scale_"))
            value = std::log(.5);
        if (name.starts_with("f_rest_"))
            value = static_cast<double>(std::stoul(name.substr(7)) + 1) / 16;
        row << value << ' ';
    }
    fixture.write(fixture.header() + row.str() + "\n \t\r\n");
    const auto before = sha256_file(fixture.path, 16384);
    REQUIRE(before);
    const auto loaded = load_gsplat_ply(fixture.path, {42});
    REQUIRE(loaded);
    REQUIRE((*loaded)->chunks.size() == 1);
    const auto& chunk = *(*loaded)->chunks.at({1});
    REQUIRE(chunk.splats.size() == 1);
    const auto& splat = chunk.splats[0];
    CHECK(splat.sh_degree == degree);
    CHECK(splat.mean == std::array{1.F, 2.F, 3.F});
    CHECK(splat.scale == std::array{.5F, .5F, .5F});
    CHECK(splat.rotation == std::array{1.F, 0.F, 0.F, 0.F});
    CHECK(std::abs(splat.opacity - static_cast<float>(1 / (1 + std::exp(reverse ? 2.0 : -2.0)))) <
          1e-7F);
    CHECK(splat.sh[0] == .25F);
    CHECK(splat.sh[1] == .5F);
    CHECK(splat.sh[2] == .75F);
    const auto coefficients_per_channel = rest / 3;
    for (std::size_t axis = 0; axis < 3; ++axis)
        for (std::size_t coefficient = 0; coefficient < coefficients_per_channel; ++coefficient)
            CHECK(splat.sh[(coefficient + 1) * 3 + axis] ==
                  static_cast<float>(axis * coefficients_per_channel + coefficient + 1) / 16);
    for (std::size_t index = rest + 3; index < 48; ++index)
        CHECK(splat.sh[index] == 0);
    CHECK(transform_point(chunk.world_from_chunk, {1, 2, 3}) == vec3d{1, -2, -3});
    CHECK_FALSE(chunk.geometry_valid);
    CHECK((*loaded)->scale == scale_status::ARBITRARY);
    const auto after = sha256_file(fixture.path, 16384);
    REQUIRE(after);
    CHECK(*after == *before);
}

TEST_CASE("gsplat PLY malformed headers and unsupported layouts preserve prior snapshots",
          "[bundle_admission][ply][header]") {
    const auto field =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    CAPTURE(field);
    ply_fixture fixture;
    const auto original = fixture.header() + ply_fixture::row();
    fixture.write(original);
    const auto retained = load_gsplat_ply(fixture.path, {42});
    REQUIRE(retained);
    const auto hash = scene_content_hash(**retained);
    auto header = fixture.header();
    bundle_limits limits;
    const auto replace = [&](std::string_view from, std::string_view to) {
        const auto position = header.find(from);
        REQUIRE(position != std::string::npos);
        header.replace(position, from.size(), to);
    };
    if (field == 0)
        header.clear();
    if (field == 1)
        replace("ply\n", "not-ply\n");
    if (field == 2)
        replace("end_header\n", "");
    if (field == 3)
        replace("format ascii 1.0\n", "");
    if (field == 4)
        replace("comment exporter gsplat-1.5.3\n", "");
    if (field == 5)
        replace("comment color_domain srgb\n", "");
    if (field == 6)
        header = "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain "
                 "srgb\nend_header\n";
    if (field == 7)
        replace("vertex 1", "vertex x");
    if (field == 8)
        replace("vertex 1", "vertex 1x");
    if (field == 9)
        replace("vertex 1", "vertex 0");
    if (field == 10)
        limits.scene.max_splats = 0;
    if (field == 11)
        limits.scene.max_payload_bytes = sizeof(gaussian) - 1;
    if (field == 12)
        replace("property float x\n", "property float x\nproperty float x\n");
    if (field == 13)
        replace("property float x\n", "");
    if (field == 14)
        replace("end_header", "property float f_rest_0\nend_header");
    if (field == 15)
        replace("end_header", "property float unknown\nend_header");
    if (field == 16)
        replace("property float x", "property double x");
    if (field == 17)
        replace("element vertex 1", "element vertex 1\nelement vertex 1");
    if (field == 18) {
        std::string properties;
        for (int index = 0; index < 65; ++index)
            properties += "property float x\n";
        replace("end_header", properties + "end_header");
    }
    if (field == 19)
        header = "ply\n" + std::string(65537, '\r') + "end_header\n";
    // EOF is intentional for missing/unfinished headers; no vertex text can mask it.
    const auto malformed = header + (field <= 2 ? "" : ply_fixture::row());
    fixture.write(malformed);
    const auto before = sha256_file(fixture.path, 131072);
    REQUIRE(before);
    const auto rejected = load_gsplat_ply(fixture.path, {42}, limits);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    const auto after = sha256_file(fixture.path, 131072);
    REQUIRE(after);
    CHECK(*after == *before);
    CHECK(scene_content_hash(**retained) == hash);
    fixture.write(original);
    const auto repaired = load_gsplat_ply(fixture.path, {42});
    REQUIRE(repaired);
    CHECK(scene_content_hash(**repaired) == hash);
}

TEST_CASE("gsplat PLY rejects invalid numeric rows sizes and trailing data without altering input",
          "[bundle_admission][ply][numeric]") {
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    ply_fixture fixture;
    std::vector<std::string> values{"1", "2", "3", ".25", ".5", ".75", "0",
                                    "0", "0", "0", "1",   "0",  "0",   "0"};
    if (field == 0)
        values.clear();
    if (field == 1)
        values[0] = "NaN";
    if (field == 2)
        values[0] = "1e300";
    if (field == 3)
        values[3] = "1e300";
    if (field == 4)
        values[7] = "1000";
    if (field == 5)
        values[7] = "-1000";
    if (field == 6)
        values[10] = "0";
    if (field == 7)
        values[10] = "1e300";
    if (field == 8)
        values.push_back("extra");
    if (field == 9)
        values.pop_back();
    std::string row;
    for (const auto& value : values)
        row += value + " ";
    if (field != 0)
        row += '\n';
    if (field == 10)
        row += "extra\n";
    if (field == 11)
        row += std::string(16385, '\r') + '\n';
    fixture.write(fixture.header() + row);
    const auto before = sha256_file(fixture.path, 32768);
    REQUIRE(before);
    const auto rejected = load_gsplat_ply(fixture.path, {42});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    const auto after = sha256_file(fixture.path, 32768);
    REQUIRE(after);
    CHECK(*after == *before);
}

TEST_CASE("gsplat PLY failed opens and invalid scene admission remain explicit",
          "[bundle_admission][ply][config]") {
    ply_fixture fixture;
    const auto missing = load_gsplat_ply(fixture.path, {42});
    REQUIRE(!missing);
    CHECK(missing.error().code == error_code::IO_ERROR);
    fixture.write(fixture.header() + ply_fixture::row());
    const auto invalid_id = load_gsplat_ply(fixture.path, {});
    REQUIRE(!invalid_id);
    CHECK(invalid_id.error().code == error_code::INVALID_DATA);
    bundle_limits limits;
    limits.scene.max_chunks = 0;
    const auto exhausted = load_gsplat_ply(fixture.path, {42}, limits);
    REQUIRE(!exhausted);
    CHECK(exhausted.error().code == error_code::RESOURCE_EXHAUSTED);
}
