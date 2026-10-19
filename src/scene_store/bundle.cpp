#include "world_explorer/scene_store/bundle.hpp"

#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace world_explorer {
namespace {

using Json = nlohmann::json;
constexpr std::size_t K_SPLAT_BYTES { 240uz };

struct Encoder {
    std::vector<std::byte> m_bytes;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): binary encoding uses value, byte width.
    void integer(std::uint64_t value, std::uint8_t count = 8) {
        for (std::size_t i { 0uz }; i < count; ++i) {
            m_bytes.push_back(static_cast<std::byte>(value & 255U));
            value >>= 8U;
        }
    }

    void real(double value) { integer(std::bit_cast<std::uint64_t>(value)); }

    void real(float value) { integer(std::bit_cast<std::uint32_t>(value), 4); }

    void text(std::string_view value) {
        for (char character : value) m_bytes.push_back(static_cast<std::byte>(character));
    }
};

struct Decoder {
    std::span<const std::byte> m_bytes;
    std::size_t m_position {};

    std::uint64_t integer(std::uint8_t count = 8) {
        if (count > m_bytes.size() - m_position) throw std::runtime_error("truncated binary asset");
        std::uint64_t value {};
        for (std::size_t i { 0uz }; i < count; ++i)
            value |= std::uint64_t(std::to_integer<unsigned char>(m_bytes[m_position++]))
                     << (i * 8);
        return value;
    }

    double real64() { return std::bit_cast<double>(integer()); }

    float real32() { return std::bit_cast<float>(static_cast<std::uint32_t>(integer(4))); }

    void magic(std::string_view expected) {
        for (char character : expected)
            if (integer(1) != static_cast<unsigned char>(character))
                throw std::runtime_error("unsupported binary asset version");
    }

    std::string text(std::size_t length) {
        if (length > m_bytes.size() - m_position) throw std::runtime_error("truncated binary text");
        std::string result;
        result.reserve(length);
        for (std::size_t i { 0uz }; i < length; ++i) result.push_back(static_cast<char>(integer(1)));
        return result;
    }
};

bool hash_valid(std::string_view hash) {
    return hash.size() == 64 &&
           hash.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

std::filesystem::path asset_path(const std::filesystem::path& root, const std::string& name) {
    // Only one filename component is accepted; nested paths are not part of v1.
    if (name.empty() || name.size() > 128 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.-") != std::string::npos ||
        name == "." || name == "..")
        throw std::runtime_error("unsafe asset path");
    const auto K_PATH = root / name;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(K_PATH)))
        throw std::runtime_error("symlink asset rejected");
    return K_PATH;
}

std::vector<std::byte> read_file(const std::filesystem::path& path, std::size_t maximum) {
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("asset is not a regular file");
    const auto K_SIZE = std::filesystem::file_size(path);
    if (K_SIZE > maximum ||
        K_SIZE > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("asset byte budget exceeded");
    std::vector<std::byte> bytes(static_cast<std::size_t>(K_SIZE));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("asset read changed or failed");
    return bytes;
}

Json parse_json(std::span<const std::byte> bytes) {
    // JSON consumes characters; std::char_traits<std::byte> is not portable.
    const auto K_TEXT =
        bytes.empty() ? std::string_view {}
                      : std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (K_TEXT.find('\0') != K_TEXT.npos) throw std::runtime_error("raw NUL in bundle JSON");
    std::vector<std::set<std::string>> keys;
    return Json::parse(K_TEXT, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 8) throw std::runtime_error("bundle JSON nesting exceeds budget");
        const auto K_DEPTH = static_cast<std::size_t>(depth);
        if (event == Json::parse_event_t::object_start) {
            keys.resize(K_DEPTH + 1uz);
            keys[K_DEPTH].clear();
        }
        if (event == Json::parse_event_t::key &&
            !keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second)
            throw std::runtime_error("duplicate bundle JSON key");
        return true;
    });
}

void write_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    static std::atomic<std::uint64_t> sequence {};
#ifdef _WIN32
    const auto K_PROCESS = GetCurrentProcessId();
#else
    const auto K_PROCESS = getpid();
#endif
    auto temporary = path;
    temporary += ".tmp." + std::to_string(K_PROCESS) + "." + std::to_string(sequence.fetch_add(1));
    std::FILE* file {};
#ifdef _WIN32
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0) file = nullptr;
#else
    file = std::fopen(temporary.c_str(), "wb");
#endif
    if (!file) throw std::runtime_error("cannot create staged asset");
    const bool K_WRITTEN =
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && std::fflush(file) == 0;
#ifdef _WIN32
    const bool K_FLUSHED = K_WRITTEN && _commit(_fileno(file)) == 0;
#else
    const bool K_FLUSHED = K_WRITTEN && fsync(fileno(file)) == 0;
#endif
    const bool K_CLOSED = std::fclose(file) == 0;
    if (!K_FLUSHED || !K_CLOSED) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("asset durable write failed");
    }
    try {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),
                         path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("manifest atomic replace failed");
#else
        std::filesystem::rename(temporary, path);
        const int K_DIRECTORY = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (K_DIRECTORY < 0) throw std::runtime_error("cannot open asset directory for durability");
        const int K_FLUSHED_DIRECTORY = fsync(K_DIRECTORY);
        close(K_DIRECTORY);
        if (K_FLUSHED_DIRECTORY != 0) throw std::runtime_error("asset directory flush failed");
#endif
    } catch (...) {
        std::filesystem::remove(temporary);
        throw;
    }
}

void put_transform(Encoder& output, const SimilarityTransform& transform) {
    for (double value : {transform.m_rotation.m_w,
                         transform.m_rotation.m_x,
                         transform.m_rotation.m_y,
                         transform.m_rotation.m_z,
                         transform.m_translation.m_x,
                         transform.m_translation.m_y,
                         transform.m_translation.m_z,
                         transform.m_scale})
        output.real(value);
}

SimilarityTransform get_transform(Decoder& input) {
    SimilarityTransform transform;
    transform.m_rotation = { input.real64(), input.real64(), input.real64(), input.real64() };
    transform.m_translation = { input.real64(), input.real64(), input.real64() };
    transform.m_scale = input.real64();
    return transform;
}

Encoder encode_chunk(const SceneChunk& chunk) {
    Encoder output;
    output.text(chunk.m_mesh && chunk.m_mesh->m_baked ? "WEXCHK03"
                : chunk.m_mesh                        ? "WEXCHK02"
                                                      : "WEXCHK01");
    output.integer(chunk.m_id.m_value);
    output.integer(chunk.m_generation);
    output.integer(chunk.m_pose_revision.m_value);
    output.integer(chunk.m_geometry_valid ? 1 : 0);
    put_transform(output, chunk.m_world_from_chunk);
    for (double value : {chunk.m_bounds.m_minimum.m_x,
                         chunk.m_bounds.m_minimum.m_y,
                         chunk.m_bounds.m_minimum.m_z,
                         chunk.m_bounds.m_maximum.m_x,
                         chunk.m_bounds.m_maximum.m_y,
                         chunk.m_bounds.m_maximum.m_z})
        output.real(value);
    output.integer(chunk.m_splats.size());
    output.integer(chunk.m_source_frames.size());
    for (auto frame : chunk.m_source_frames) output.integer(frame.m_value);
    for (const auto& splat : chunk.m_splats) {
        for (float value : splat.m_mean) output.real(value);
        for (float value : splat.m_scale) output.real(value);
        for (float value : splat.m_rotation) output.real(value);
        output.real(splat.m_opacity);
        for (float value : splat.m_sh) output.real(value);
        output.integer(splat.m_sh_degree, 4);
    }
    if (chunk.m_mesh) {
        const auto& mesh = *chunk.m_mesh;
        output.text("WEXMSH01");
        output.integer(mesh.m_vertices.size());
        output.integer(mesh.m_triangles.size());
        output.integer(mesh.m_observed.has_value());
        if (mesh.m_observed) {
            const auto& source = *mesh.m_observed;
            output.integer(source.m_context.m_epoch.m_value);
            output.integer(source.m_context.m_pose_revision.m_value);
            for (const auto& text : {source.m_context.m_manifest_sha256,
                                     source.m_context.m_receipt_sha256,
                                     source.m_context.m_recipe_sha256,
                                     source.m_context.m_pose_sha256,
                                     source.m_asset_sha256,
                                     source.m_manifest_json,
                                     source.m_receipt_json,
                                     source.m_cleanup_sha256,
                                     source.m_cleanup_json}) {
                output.integer(text.size(), 4);
                output.text(text);
            }
        }
        for (const auto& vertex : mesh.m_vertices) {
            for (float value : vertex.m_position) output.real(value);
            for (float value : vertex.m_normal) output.real(value);
        }
        for (const auto& triangle : mesh.m_triangles)
            for (auto index : triangle) output.integer(index, 4);
        if (mesh.m_baked) {
            const auto& baked = *mesh.m_baked;
            output.text("WEXMAT01");
            output.integer(baked.m_width, 4);
            output.integer(baked.m_height, 4);
            output.integer(baked.m_frames.size(), 4);
            for (auto frame : baked.m_frames) output.integer(frame.m_value);
            for (const auto& text :
                 { baked.m_source_chunk_sha256, baked.m_manifest_sha256, baked.m_manifest_json }) {
                output.integer(text.size(), 4);
                output.text(text);
            }
            for (const auto& uv : baked.m_corner_uv)
                for (float value : uv) output.real(value);
            for (const auto& color : baked.m_rgba)
                for (float value : color) output.real(value);
            for (auto owner : baked.m_owners) output.integer(owner.m_value);
            for (auto mask : baked.m_candidates) output.integer(mask, 4);
        }
    }
    return output;
}

SceneMesh decode_mesh(Decoder& input, SceneLimits limits, bool has_baked) {
    input.magic("WEXMSH01");
    const auto K_VERTICES = input.integer(), K_TRIANGLES = input.integer();
    if (K_VERTICES > limits.m_max_mesh_vertices || K_TRIANGLES > limits.m_max_mesh_triangles ||
        K_VERTICES > limits.m_max_payload_bytes / 24uz ||
        K_TRIANGLES > (limits.m_max_payload_bytes - K_VERTICES * 24uz) / 12uz)
        throw std::runtime_error("mesh element count exceeds budget");
    SceneMesh mesh;
    const auto K_SOURCE = input.integer();
    if (K_SOURCE > 1) throw std::runtime_error("invalid mesh source status");
    if (K_SOURCE) {
        auto& source = mesh.m_observed.emplace();
        source.m_context.m_epoch = { input.integer() };
        source.m_context.m_pose_revision = { input.integer() };
        const auto K_TEXT = [&](std::size_t maximum) {
            const auto K_LENGTH = input.integer(4);
            if (K_LENGTH > maximum) throw std::runtime_error("mesh metadata exceeds budget");
            return input.text(static_cast<std::size_t>(K_LENGTH));
        };
        source.m_context.m_manifest_sha256 = K_TEXT(64uz);
        source.m_context.m_receipt_sha256 = K_TEXT(64uz);
        source.m_context.m_recipe_sha256 = K_TEXT(64uz);
        source.m_context.m_pose_sha256 = K_TEXT(64uz);
        source.m_asset_sha256 = K_TEXT(64uz);
        source.m_manifest_json = K_TEXT(4uz * 1024uz * 1024uz);
        source.m_receipt_json = K_TEXT(4uz * 1024uz * 1024uz);
        source.m_cleanup_sha256 = K_TEXT(64uz);
        source.m_cleanup_json = K_TEXT(4uz * 1024uz * 1024uz);
    }
    const auto K_REMAINING = input.m_bytes.size() - input.m_position;
    if (K_VERTICES > K_REMAINING / 24uz || K_TRIANGLES > (K_REMAINING - K_VERTICES * 24uz) / 12uz ||
        (!has_baked && (K_TRIANGLES != (K_REMAINING - K_VERTICES * 24uz) / 12uz ||
                        (K_REMAINING - K_VERTICES * 24uz) % 12uz != 0)))
        throw std::runtime_error("mesh counts do not match exact byte size");
    mesh.m_vertices.resize(static_cast<std::size_t>(K_VERTICES));
    mesh.m_triangles.resize(static_cast<std::size_t>(K_TRIANGLES));
    for (auto& vertex : mesh.m_vertices) {
        for (auto& value : vertex.m_position) value = input.real32();
        for (auto& value : vertex.m_normal) value = input.real32();
    }
    for (auto& triangle : mesh.m_triangles)
        for (auto& index : triangle) index = static_cast<std::uint32_t>(input.integer(4));
    if (has_baked) {
        input.magic("WEXMAT01");
        auto& baked = mesh.m_baked.emplace();
        baked.m_width = static_cast<std::uint32_t>(input.integer(4));
        baked.m_height = static_cast<std::uint32_t>(input.integer(4));
        const auto K_FRAMES = input.integer(4);
        if (!baked.m_width || !baked.m_height || baked.m_width > 8192 || baked.m_height > 8192 ||
            baked.m_height > limits.m_max_texture_texels / baked.m_width || K_FRAMES > 18)
            throw std::runtime_error("Texture dimensions or frames exceed budget");
        for (std::uint64_t i { 0 }; i < K_FRAMES; ++i) baked.m_frames.push_back({ input.integer() });
        for (auto field : {std::ref(baked.m_source_chunk_sha256),
                           std::ref(baked.m_manifest_sha256),
                           std::ref(baked.m_manifest_json)}) {
            const auto K_LENGTH = input.integer(4);
            const auto K_LIMIT = &field.get() == &baked.m_manifest_json ? 1024uz * 1024uz : 64uz;
            if (K_LENGTH > K_LIMIT) throw std::runtime_error("Texture metadata exceeds budget");
            field.get() = input.text(static_cast<std::size_t>(K_LENGTH));
        }
        const auto K_PIXELS = static_cast<std::size_t>(baked.m_width) * baked.m_height;
        if (K_TRIANGLES > limits.m_max_payload_bytes / 24uz ||
            K_PIXELS > (limits.m_max_payload_bytes - K_TRIANGLES * 24uz) / 28uz ||
            input.m_bytes.size() - input.m_position != K_TRIANGLES * 24uz + K_PIXELS * 28uz)
            throw std::runtime_error("Texture counts differ from exact payload size/budget");
        baked.m_corner_uv.resize(static_cast<std::size_t>(K_TRIANGLES) * 3uz);
        baked.m_rgba.resize(K_PIXELS);
        baked.m_owners.resize(K_PIXELS);
        baked.m_candidates.resize(K_PIXELS);
        for (auto& uv : baked.m_corner_uv)
            for (auto& value : uv) value = input.real32();
        for (auto& color : baked.m_rgba)
            for (auto& value : color) value = input.real32();
        for (auto& owner : baked.m_owners) owner = { input.integer() };
        for (auto& mask : baked.m_candidates) mask = static_cast<std::uint32_t>(input.integer(4));
    }
    return mesh;
}

std::shared_ptr<const SceneChunk>
decode_chunk(std::span<const std::byte> bytes, SceneLimits limits, std::uint64_t schema) {
    Decoder input { bytes };
    const auto K_MAGIC = input.text(8uz);
    const bool K_BAKED = K_MAGIC == "WEXCHK03" && schema == 3;
    const bool K_MESH = (K_MAGIC == "WEXCHK02" && schema >= 2) || K_BAKED;
    if (K_MAGIC != "WEXCHK01" && !K_MESH)
        throw std::runtime_error("unsupported binary chunk version");
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { input.integer() };
    chunk->m_generation = input.integer();
    chunk->m_pose_revision = { input.integer() };
    const auto K_GEOMETRY = input.integer();
    if (K_GEOMETRY > 1) throw std::runtime_error("invalid geometry status");
    chunk->m_geometry_valid = K_GEOMETRY != 0;
    chunk->m_world_from_chunk = get_transform(input);
    chunk->m_bounds = { { input.real64(), input.real64(), input.real64() },
                       { input.real64(), input.real64(), input.real64() } };
    const auto K_SPLATS = input.integer(), K_FRAMES = input.integer();
    if (K_SPLATS > limits.m_max_splats ||
        K_SPLATS > limits.m_max_payload_bytes / sizeof(Gaussian) ||
        K_FRAMES > limits.m_max_source_frames || K_FRAMES > (bytes.size() - input.m_position) / 8)
        throw std::runtime_error("chunk element count exceeds budget");
    chunk->m_source_frames.reserve(static_cast<std::size_t>(K_FRAMES));
    for (std::uint64_t i { 0 }; i < K_FRAMES; ++i)
        chunk->m_source_frames.push_back({input.integer()});
    if (K_SPLATS > (bytes.size() - input.m_position) / K_SPLAT_BYTES ||
        (!K_MESH && (K_SPLATS != (bytes.size() - input.m_position) / K_SPLAT_BYTES ||
                     (bytes.size() - input.m_position) % K_SPLAT_BYTES != 0)))
        throw std::runtime_error("chunk count does not match exact byte size");
    chunk->m_splats.resize(static_cast<std::size_t>(K_SPLATS));
    for (auto& splat : chunk->m_splats) {
        for (float& value : splat.m_mean) value = input.real32();
        for (float& value : splat.m_scale) value = input.real32();
        for (float& value : splat.m_rotation) value = input.real32();
        splat.m_opacity = input.real32();
        for (float& value : splat.m_sh) value = input.real32();
        const auto K_DEGREE = input.integer(4);
        if (K_DEGREE > 3) throw std::runtime_error("unsupported SH degree");
        splat.m_sh_degree = static_cast<std::uint8_t>(K_DEGREE);
    }
    if (K_MESH) chunk->m_mesh = decode_mesh(input, limits, K_BAKED);
    auto checked = validate_chunk(*chunk, limits);
    if (!checked) throw std::runtime_error(checked.error().m_message);
    return chunk;
}

std::uint64_t unsigned_field(const Json& object, const char* name) {
    const auto& field = object.at(name);
    if (!field.is_number_unsigned())
        throw std::runtime_error(std::string("invalid unsigned field: ") + name);
    return field.get<std::uint64_t>();
}

void expect(const Json& object, const char* key, const char* value) {
    if (object.at(key) != value)
        throw std::runtime_error(std::string("unsupported convention: ") + key);
}

std::span<const std::byte> as_bytes(const std::string& text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

bool bounded_line(std::istream& input, std::string& line, std::size_t limit, std::size_t& bytes) {
    line.clear();
    bytes = 0;
    char character {};
    while (input.get(character)) {
        if (bytes == limit) throw std::runtime_error("PLY line exceeds length limit");
        ++bytes;
        if (character == '\n') return true;
        if (character != '\r') line.push_back(character);
    }
    return bytes != 0;
}

}  // namespace

Result<void> write_bundle(const std::filesystem::path& directory, const SceneSnapshot& snapshot) {
    try {
        if (!snapshot) return fail(ErrorCode::INVALID_ARGUMENT, "missing bundle snapshot");
        auto checked = validate_scene(*snapshot);
        if (!checked) return std::unexpected(checked.error());
        const bool K_MESH = std::ranges::any_of(
            snapshot->m_chunks, [](const auto& entry) { return entry.second->m_mesh.has_value(); });
        const bool K_BAKED = std::ranges::any_of(snapshot->m_chunks, [](const auto& entry) {
            return entry.second->m_mesh && entry.second->m_mesh->m_baked;
        });
        std::filesystem::create_directories(directory);
        Encoder index;
        index.text("WEXIDX01");
        index.integer(snapshot->m_chunks.size());
        for (const auto& [K_ID, K_CHUNK] : snapshot->m_chunks) {
            auto encoded = encode_chunk(*K_CHUNK);
            const auto K_HASH = sha256(encoded.m_bytes);
            const auto K_NAME = "chunk_" + K_HASH + ".bin";
            write_atomic(asset_path(directory, K_NAME), encoded.m_bytes);
            index.integer(K_ID.m_value);
            index.integer(encoded.m_bytes.size());
            index.text(K_HASH);
            index.integer(K_NAME.size(), 4);
            index.text(K_NAME);
        }
        const auto K_INDEX_HASH = sha256(index.m_bytes);
        const auto K_INDEX_NAME = "index_" + K_INDEX_HASH + ".bin";
        write_atomic(asset_path(directory, K_INDEX_NAME), index.m_bytes);
        const auto K_PROVENANCE = Json {
            {"schema_version", 1},
            {"producer", "world_explorer"},
            {"metadata", snapshot->m_provenance},
            {"training_checkpoint",
             false}}.dump(2);
        const auto K_PROVENANCE_HASH = sha256(as_bytes(K_PROVENANCE));
        const auto K_PROVENANCE_NAME = "provenance_" + K_PROVENANCE_HASH + ".json";
        write_atomic(asset_path(directory, K_PROVENANCE_NAME), as_bytes(K_PROVENANCE));
        Json manifest = {
            {"schema_version",
             K_BAKED  ? 3
             : K_MESH ? 2
                      : 1},
            { "scene_id", snapshot->m_id.m_value },
            { "epoch", snapshot->m_epoch.m_value },
            { "revision", snapshot->m_revision.m_value },
            { "coordinates",
             { { "handedness", "right" },
              { "world_up", "+y" },
              { "camera_axes", "+x_right,+y_down,+z_forward" },
              { "pose_convention", "world_from_camera" },
              { "quaternion_order", "wxyz" },
              { "pixel_centers", "integer" },
              { "scale_status",
               snapshot->m_scale == ScaleStatus::ARBITRARY ? "ARBITRARY" : "METRIC_MEASURED" } } },
            { "appearance",
             { { "representation", "gaussian_3d" },
              { "lighting", "captured" },
              { "color_domain", "srgb" },
              { "sh_basis", "real_3dgs_coefficient_major_rgb" } } },
            { "geometry", { { "collision_status", "UNAVAILABLE" } } },
            { "compression", "none" },
            { "index", K_INDEX_NAME },
            { "index_sha256", K_INDEX_HASH },
            { "index_bytes", index.m_bytes.size() },
            { "provenance", K_PROVENANCE_NAME },
            { "provenance_sha256", K_PROVENANCE_HASH } };
        if (K_MESH) {
            manifest["geometry"]["representation"] = "indexed_position_normal";
            manifest["geometry"]["encoding"] = "WEXCHK02/WEXMSH01";
            manifest["geometry"]["source_validation"] = "snapshot_dependencies";
        }
        if (K_BAKED) {
            manifest["geometry"]["representation"] = "indexed_position_normal_baked_unlit";
            manifest["geometry"]["encoding"] = "WEXCHK03/WEXMSH01/WEXMAT01";
            manifest["geometry"]["lighting"] = "captured_unlit";
        }
        const auto K_TEXT = manifest.dump(2);
        write_atomic(asset_path(directory, "manifest.json"), as_bytes(K_TEXT));
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "bundle allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::IO_ERROR, exception.what());
    }
}

Result<SceneSnapshot> load_bundle(const std::filesystem::path& directory, BundleLimits limits) {
    try {
        const auto K_BYTES =
            read_file(asset_path(directory, "manifest.json"), limits.m_max_manifest_bytes);
        const auto K_MANIFEST = parse_json(K_BYTES);
        const auto K_SCHEMA = unsigned_field(K_MANIFEST, "schema_version");
        if (K_SCHEMA != 1 && K_SCHEMA != 2 && K_SCHEMA != 3)
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "unsupported scene schema major version");
        if (K_SCHEMA >= 2) {
            const auto& geometry = K_MANIFEST.at("geometry");
            expect(geometry, "collision_status", "UNAVAILABLE");
            expect(geometry,
                   "representation",
                   K_SCHEMA == 3 ? "indexed_position_normal_baked_unlit"
                                 : "indexed_position_normal");
            expect(geometry,
                   "encoding",
                   K_SCHEMA == 3 ? "WEXCHK03/WEXMSH01/WEXMAT01" : "WEXCHK02/WEXMSH01");
            if (K_SCHEMA == 3) expect(geometry, "lighting", "captured_unlit");
            expect(geometry, "source_validation", "snapshot_dependencies");
        }
        const auto& coordinates = K_MANIFEST.at("coordinates");
        expect(coordinates, "handedness", "right");
        expect(coordinates, "world_up", "+y");
        expect(coordinates, "camera_axes", "+x_right,+y_down,+z_forward");
        expect(coordinates, "pose_convention", "world_from_camera");
        expect(coordinates, "quaternion_order", "wxyz");
        expect(coordinates, "pixel_centers", "integer");
        expect(K_MANIFEST.at("appearance"), "representation", "gaussian_3d");
        expect(K_MANIFEST.at("appearance"), "lighting", "captured");
        expect(K_MANIFEST.at("appearance"), "color_domain", "srgb");
        expect(K_MANIFEST.at("appearance"), "sh_basis", "real_3dgs_coefficient_major_rgb");
        expect(K_MANIFEST, "compression", "none");
        auto state = std::make_shared<SceneState>();
        state->m_id = { unsigned_field(K_MANIFEST, "scene_id") };
        state->m_epoch = { unsigned_field(K_MANIFEST, "epoch") };
        state->m_revision = { unsigned_field(K_MANIFEST, "revision") };
        const auto K_SCALE = coordinates.at("scale_status").get<std::string>();
        if (K_SCALE == "METRIC_MEASURED") state->m_scale = ScaleStatus::METRIC_MEASURED;
        else if (K_SCALE != "ARBITRARY") throw std::runtime_error("unsupported scale status");
        const auto K_INDEX_HASH = K_MANIFEST.at("index_sha256").get<std::string>();
        if (!hash_valid(K_INDEX_HASH)) throw std::runtime_error("invalid index hash");
        if (limits.m_scene.m_max_chunks > (std::numeric_limits<std::size_t>::max() - 16) / 256)
            throw std::runtime_error("invalid chunk limit");
        const auto K_INDEX_BYTES =
            read_file(asset_path(directory, K_MANIFEST.at("index").get<std::string>()),
                      16 + limits.m_scene.m_max_chunks * 256);
        if (K_INDEX_BYTES.size() != unsigned_field(K_MANIFEST, "index_bytes") ||
            sha256(K_INDEX_BYTES) != K_INDEX_HASH)
            throw std::runtime_error("index checksum/size mismatch");
        const auto K_PROVENANCE_HASH = K_MANIFEST.at("provenance_sha256").get<std::string>();
        const auto K_PROVENANCE =
            read_file(asset_path(directory, K_MANIFEST.at("provenance").get<std::string>()),
                      limits.m_max_manifest_bytes);
        if (!hash_valid(K_PROVENANCE_HASH) || sha256(K_PROVENANCE) != K_PROVENANCE_HASH)
            throw std::runtime_error("provenance checksum mismatch");
        const auto K_PROVENANCE_VALUE = parse_json(K_PROVENANCE);
        if (K_PROVENANCE_VALUE.at("schema_version") != 1)
            throw std::runtime_error("unsupported provenance schema");
        if (K_PROVENANCE_VALUE.contains("metadata"))
            state->m_provenance =
                K_PROVENANCE_VALUE.at("metadata").get<std::map<std::string, std::string>>();
        Decoder index { K_INDEX_BYTES };
        index.magic("WEXIDX01");
        const auto K_COUNT = index.integer();
        if (K_COUNT > limits.m_scene.m_max_chunks)
            throw std::runtime_error("index chunk count exceeds budget");
        std::size_t total_bytes {}, total_splats {}, total_vertices {}, total_triangles {},
            total_texels {};
        bool has_mesh = false, has_baked = false;
        for (std::uint64_t i { 0 }; i < K_COUNT; ++i) {
            const ChunkId K_ID { index.integer() };
            const auto K_SIZE = index.integer();
            const auto K_HASH = index.text(64);
            const auto K_NAME = index.text(static_cast<std::size_t>(index.integer(4)));
            if (!hash_valid(K_HASH) || K_SIZE > limits.m_scene.m_max_payload_bytes - total_bytes)
                throw std::runtime_error("asset size/hash exceeds budget");
            auto chunk_bytes =
                read_file(asset_path(directory, K_NAME), static_cast<std::size_t>(K_SIZE));
            if (chunk_bytes.size() != K_SIZE || sha256(chunk_bytes) != K_HASH)
                throw std::runtime_error("chunk checksum/size mismatch");
            auto chunk_limits = limits.m_scene;
            chunk_limits.m_max_splats -= total_splats;
            chunk_limits.m_max_mesh_vertices -= total_vertices;
            chunk_limits.m_max_mesh_triangles -= total_triangles;
            chunk_limits.m_max_texture_texels -= total_texels;
            auto chunk = decode_chunk(chunk_bytes, chunk_limits, K_SCHEMA);
            if (chunk->m_id != K_ID || !state->m_chunks.emplace(K_ID, chunk).second)
                throw std::runtime_error("duplicate/mismatched chunk ID");
            total_bytes += chunk_bytes.size();
            if (chunk->m_splats.size() > limits.m_scene.m_max_splats - total_splats)
                throw std::runtime_error("aggregate splat count exceeds budget");
            total_splats += chunk->m_splats.size();
            if (chunk->m_mesh) {
                has_mesh = true;
                if (chunk->m_mesh->m_baked) {
                    has_baked = true;
                    total_texels += chunk->m_mesh->m_baked->m_rgba.size();
                }
                total_vertices += chunk->m_mesh->m_vertices.size();
                total_triangles += chunk->m_mesh->m_triangles.size();
            }
        }
        if (K_SCHEMA == 3 && !has_baked)
            throw std::runtime_error("Texture schema requires a baked mesh");
        if (K_SCHEMA >= 2 && !has_mesh)
            throw std::runtime_error("mesh schema requires a mesh chunk");
        if (index.m_position != index.m_bytes.size())
            throw std::runtime_error("trailing index bytes");
        auto checked = validate_scene(*state, limits.m_scene);
        if (!checked) return std::unexpected(checked.error());
        return SceneSnapshot { std::move(state) };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "bundle allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

Result<SceneSnapshot>
load_gsplat_ply(const std::filesystem::path& path, SceneId id, BundleLimits limits) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) return fail(ErrorCode::IO_ERROR, "cannot open PLY input");
        std::string line;
        std::size_t header_bytes {}, line_bytes {}, count {};
        bool ascii {}, exporter {}, color {}, vertices {}, header_done {};
        std::vector<std::string> properties;
        if (!bounded_line(input, line, limits.m_max_ply_header_bytes, header_bytes) ||
            line != "ply")
            throw std::runtime_error("missing PLY magic");
        while (
            bounded_line(input, line, limits.m_max_ply_header_bytes - header_bytes, line_bytes)) {
            header_bytes += line_bytes;
            if (line == "end_header") {
                header_done = true;
                break;
            }
            if (line == "format ascii 1.0") {
                ascii = true;
                continue;
            }
            if (line == "comment exporter gsplat-1.5.3") {
                exporter = true;
                continue;
            }
            if (line == "comment color_domain srgb") {
                color = true;
                continue;
            }
            if (line.starts_with("comment ")) continue;
            if (line.starts_with("element vertex ") && !vertices) {
                const auto K_TEXT = std::string_view(line).substr(15);
                const auto K_PARSED =
                    std::from_chars(K_TEXT.data(), K_TEXT.data() + K_TEXT.size(), count);
                if (K_PARSED.ec != std::errc {} || K_PARSED.ptr != K_TEXT.data() + K_TEXT.size() ||
                    count == 0 || count > limits.m_scene.m_max_splats ||
                    count > limits.m_scene.m_max_payload_bytes / sizeof(Gaussian))
                    throw std::runtime_error("invalid/excessive PLY vertex count");
                vertices = true;
                continue;
            }
            if (line.starts_with("property float ") && vertices && properties.size() < 64) {
                properties.push_back(line.substr(15));
                continue;
            }
            throw std::runtime_error("unsupported PLY element, format or property type");
        }
        if (!header_done || !ascii || !exporter || !color || !vertices)
            throw std::runtime_error("PLY requires explicit gsplat-1.5.3/srgb ASCII profile");
        std::map<std::string, std::size_t> columns;
        for (std::size_t i { 0uz }; i < properties.size(); ++i)
            if (!columns.emplace(properties[i], i).second)
                throw std::runtime_error("duplicate PLY property");
        for (const auto* name : {"x",
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
                                 "rot_3"})
            if (!columns.contains(name))
                throw std::runtime_error(std::string("missing PLY property ") + name);
        std::size_t rest {};
        while (columns.contains("f_rest_" + std::to_string(rest))) ++rest;
        std::uint8_t degree {};
        if (rest == 9) degree = 1;
        else if (rest == 24) degree = 2;
        else if (rest == 45) degree = 3;
        else if (rest != 0) throw std::runtime_error("unsupported/incomplete SH layout");
        for (const auto& [K_NAME, K_COLUMN] : columns) {
            (void)K_COLUMN;
            if (K_NAME == "x" || K_NAME == "y" || K_NAME == "z" || K_NAME == "nx" ||
                K_NAME == "ny" || K_NAME == "nz" || K_NAME == "opacity")
                continue;
            bool known = false;
            for (const auto& [K_PREFIX, K_MAXIMUM] :
                 std::array<std::pair<std::string, std::size_t>, 4> {
                     {{"f_dc_", 3}, {"f_rest_", rest}, {"scale_", 3}, {"rot_", 4}}})
                for (std::size_t i { 0uz }; i < K_MAXIMUM; ++i)
                    if (K_NAME == K_PREFIX + std::to_string(i)) known = true;
            if (!known) throw std::runtime_error("unknown/ambiguous PLY property");
        }
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { 1 };
        chunk->m_pose_revision = { 1 };
        chunk->m_world_from_chunk.m_rotation = { 0, 1, 0, 0 };
        chunk->m_splats.reserve(count);
        chunk->m_bounds.m_minimum = { std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity() };
        chunk->m_bounds.m_maximum = chunk->m_bounds.m_minimum * (-1);
        for (std::size_t row { 0uz }; row < count; ++row) {
            if (!bounded_line(input, line, 16384, line_bytes))
                throw std::runtime_error("truncated PLY vertices");
            std::istringstream values(line);
            values.imbue(std::locale::classic());
            std::vector<double> fields;
            fields.reserve(properties.size());
            for (std::size_t column { 0uz }; column < properties.size(); ++column) {
                double value {};
                if (!(values >> value) || !std::isfinite(value))
                    throw std::runtime_error("nonfinite/invalid PLY numeric value");
                fields.push_back(value);
            }
            std::string extra;
            if (values >> extra) throw std::runtime_error("extra PLY vertex columns");
            const auto K_GET = [&](const std::string& name) { return fields[columns.at(name)]; };
            Gaussian splat;
            splat.m_sh_degree = degree;
            splat.m_mean = { static_cast<float>(K_GET("x")),
                            static_cast<float>(K_GET("y")),
                            static_cast<float>(K_GET("z")) };
            double length {};
            for (std::size_t axis { 0uz }; axis < 4uz; ++axis) {
                const auto K_VALUE = K_GET("rot_" + std::to_string(axis));
                length += K_VALUE * K_VALUE;
            }
            if (!std::isfinite(length) || length < 1e-30)
                throw std::runtime_error("degenerate PLY quaternion");
            for (std::size_t axis { 0uz }; axis < 4uz; ++axis)
                splat.m_rotation[axis] =
                    static_cast<float>(K_GET("rot_" + std::to_string(axis)) / std::sqrt(length));
            for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
                splat.m_scale[axis] =
                    static_cast<float>(std::exp(K_GET("scale_" + std::to_string(axis))));
                splat.m_sh[axis] = static_cast<float>(K_GET("f_dc_" + std::to_string(axis)));
                for (std::size_t coefficient { 0uz }; coefficient < rest / 3uz; ++coefficient)
                    splat.m_sh[(coefficient + 1) * 3 + axis] = static_cast<float>(
                        K_GET("f_rest_" + std::to_string(axis * (rest / 3) + coefficient)));
            }
            const auto K_LOGIT = K_GET("opacity");
            splat.m_opacity =
                static_cast<float>(K_LOGIT >= 0 ? 1 / (1 + std::exp(-K_LOGIT))
                                                : std::exp(K_LOGIT) / (1 + std::exp(K_LOGIT)));
            // Bounds cover the 3-sigma support, not only Gaussian centers.
            const double K_RADIUS =
                3 * std::max({splat.m_scale[0], splat.m_scale[1], splat.m_scale[2]});
            chunk->m_bounds.m_minimum = {
                std::min(chunk->m_bounds.m_minimum.m_x, splat.m_mean[0] - K_RADIUS),
                std::min(chunk->m_bounds.m_minimum.m_y, splat.m_mean[1] - K_RADIUS),
                std::min(chunk->m_bounds.m_minimum.m_z, splat.m_mean[2] - K_RADIUS) };
            chunk->m_bounds.m_maximum = {
                std::max(chunk->m_bounds.m_maximum.m_x, splat.m_mean[0] + K_RADIUS),
                std::max(chunk->m_bounds.m_maximum.m_y, splat.m_mean[1] + K_RADIUS),
                std::max(chunk->m_bounds.m_maximum.m_z, splat.m_mean[2] + K_RADIUS) };
            chunk->m_splats.push_back(splat);
        }
        while (bounded_line(input, line, 16384, line_bytes))
            if (line.find_first_not_of(" \t\r") != std::string::npos)
                throw std::runtime_error("trailing PLY data");
        auto state = std::make_shared<SceneState>();
        state->m_id = id;
        state->m_revision = { 1 };
        state->m_chunks.emplace(chunk->m_id, std::move(chunk));
        auto checked = validate_scene(*state, limits.m_scene);
        if (!checked) return std::unexpected(checked.error());
        return SceneSnapshot { std::move(state) };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "PLY allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

}  // namespace world_explorer
