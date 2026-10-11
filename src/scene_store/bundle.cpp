#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace world_explorer {
namespace {
using json = nlohmann::json;
constexpr std::size_t SPLAT_BYTES = 240;
struct encoder {
    std::vector<std::byte> bytes;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): binary encoding uses value, byte width.
    void integer(std::uint64_t value, std::uint8_t count = 8) {
        for (std::size_t i = 0; i < count; ++i) {
            bytes.push_back(static_cast<std::byte>(value & 255U));
            value >>= 8U;
        }
    }
    void real(double value) { integer(std::bit_cast<std::uint64_t>(value)); }
    void real(float value) { integer(std::bit_cast<std::uint32_t>(value), 4); }
    void text(std::string_view value) {
        for (char character : value)
            bytes.push_back(static_cast<std::byte>(character));
    }
};
struct decoder {
    std::span<const std::byte> bytes;
    std::size_t position{};
    std::uint64_t integer(std::uint8_t count = 8) {
        if (count > bytes.size() - position)
            throw std::runtime_error("truncated binary asset");
        std::uint64_t value{};
        for (std::size_t i = 0; i < count; ++i)
            value |= std::uint64_t(std::to_integer<unsigned char>(bytes[position++])) << (i * 8);
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
        if (length > bytes.size() - position)
            throw std::runtime_error("truncated binary text");
        std::string result;
        result.reserve(length);
        for (std::size_t i = 0; i < length; ++i)
            result.push_back(static_cast<char>(integer(1)));
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
    const auto path = root / name;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        throw std::runtime_error("symlink asset rejected");
    return path;
}
std::vector<std::byte> read_file(const std::filesystem::path& path, std::size_t maximum) {
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("asset is not a regular file");
    const auto size = std::filesystem::file_size(path);
    if (size > maximum ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("asset byte budget exceeded");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("asset read changed or failed");
    return bytes;
}
void write_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    static std::atomic<std::uint64_t> sequence{};
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    auto temporary = path;
    temporary += ".tmp." + std::to_string(process) + "." + std::to_string(sequence.fetch_add(1));
    std::FILE* file{};
#ifdef _WIN32
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0)
        file = nullptr;
#else
    file = std::fopen(temporary.c_str(), "wb");
#endif
    if (!file)
        throw std::runtime_error("cannot create staged asset");
    const bool written =
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && std::fflush(file) == 0;
#ifdef _WIN32
    const bool flushed = written && _commit(_fileno(file)) == 0;
#else
    const bool flushed = written && fsync(fileno(file)) == 0;
#endif
    const bool closed = std::fclose(file) == 0;
    if (!flushed || !closed) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("asset durable write failed");
    }
    try {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("manifest atomic replace failed");
#else
        std::filesystem::rename(temporary, path);
        const int directory = open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0)
            throw std::runtime_error("cannot open asset directory for durability");
        const int flushed_directory = fsync(directory);
        close(directory);
        if (flushed_directory != 0)
            throw std::runtime_error("asset directory flush failed");
#endif
    } catch (...) {
        std::filesystem::remove(temporary);
        throw;
    }
}
void put_transform(encoder& output, const similarity_transform& transform) {
    for (double value : {transform.rotation.w, transform.rotation.x, transform.rotation.y,
                         transform.rotation.z, transform.translation.x, transform.translation.y,
                         transform.translation.z, transform.scale})
        output.real(value);
}
similarity_transform get_transform(decoder& input) {
    similarity_transform transform;
    transform.rotation = {input.real64(), input.real64(), input.real64(), input.real64()};
    transform.translation = {input.real64(), input.real64(), input.real64()};
    transform.scale = input.real64();
    return transform;
}
encoder encode_chunk(const scene_chunk& chunk) {
    encoder output;
    output.text("WEXCHK01");
    output.integer(chunk.id.value);
    output.integer(chunk.generation);
    output.integer(chunk.pose_revision.value);
    output.integer(chunk.geometry_valid ? 1 : 0);
    put_transform(output, chunk.world_from_chunk);
    for (double value : {chunk.bounds.minimum.x, chunk.bounds.minimum.y, chunk.bounds.minimum.z,
                         chunk.bounds.maximum.x, chunk.bounds.maximum.y, chunk.bounds.maximum.z})
        output.real(value);
    output.integer(chunk.splats.size());
    output.integer(chunk.source_frames.size());
    for (auto frame : chunk.source_frames)
        output.integer(frame.value);
    for (const auto& splat : chunk.splats) {
        for (float value : splat.mean)
            output.real(value);
        for (float value : splat.scale)
            output.real(value);
        for (float value : splat.rotation)
            output.real(value);
        output.real(splat.opacity);
        for (float value : splat.sh)
            output.real(value);
        output.integer(splat.sh_degree, 4);
    }
    return output;
}
std::shared_ptr<const scene_chunk> decode_chunk(std::span<const std::byte> bytes,
                                                scene_limits limits) {
    decoder input{bytes};
    input.magic("WEXCHK01");
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {input.integer()};
    chunk->generation = input.integer();
    chunk->pose_revision = {input.integer()};
    const auto geometry = input.integer();
    if (geometry > 1)
        throw std::runtime_error("invalid geometry status");
    chunk->geometry_valid = geometry != 0;
    chunk->world_from_chunk = get_transform(input);
    chunk->bounds = {{input.real64(), input.real64(), input.real64()},
                     {input.real64(), input.real64(), input.real64()}};
    const auto splats = input.integer(), frames = input.integer();
    if (splats > limits.max_splats || splats > limits.max_payload_bytes / sizeof(gaussian) ||
        frames > limits.max_source_frames || frames > (bytes.size() - input.position) / 8)
        throw std::runtime_error("chunk element count exceeds budget");
    chunk->source_frames.reserve(static_cast<std::size_t>(frames));
    for (std::uint64_t i = 0; i < frames; ++i)
        chunk->source_frames.push_back({input.integer()});
    if (splats != (bytes.size() - input.position) / SPLAT_BYTES ||
        (bytes.size() - input.position) % SPLAT_BYTES != 0)
        throw std::runtime_error("chunk count does not match exact byte size");
    chunk->splats.resize(static_cast<std::size_t>(splats));
    for (auto& splat : chunk->splats) {
        for (float& value : splat.mean)
            value = input.real32();
        for (float& value : splat.scale)
            value = input.real32();
        for (float& value : splat.rotation)
            value = input.real32();
        splat.opacity = input.real32();
        for (float& value : splat.sh)
            value = input.real32();
        const auto degree = input.integer(4);
        if (degree > 3)
            throw std::runtime_error("unsupported SH degree");
        splat.sh_degree = static_cast<std::uint8_t>(degree);
    }
    auto checked = validate_chunk(*chunk, limits);
    if (!checked)
        throw std::runtime_error(checked.error().message);
    return chunk;
}
std::uint64_t unsigned_field(const json& object, const char* name) {
    const auto& field = object.at(name);
    if (!field.is_number_unsigned())
        throw std::runtime_error(std::string("invalid unsigned field: ") + name);
    return field.get<std::uint64_t>();
}
void expect(const json& object, const char* key, const char* value) {
    if (object.at(key) != value)
        throw std::runtime_error(std::string("unsupported convention: ") + key);
}
std::span<const std::byte> as_bytes(const std::string& text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}
bool bounded_line(std::istream& input, std::string& line, std::size_t limit) {
    line.clear();
    char character{};
    while (input.get(character)) {
        if (character == '\n')
            return true;
        if (line.size() == limit)
            throw std::runtime_error("PLY line exceeds length limit");
        if (character != '\r')
            line.push_back(character);
    }
    return !line.empty();
}
} // namespace
result<void> write_bundle(const std::filesystem::path& directory, const scene_snapshot& snapshot) {
    try {
        if (!snapshot)
            return fail(error_code::INVALID_ARGUMENT, "missing bundle snapshot");
        auto checked = validate_scene(*snapshot);
        if (!checked)
            return std::unexpected(checked.error());
        std::filesystem::create_directories(directory);
        encoder index;
        index.text("WEXIDX01");
        index.integer(snapshot->chunks.size());
        for (const auto& [id, chunk] : snapshot->chunks) {
            auto encoded = encode_chunk(*chunk);
            const auto hash = sha256(encoded.bytes);
            const auto name = "chunk_" + hash + ".bin";
            write_atomic(asset_path(directory, name), encoded.bytes);
            index.integer(id.value);
            index.integer(encoded.bytes.size());
            index.text(hash);
            index.integer(name.size(), 4);
            index.text(name);
        }
        const auto index_hash = sha256(index.bytes);
        const auto index_name = "index_" + index_hash + ".bin";
        write_atomic(asset_path(directory, index_name), index.bytes);
        const auto provenance = json{
            {"schema_version", 1},
            {"producer", "world_explorer"},
            {"metadata", snapshot->provenance},
            {"training_checkpoint",
             false}}.dump(2);
        const auto provenance_hash = sha256(as_bytes(provenance));
        const auto provenance_name = "provenance_" + provenance_hash + ".json";
        write_atomic(asset_path(directory, provenance_name), as_bytes(provenance));
        json manifest = {
            {"schema_version", 1},
            {"scene_id", snapshot->id.value},
            {"epoch", snapshot->epoch.value},
            {"revision", snapshot->revision.value},
            {"coordinates",
             {{"handedness", "right"},
              {"world_up", "+y"},
              {"camera_axes", "+x_right,+y_down,+z_forward"},
              {"pose_convention", "world_from_camera"},
              {"quaternion_order", "wxyz"},
              {"pixel_centers", "integer"},
              {"scale_status",
               snapshot->scale == scale_status::ARBITRARY ? "ARBITRARY" : "METRIC_MEASURED"}}},
            {"appearance",
             {{"representation", "gaussian_3d"},
              {"lighting", "captured"},
              {"color_domain", "srgb"},
              {"sh_basis", "real_3dgs_coefficient_major_rgb"}}},
            {"geometry", {{"collision_status", "UNAVAILABLE"}}},
            {"compression", "none"},
            {"index", index_name},
            {"index_sha256", index_hash},
            {"index_bytes", index.bytes.size()},
            {"provenance", provenance_name},
            {"provenance_sha256", provenance_hash}};
        const auto text = manifest.dump(2);
        write_atomic(asset_path(directory, "manifest.json"), as_bytes(text));
        return {};
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "bundle allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::IO_ERROR, exception.what());
    }
}
result<scene_snapshot> load_bundle(const std::filesystem::path& directory, bundle_limits limits) {
    try {
        const auto bytes =
            read_file(asset_path(directory, "manifest.json"), limits.max_manifest_bytes);
        const auto manifest = json::parse(bytes.begin(), bytes.end());
        if (unsigned_field(manifest, "schema_version") != 1)
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "unsupported scene schema major version");
        const auto& coordinates = manifest.at("coordinates");
        expect(coordinates, "handedness", "right");
        expect(coordinates, "world_up", "+y");
        expect(coordinates, "camera_axes", "+x_right,+y_down,+z_forward");
        expect(coordinates, "pose_convention", "world_from_camera");
        expect(coordinates, "quaternion_order", "wxyz");
        expect(coordinates, "pixel_centers", "integer");
        expect(manifest.at("appearance"), "representation", "gaussian_3d");
        expect(manifest.at("appearance"), "lighting", "captured");
        expect(manifest.at("appearance"), "color_domain", "srgb");
        expect(manifest.at("appearance"), "sh_basis", "real_3dgs_coefficient_major_rgb");
        expect(manifest, "compression", "none");
        auto state = std::make_shared<scene_state>();
        state->id = {unsigned_field(manifest, "scene_id")};
        state->epoch = {unsigned_field(manifest, "epoch")};
        state->revision = {unsigned_field(manifest, "revision")};
        const auto scale = coordinates.at("scale_status").get<std::string>();
        if (scale == "METRIC_MEASURED")
            state->scale = scale_status::METRIC_MEASURED;
        else if (scale != "ARBITRARY")
            throw std::runtime_error("unsupported scale status");
        const auto index_hash = manifest.at("index_sha256").get<std::string>();
        if (!hash_valid(index_hash))
            throw std::runtime_error("invalid index hash");
        if (limits.scene.max_chunks > (std::numeric_limits<std::size_t>::max() - 16) / 256)
            throw std::runtime_error("invalid chunk limit");
        const auto index_bytes =
            read_file(asset_path(directory, manifest.at("index").get<std::string>()),
                      16 + limits.scene.max_chunks * 256);
        if (index_bytes.size() != unsigned_field(manifest, "index_bytes") ||
            sha256(index_bytes) != index_hash)
            throw std::runtime_error("index checksum/size mismatch");
        const auto provenance_hash = manifest.at("provenance_sha256").get<std::string>();
        const auto provenance =
            read_file(asset_path(directory, manifest.at("provenance").get<std::string>()),
                      limits.max_manifest_bytes);
        if (!hash_valid(provenance_hash) || sha256(provenance) != provenance_hash)
            throw std::runtime_error("provenance checksum mismatch");
        const auto provenance_value = json::parse(provenance.begin(), provenance.end());
        if (provenance_value.at("schema_version") != 1)
            throw std::runtime_error("unsupported provenance schema");
        if (provenance_value.contains("metadata"))
            state->provenance =
                provenance_value.at("metadata").get<std::map<std::string, std::string>>();
        decoder index{index_bytes};
        index.magic("WEXIDX01");
        const auto count = index.integer();
        if (count > limits.scene.max_chunks)
            throw std::runtime_error("index chunk count exceeds budget");
        std::size_t total_bytes{}, total_splats{};
        for (std::uint64_t i = 0; i < count; ++i) {
            const chunk_id id{index.integer()};
            const auto size = index.integer();
            const auto hash = index.text(64);
            const auto name = index.text(static_cast<std::size_t>(index.integer(4)));
            if (!hash_valid(hash) || size > limits.scene.max_payload_bytes - total_bytes)
                throw std::runtime_error("asset size/hash exceeds budget");
            auto chunk_bytes =
                read_file(asset_path(directory, name), static_cast<std::size_t>(size));
            if (chunk_bytes.size() != size || sha256(chunk_bytes) != hash)
                throw std::runtime_error("chunk checksum/size mismatch");
            auto chunk = decode_chunk(chunk_bytes, limits.scene);
            if (chunk->id != id || !state->chunks.emplace(id, chunk).second)
                throw std::runtime_error("duplicate/mismatched chunk ID");
            total_bytes += chunk_bytes.size();
            if (chunk->splats.size() > limits.scene.max_splats - total_splats)
                throw std::runtime_error("aggregate splat count exceeds budget");
            total_splats += chunk->splats.size();
        }
        if (index.position != index.bytes.size())
            throw std::runtime_error("trailing index bytes");
        auto checked = validate_scene(*state, limits.scene);
        if (!checked)
            return std::unexpected(checked.error());
        return scene_snapshot{std::move(state)};
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "bundle allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
result<scene_snapshot> load_gsplat_ply(const std::filesystem::path& path, scene_id id,
                                       bundle_limits limits) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return fail(error_code::IO_ERROR, "cannot open PLY input");
        std::string line;
        std::size_t header_bytes{}, count{};
        bool ascii{}, exporter{}, color{}, vertices{}, header_done{};
        std::vector<std::string> properties;
        if (!bounded_line(input, line, limits.max_ply_header_bytes) || line != "ply")
            throw std::runtime_error("missing PLY magic");
        while (bounded_line(input, line, limits.max_ply_header_bytes)) {
            header_bytes += line.size() + 1;
            if (header_bytes > limits.max_ply_header_bytes)
                throw std::runtime_error("PLY header exceeds budget");
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
            if (line.starts_with("comment "))
                continue;
            if (line.starts_with("element vertex ") && !vertices) {
                const auto text = std::string_view(line).substr(15);
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), count);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                    count == 0 || count > limits.scene.max_splats ||
                    count > limits.scene.max_payload_bytes / sizeof(gaussian))
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
        for (std::size_t i = 0; i < properties.size(); ++i)
            if (!columns.emplace(properties[i], i).second)
                throw std::runtime_error("duplicate PLY property");
        for (const auto* name : {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0",
                                 "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"})
            if (!columns.contains(name))
                throw std::runtime_error(std::string("missing PLY property ") + name);
        std::size_t rest{};
        while (columns.contains("f_rest_" + std::to_string(rest)))
            ++rest;
        std::uint8_t degree{};
        if (rest == 9)
            degree = 1;
        else if (rest == 24)
            degree = 2;
        else if (rest == 45)
            degree = 3;
        else if (rest != 0)
            throw std::runtime_error("unsupported/incomplete SH layout");
        for (const auto& [name, column] : columns) {
            (void)column;
            if (name == "x" || name == "y" || name == "z" || name == "nx" || name == "ny" ||
                name == "nz" || name == "opacity")
                continue;
            bool known = false;
            for (const auto& [prefix, maximum] : std::array<std::pair<std::string, std::size_t>, 4>{
                     {{"f_dc_", 3}, {"f_rest_", rest}, {"scale_", 3}, {"rot_", 4}}})
                for (std::size_t i = 0; i < maximum; ++i)
                    if (name == prefix + std::to_string(i))
                        known = true;
            if (!known)
                throw std::runtime_error("unknown/ambiguous PLY property");
        }
        auto chunk = std::make_shared<scene_chunk>();
        chunk->id = {1};
        chunk->pose_revision = {1};
        chunk->world_from_chunk.rotation = {0, 1, 0, 0};
        chunk->splats.reserve(count);
        chunk->bounds.minimum = {std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::infinity()};
        chunk->bounds.maximum = chunk->bounds.minimum * (-1);
        for (std::size_t row = 0; row < count; ++row) {
            if (!bounded_line(input, line, 16384))
                throw std::runtime_error("truncated PLY vertices");
            std::istringstream values(line);
            values.imbue(std::locale::classic());
            std::vector<double> fields;
            fields.reserve(properties.size());
            for (std::size_t column = 0; column < properties.size(); ++column) {
                double value{};
                if (!(values >> value) || !std::isfinite(value))
                    throw std::runtime_error("nonfinite/invalid PLY numeric value");
                fields.push_back(value);
            }
            std::string extra;
            if (values >> extra)
                throw std::runtime_error("extra PLY vertex columns");
            const auto get = [&](const std::string& name) { return fields[columns.at(name)]; };
            gaussian splat;
            splat.sh_degree = degree;
            splat.mean = {static_cast<float>(get("x")), static_cast<float>(get("y")),
                          static_cast<float>(get("z"))};
            double length{};
            for (std::size_t axis = 0; axis < 4; ++axis) {
                const auto value = get("rot_" + std::to_string(axis));
                length += value * value;
            }
            if (!std::isfinite(length) || length < 1e-30)
                throw std::runtime_error("degenerate PLY quaternion");
            for (std::size_t axis = 0; axis < 4; ++axis)
                splat.rotation[axis] =
                    static_cast<float>(get("rot_" + std::to_string(axis)) / std::sqrt(length));
            for (std::size_t axis = 0; axis < 3; ++axis) {
                splat.scale[axis] =
                    static_cast<float>(std::exp(get("scale_" + std::to_string(axis))));
                splat.sh[axis] = static_cast<float>(get("f_dc_" + std::to_string(axis)));
                for (std::size_t coefficient = 0; coefficient < rest / 3; ++coefficient)
                    splat.sh[(coefficient + 1) * 3 + axis] = static_cast<float>(
                        get("f_rest_" + std::to_string(axis * (rest / 3) + coefficient)));
            }
            const auto logit = get("opacity");
            splat.opacity = static_cast<float>(
                logit >= 0 ? 1 / (1 + std::exp(-logit)) : std::exp(logit) / (1 + std::exp(logit)));
            // Bounds cover the 3-sigma support, not only Gaussian centers.
            const double radius = 3 * std::max({splat.scale[0], splat.scale[1], splat.scale[2]});
            chunk->bounds.minimum = {std::min(chunk->bounds.minimum.x, splat.mean[0] - radius),
                                     std::min(chunk->bounds.minimum.y, splat.mean[1] - radius),
                                     std::min(chunk->bounds.minimum.z, splat.mean[2] - radius)};
            chunk->bounds.maximum = {std::max(chunk->bounds.maximum.x, splat.mean[0] + radius),
                                     std::max(chunk->bounds.maximum.y, splat.mean[1] + radius),
                                     std::max(chunk->bounds.maximum.z, splat.mean[2] + radius)};
            chunk->splats.push_back(splat);
        }
        while (bounded_line(input, line, 16384))
            if (line.find_first_not_of(" \t\r") != std::string::npos)
                throw std::runtime_error("trailing PLY data");
        auto state = std::make_shared<scene_state>();
        state->id = id;
        state->revision = {1};
        state->chunks.emplace(chunk->id, std::move(chunk));
        auto checked = validate_scene(*state, limits.scene);
        if (!checked)
            return std::unexpected(checked.error());
        return scene_snapshot{std::move(state)};
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "PLY allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
} // namespace world_explorer
