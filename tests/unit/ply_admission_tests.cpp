#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

namespace {

struct PlyFixture {
    std::filesystem::path m_root =
        std::filesystem::temp_directory_path() /
        ("we_ply_admission_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path m_path = m_root / "synthetic.ply";
    std::vector<std::string> m_properties {"x",
                                           "y",
                                           "z",
                                           "f_dc_0",
                                           "f_dc_1",
                                           "f_dc_2",
                                           "opacity",
                                           "scale_0",
                                           "scale_1",
                                           "scale_2",
                                           "rot_0",
                                           "rot_1",
                                           "rot_2",
                                           "rot_3"};

    std::string header() const {
        std::string value = "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment "
                            "color_domain srgb\nelement vertex 1\n";
        for (const auto& property : m_properties) value += "property float " + property + "\n";
        return value + "end_header\n";
    }

    static std::string row() { return "1 2 3 0.25 0.5 0.75 0 0 0 0 1 0 0 0\n"; }

    PlyFixture() { std::filesystem::create_directory(m_root); }

    ~PlyFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    void write(std::string_view bytes) const {
        std::ofstream file(m_path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        REQUIRE(file);
    }
};

}  // namespace

TEST_CASE("PLY header byte budget includes magic line and every raw CRLF byte",
          "[bundle_admission][ply][budget]") {
    const auto K_CRLF = GENERATE(false, true);
    PlyFixture fixture;
    auto header = fixture.header();
    if (K_CRLF) {
        std::string raw;
        for (char value : header) {
            if (value == '\n') raw += '\r';
            raw += value;
        }
        header = std::move(raw);
    }
    fixture.write(header + PlyFixture::row());
    const auto K_ORIGINAL = sha256_file(fixture.m_path, 4096);
    REQUIRE(K_ORIGINAL);
    BundleLimits limits;
    limits.m_max_ply_header_bytes = header.size();
    const auto K_EXACT = load_gsplat_ply(fixture.m_path, {42}, limits);
    REQUIRE(K_EXACT);
    const auto K_RETAINED_HASH = scene_content_hash(**K_EXACT);
    --limits.m_max_ply_header_bytes;
    const auto K_REJECTED = load_gsplat_ply(fixture.m_path, {42}, limits);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(scene_content_hash(**K_EXACT) == K_RETAINED_HASH);
    const auto K_AFTER = sha256_file(fixture.m_path, 4096);
    REQUIRE(K_AFTER);
    CHECK(*K_AFTER == *K_ORIGINAL);
}

TEST_CASE("gsplat PLY degree zero through three preserve channel major SH and decoded values",
          "[bundle_admission][ply][roundtrip]") {
    const auto K_DEGREE = GENERATE(0, 1, 2, 3);
    const auto K_REVERSE = GENERATE(false, true);
    CAPTURE(K_DEGREE, K_REVERSE);
    PlyFixture fixture;
    const auto K_REST = static_cast<std::size_t>((K_DEGREE + 1) * (K_DEGREE + 1) - 1) * 3;
    for (std::size_t index { 0uz }; index < K_REST; ++index)
        fixture.m_properties.push_back("f_rest_" + std::to_string(index));
    for (const auto* normal : {"nx", "ny", "nz"}) fixture.m_properties.emplace_back(normal);
    if (K_REVERSE) std::ranges::reverse(fixture.m_properties);
    std::ostringstream row;
    row.imbue(std::locale::classic());
    row << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& name : fixture.m_properties) {
        double value {};
        if (name == "x") value = 1;
        if (name == "y") value = 2;
        if (name == "z") value = 3;
        if (name == "rot_0") value = 2;
        if (name == "f_dc_0") value = .25;
        if (name == "f_dc_1") value = .5;
        if (name == "f_dc_2") value = .75;
        if (name == "opacity") value = K_REVERSE ? -2 : 2;
        if (name.starts_with("scale_")) value = std::log(.5);
        if (name.starts_with("f_rest_"))
            value = static_cast<double>(std::stoul(name.substr(7)) + 1) / 16;
        row << value << ' ';
    }
    fixture.write(fixture.header() + row.str() + "\n \t\r\n");
    const auto K_BEFORE = sha256_file(fixture.m_path, 16384);
    REQUIRE(K_BEFORE);
    const auto K_LOADED = load_gsplat_ply(fixture.m_path, {42});
    REQUIRE(K_LOADED);
    REQUIRE((*K_LOADED)->m_chunks.size() == 1);
    const auto& chunk = *(*K_LOADED)->m_chunks.at({1});
    REQUIRE(chunk.m_splats.size() == 1);
    const auto& splat = chunk.m_splats[0];
    CHECK(splat.m_sh_degree == K_DEGREE);
    CHECK(splat.m_mean == std::array { 1.F, 2.F, 3.F });
    CHECK(splat.m_scale == std::array { .5F, .5F, .5F });
    CHECK(splat.m_rotation == std::array { 1.F, 0.F, 0.F, 0.F });
    CHECK(std::abs(splat.m_opacity -
                   static_cast<float>(1 / (1 + std::exp(K_REVERSE ? 2.0 : -2.0)))) < 1e-7F);
    CHECK(splat.m_sh[0] == .25F);
    CHECK(splat.m_sh[1] == .5F);
    CHECK(splat.m_sh[2] == .75F);
    const auto K_COEFFICIENTS_PER_CHANNEL = K_REST / 3;
    for (std::size_t axis { 0uz }; axis < 3uz; ++axis)
        for (std::size_t coefficient { 0uz }; coefficient < K_COEFFICIENTS_PER_CHANNEL; ++coefficient)
            CHECK(splat.m_sh[(coefficient + 1) * 3 + axis] ==
                  static_cast<float>(axis * K_COEFFICIENTS_PER_CHANNEL + coefficient + 1) / 16);
    for (std::size_t index { K_REST + 3uz }; index < 48uz; ++index) CHECK(splat.m_sh[index] == 0);
    CHECK(transform_point(chunk.m_world_from_chunk, {1, 2, 3}) == Vec3d { 1, -2, -3 });
    CHECK_FALSE(chunk.m_geometry_valid);
    CHECK((*K_LOADED)->m_scale == ScaleStatus::ARBITRARY);
    const auto K_AFTER = sha256_file(fixture.m_path, 16384);
    REQUIRE(K_AFTER);
    CHECK(*K_AFTER == *K_BEFORE);
}

TEST_CASE("gsplat PLY malformed headers and unsupported layouts preserve prior snapshots",
          "[bundle_admission][ply][header]") {
    const auto K_FIELD =
        GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    CAPTURE(K_FIELD);
    PlyFixture fixture;
    const auto K_ORIGINAL = fixture.header() + PlyFixture::row();
    fixture.write(K_ORIGINAL);
    const auto K_RETAINED = load_gsplat_ply(fixture.m_path, {42});
    REQUIRE(K_RETAINED);
    const auto K_HASH = scene_content_hash(**K_RETAINED);
    auto header = fixture.header();
    BundleLimits limits;
    const auto K_REPLACE = [&](std::string_view from, std::string_view to) {
        const auto K_POSITION = header.find(from);
        REQUIRE(K_POSITION != std::string::npos);
        header.replace(K_POSITION, from.size(), to);
    };
    if (K_FIELD == 0) header.clear();
    if (K_FIELD == 1) K_REPLACE("ply\n", "not-ply\n");
    if (K_FIELD == 2) K_REPLACE("end_header\n", "");
    if (K_FIELD == 3) K_REPLACE("format ascii 1.0\n", "");
    if (K_FIELD == 4) K_REPLACE("comment exporter gsplat-1.5.3\n", "");
    if (K_FIELD == 5) K_REPLACE("comment color_domain srgb\n", "");
    if (K_FIELD == 6)
        header = "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain "
                 "srgb\nend_header\n";
    if (K_FIELD == 7) K_REPLACE("vertex 1", "vertex x");
    if (K_FIELD == 8) K_REPLACE("vertex 1", "vertex 1x");
    if (K_FIELD == 9) K_REPLACE("vertex 1", "vertex 0");
    if (K_FIELD == 10) limits.m_scene.m_max_splats = 0;
    if (K_FIELD == 11) limits.m_scene.m_max_payload_bytes = sizeof(Gaussian) - 1;
    if (K_FIELD == 12) K_REPLACE("property float x\n", "property float x\nproperty float x\n");
    if (K_FIELD == 13) K_REPLACE("property float x\n", "");
    if (K_FIELD == 14) K_REPLACE("end_header", "property float f_rest_0\nend_header");
    if (K_FIELD == 15) K_REPLACE("end_header", "property float unknown\nend_header");
    if (K_FIELD == 16) K_REPLACE("property float x", "property double x");
    if (K_FIELD == 17) K_REPLACE("element vertex 1", "element vertex 1\nelement vertex 1");
    if (K_FIELD == 18) {
        std::string properties;
        for (int index { 0 }; index < 65; ++index) properties += "property float x\n";
        K_REPLACE("end_header", properties + "end_header");
    }
    if (K_FIELD == 19) header = "ply\n" + std::string(65537, '\r') + "end_header\n";
    // EOF is intentional for missing/unfinished headers; no vertex text can mask it.
    const auto K_MALFORMED = header + (K_FIELD <= 2 ? "" : PlyFixture::row());
    fixture.write(K_MALFORMED);
    const auto K_BEFORE = sha256_file(fixture.m_path, 131072);
    REQUIRE(K_BEFORE);
    const auto K_REJECTED = load_gsplat_ply(fixture.m_path, {42}, limits);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    const auto K_AFTER = sha256_file(fixture.m_path, 131072);
    REQUIRE(K_AFTER);
    CHECK(*K_AFTER == *K_BEFORE);
    CHECK(scene_content_hash(**K_RETAINED) == K_HASH);
    fixture.write(K_ORIGINAL);
    const auto K_REPAIRED = load_gsplat_ply(fixture.m_path, {42});
    REQUIRE(K_REPAIRED);
    CHECK(scene_content_hash(**K_REPAIRED) == K_HASH);
}

TEST_CASE("gsplat PLY rejects invalid numeric rows sizes and trailing data without altering input",
          "[bundle_admission][ply][numeric]") {
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    PlyFixture fixture;
    std::vector<std::string> values {
        "1", "2", "3", ".25", ".5", ".75", "0", "0", "0", "0", "1", "0", "0", "0"};
    if (K_FIELD == 0) values.clear();
    if (K_FIELD == 1) values[0] = "NaN";
    if (K_FIELD == 2) values[0] = "1e300";
    if (K_FIELD == 3) values[3] = "1e300";
    if (K_FIELD == 4) values[7] = "1000";
    if (K_FIELD == 5) values[7] = "-1000";
    if (K_FIELD == 6) values[10] = "0";
    if (K_FIELD == 7) values[10] = "1e300";
    if (K_FIELD == 8) values.push_back("extra");
    if (K_FIELD == 9) values.pop_back();
    std::string row;
    for (const auto& value : values) row += value + " ";
    if (K_FIELD != 0) row += '\n';
    if (K_FIELD == 10) row += "extra\n";
    if (K_FIELD == 11) row += std::string(16385, '\r') + '\n';
    fixture.write(fixture.header() + row);
    const auto K_BEFORE = sha256_file(fixture.m_path, 32768);
    REQUIRE(K_BEFORE);
    const auto K_REJECTED = load_gsplat_ply(fixture.m_path, {42});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    const auto K_AFTER = sha256_file(fixture.m_path, 32768);
    REQUIRE(K_AFTER);
    CHECK(*K_AFTER == *K_BEFORE);
}

TEST_CASE("gsplat PLY failed opens and invalid scene admission remain explicit",
          "[bundle_admission][ply][config]") {
    PlyFixture fixture;
    const auto K_MISSING = load_gsplat_ply(fixture.m_path, {42});
    REQUIRE(!K_MISSING);
    CHECK(K_MISSING.error().m_code == ErrorCode::IO_ERROR);
    fixture.write(fixture.header() + PlyFixture::row());
    const auto K_INVALID_ID = load_gsplat_ply(fixture.m_path, {});
    REQUIRE(!K_INVALID_ID);
    CHECK(K_INVALID_ID.error().m_code == ErrorCode::INVALID_DATA);
    BundleLimits limits;
    limits.m_scene.m_max_chunks = 0;
    const auto K_EXHAUSTED = load_gsplat_ply(fixture.m_path, {42}, limits);
    REQUIRE(!K_EXHAUSTED);
    CHECK(K_EXHAUSTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
}
