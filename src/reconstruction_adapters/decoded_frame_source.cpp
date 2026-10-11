#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace world_explorer {
namespace {
constexpr std::size_t MAX_MANIFEST_BYTES = std::size_t{1024} * 1024;
constexpr std::size_t MAX_ENCODED_BYTES = std::size_t{64} * 1024 * 1024;
constexpr std::int64_t MAX_CAPTURE_NS = 1200LL * 1000000000;
bool digest_valid(std::string_view value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos;
}
std::string bounded_file(const std::filesystem::path& path) {
    const auto status = std::filesystem::symlink_status(path);
    if (!std::filesystem::is_regular_file(status) ||
        std::filesystem::file_size(path) > MAX_MANIFEST_BYTES)
        throw std::invalid_argument("Missing, linked or oversized decode manifest");
    std::ifstream file(path, std::ios::binary);
    std::string bytes(MAX_MANIFEST_BYTES + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = static_cast<std::size_t>(file.gcount());
    if (file.bad() || !file.eof() || count > MAX_MANIFEST_BYTES)
        throw std::invalid_argument("Decode manifest read failed or exceeded its budget");
    bytes.resize(count);
    return bytes;
}
nlohmann::json parse(const std::string& bytes) {
    return nlohmann::json::parse(
        bytes, [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
            if (depth > 32)
                throw std::invalid_argument("Decode manifest nesting exceeded");
            return true;
        });
}
std::uint64_t unsigned_value(const nlohmann::json& value) {
    if (!value.is_number_unsigned())
        throw std::invalid_argument("Expected unsigned decode metadata");
    return value.get<std::uint64_t>();
}
std::filesystem::path contained(const std::filesystem::path& root, const std::string& name) {
    if (name.empty() || name.size() > 4096 || name.find('\0') != std::string::npos ||
        name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        throw std::invalid_argument("Invalid decoded asset path");
    const std::filesystem::path relative(std::u8string(name.begin(), name.end()));
    if (relative.is_absolute() || relative.has_root_name())
        throw std::invalid_argument("Decoded asset path must be relative");
    for (const auto& part : relative)
        if (part == "." || part == "..")
            throw std::invalid_argument("Unsafe decoded asset path");
    const auto path = std::filesystem::canonical(root / relative);
    if (path.lexically_relative(root) != relative ||
        !std::filesystem::is_regular_file(std::filesystem::symlink_status(root / relative)))
        throw std::invalid_argument("Decoded image escapes its stage or uses a symlink");
    return path;
}
std::uint64_t identity(const std::string& source, std::string_view kind,
                       std::string_view external) {
    const auto input = "world_explorer.observation.v1/" + source + "/" + std::string(kind) + "/" +
                       std::string(external);
    const auto digest = sha256(std::as_bytes(std::span(input.data(), input.size())));
    std::uint64_t value{};
    const auto parsed = std::from_chars(digest.data(), digest.data() + 16, value, 16);
    if (parsed.ec != std::errc{} || value == 0)
        throw std::invalid_argument("Invalid observation identity");
    return value;
}
source_timestamp timestamp(std::int64_t pts, const std::string& time_base) {
    source_timestamp value{pts};
    const auto slash = time_base.find('/');
    if (slash == time_base.npos || time_base.size() > 32)
        throw std::invalid_argument("Invalid original timestamp time base");
    const auto numerator =
        std::from_chars(time_base.data(), time_base.data() + slash, value.time_base_numerator);
    const auto denominator =
        std::from_chars(time_base.data() + slash + 1, time_base.data() + time_base.size(),
                        value.time_base_denominator);
    if (numerator.ec != std::errc{} || numerator.ptr != time_base.data() + slash ||
        denominator.ec != std::errc{} || denominator.ptr != time_base.data() + time_base.size() ||
        value.time_base_numerator <= 0 || value.time_base_denominator <= 0)
        throw std::invalid_argument("Invalid original timestamp time base");
    return value;
}
std::int64_t nanoseconds(source_timestamp value) {
    const auto factor = static_cast<std::int64_t>(value.time_base_numerator) * 1000000000;
    if (value.pts < 0 || value.time_base_numerator <= 0 || value.time_base_denominator <= 0 ||
        value.pts > std::numeric_limits<std::int64_t>::max() / factor)
        throw std::invalid_argument("Negative or overflowing offline timestamp");
    const auto result = value.pts * factor / value.time_base_denominator;
    if (result > MAX_CAPTURE_NS)
        throw std::invalid_argument("Offline timestamp exceeds the 1200-second decode profile");
    return result;
}
std::pair<std::uint32_t, std::uint32_t> png_dimensions(const std::filesystem::path& path) {
    std::array<unsigned char, 24> header{};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    constexpr std::array<unsigned char, 16> SIGNATURE{137, 80, 78, 71, 13,  10,  26,  10,
                                                      0,   0,  0,  13, 'I', 'H', 'D', 'R'};
    if (file.gcount() != static_cast<std::streamsize>(header.size()) ||
        !std::equal(SIGNATURE.begin(), SIGNATURE.end(), header.begin()))
        throw std::invalid_argument("Invalid decoded PNG header");
    const auto number = [&](std::size_t offset) {
        std::uint32_t value{};
        for (std::size_t index = offset; index < offset + 4; ++index)
            value = (value << 8U) | header[index];
        return value;
    };
    return {number(16), number(20)};
}
struct decoded_frame {
    frame_id id;
    calibration_id calibration;
    source_timestamp original;
    std::chrono::nanoseconds capture_time;
    std::filesystem::path path;
    std::string asset_name, digest;
    std::size_t encoded_bytes{}, image_bytes{};
    std::uint32_t width{}, height{};
};
struct decoded_source_state {
    decoded_source_config config;
    std::filesystem::path root;
    std::string completion_digest;
    capture_id capture;
    std::vector<decoded_frame> frames;
    std::size_t position{};
    world_epoch epoch;
    bool reset{true};
    result<void> unchanged(const std::stop_token& stop) const {
        if (!std::filesystem::is_regular_file(
                std::filesystem::symlink_status(root / "completion.json")))
            return fail(error_code::INVALID_DATA, "Decode completion is missing or linked");
        const auto digest = sha256_file(root / "completion.json", MAX_MANIFEST_BYTES, stop);
        if (!digest)
            return std::unexpected(digest.error());
        if (*digest != completion_digest)
            return fail(error_code::INVALID_DATA, "Decode completion changed during replay");
        return {};
    }
};
class decoded_frame_source final : public frame_source {
  public:
    explicit decoded_frame_source(std::unique_ptr<decoded_source_state> state)
        : state_(std::move(state)) {}
    source_capabilities capabilities() const noexcept override;
    result<source_event> read_next(std::stop_token stop) override;
    result<void> seek(source_timestamp target) override;

  private:
    std::unique_ptr<decoded_source_state> state_;
};
} // namespace
result<std::unique_ptr<frame_source>> open_decoded_frame_source(decoded_source_config config,
                                                                const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Opening decoded source cancelled");
        if (!digest_valid(config.source_sha256) || config.decoder_arguments.empty() ||
            config.max_image_bytes.bytes == 0 || config.max_image_bytes.bytes > MAX_ENCODED_BYTES)
            return fail(error_code::INVALID_ARGUMENT, "Invalid decoded source configuration");
        auto state = std::make_unique<decoded_source_state>();
        state->config = std::move(config);
        state->root = std::filesystem::canonical(state->config.stage_directory);
        const auto completion_bytes = bounded_file(state->root / "completion.json");
        const auto completion = parse(completion_bytes);
        const auto key = completion.at("key").get<std::string>();
        if (completion.at("schema_version") != 1 || completion.at("status") != "complete" ||
            !digest_valid(key) || state->root.filename() != "decode-" + key)
            return fail(error_code::INVALID_DATA, "Not a committed decode stage");
        const auto& inventory = completion.at("files");
        if (!inventory.is_array() || inventory.empty() || inventory.size() > 4096)
            return fail(error_code::RESOURCE_EXHAUSTED, "Invalid decode inventory count");
        std::map<std::string, std::pair<std::uint64_t, std::string>> assets;
        for (const auto& entry : inventory) {
            const auto name = entry.at("path").get<std::string>();
            const auto bytes = unsigned_value(entry.at("bytes"));
            const auto digest = entry.at("sha256").get<std::string>();
            if (!digest_valid(digest) || !assets.emplace(name, std::pair{bytes, digest}).second)
                return fail(error_code::INVALID_DATA,
                            "Duplicate or invalid decode inventory entry");
        }
        const auto result_bytes = bounded_file(state->root / "result.json");
        const auto manifest_entry = assets.find("result.json");
        if (manifest_entry == assets.end() || manifest_entry->second.first != result_bytes.size() ||
            manifest_entry->second.second !=
                sha256(std::as_bytes(std::span(result_bytes.data(), result_bytes.size()))))
            return fail(error_code::INVALID_DATA, "Decode metadata checksum/size mismatch");
        const auto metadata = parse(result_bytes);
        if (metadata.at("selection") != "timestamp_sampling_v1" || metadata.at("crop") != "none" ||
            metadata.at("resize") != "none")
            return fail(error_code::UNSUPPORTED_OPERATION, "Unsupported decode replay profile");
        const auto source_count = unsigned_value(metadata.at("source_frame_count"));
        const auto& frames = metadata.at("frames");
        if (source_count == 0 || source_count > 36000 || !frames.is_array() || frames.empty() ||
            frames.size() > 120)
            return fail(error_code::RESOURCE_EXHAUSTED, "Decode frame count exceeds profile");
        std::set<std::uint64_t> frame_ids;
        std::set<std::string> names;
        std::map<std::uint64_t, std::string> calibrations;
        std::map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> dimensions;
        std::uint64_t previous_index{};
        state->capture = capture_id{identity(state->config.source_sha256, "capture", "")};
        for (const auto& entry : frames) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Opening decoded source cancelled");
            const auto index = unsigned_value(entry.at("source_index"));
            const auto external_frame = entry.at("frame_id").get<std::string>();
            const auto external_calibration = entry.at("calibration_id").get<std::string>();
            const auto prefix = state->config.source_sha256.substr(0, 16);
            if (index >= source_count || (!state->frames.empty() && index <= previous_index) ||
                external_frame != prefix + "-" + std::to_string(index) ||
                external_calibration != prefix + "-lens-0")
                return fail(error_code::INVALID_DATA, "Invalid source/frame/calibration identity");
            previous_index = index;
            decoded_frame frame;
            frame.id = frame_id{identity(state->config.source_sha256, "frame", external_frame)};
            frame.calibration = calibration_id{
                identity(state->config.source_sha256, "calibration", external_calibration)};
            if (!frame_ids.insert(frame.id.value).second)
                return fail(error_code::INVALID_DATA, "Duplicate or colliding frame identity");
            const auto [calibration, inserted] =
                calibrations.emplace(frame.calibration.value, external_calibration);
            if (!inserted && calibration->second != external_calibration)
                return fail(error_code::INVALID_DATA, "Colliding calibration identity");
            const auto pts = unsigned_value(entry.at("pts"));
            if (pts > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return fail(error_code::INVALID_DATA, "Original PTS exceeds int64");
            frame.original =
                timestamp(static_cast<std::int64_t>(pts), entry.at("time_base").get<std::string>());
            const auto capture_time = unsigned_value(entry.at("capture_time_ns"));
            const auto expected_time = nanoseconds(frame.original);
            if (capture_time != static_cast<std::uint64_t>(expected_time) ||
                (!state->frames.empty() &&
                 expected_time <= state->frames.back().capture_time.count()))
                return fail(error_code::INVALID_DATA, "Inconsistent or nonmonotonic capture time");
            frame.capture_time = std::chrono::nanoseconds(expected_time);
            const auto name = entry.at("file").get<std::string>();
            frame.asset_name = name;
            frame.path = contained(state->root, name);
            frame.digest = entry.at("sha256").get<std::string>();
            const auto asset = assets.find(name);
            if (!names.insert(name).second || frame.path.extension() != ".png" ||
                asset == assets.end() || asset->second.second != frame.digest ||
                asset->second.first == 0 || asset->second.first > MAX_ENCODED_BYTES ||
                std::filesystem::file_size(frame.path) != asset->second.first)
                return fail(error_code::INVALID_DATA, "Invalid decoded image inventory");
            frame.encoded_bytes = static_cast<std::size_t>(asset->second.first);
            const auto [width, height] = png_dimensions(frame.path);
            if (width == 0 || height == 0 || width > 8192 || height > 8192 ||
                static_cast<std::uint64_t>(width) * height > 4096ULL * 4096 ||
                static_cast<std::uint64_t>(width) * height * 3 >
                    state->config.max_image_bytes.bytes)
                return fail(error_code::RESOURCE_EXHAUSTED, "Decoded RGB image exceeds budget");
            const auto [size, new_calibration] =
                dimensions.emplace(frame.calibration.value, std::pair{width, height});
            if (!new_calibration && size->second != std::pair{width, height})
                return fail(error_code::INVALID_DATA,
                            "Image dimensions changed without calibration");
            frame.width = width;
            frame.height = height;
            frame.image_bytes = static_cast<std::size_t>(width) * height * 3;
            state->frames.push_back(std::move(frame));
        }
        state->completion_digest =
            sha256(std::as_bytes(std::span(completion_bytes.data(), completion_bytes.size())));
        const auto unchanged = state->unchanged(stop);
        if (!unchanged)
            return std::unexpected(unchanged.error());
        return std::unique_ptr<frame_source>(
            std::make_unique<decoded_frame_source>(std::move(state)));
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Invalid decoded source: ") + exception.what());
    }
}
source_capabilities decoded_frame_source::capabilities() const noexcept {
    return {true, false, false, state_->config.max_image_bytes};
}
result<source_event> decoded_frame_source::read_next(std::stop_token stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Frame replay cancelled");
        const auto unchanged = state_->unchanged(stop);
        if (!unchanged)
            return std::unexpected(unchanged.error());
        if (state_->reset) {
            state_->reset = false;
            return source_reset{state_->capture, state_->epoch};
        }
        if (state_->position == state_->frames.size())
            return source_end{};
        const auto& frame = state_->frames[state_->position];
        const auto image = contained(state_->root, frame.asset_name);
        const auto initial_hash = sha256_file(image, frame.encoded_bytes, stop);
        if (!initial_hash)
            return std::unexpected(initial_hash.error());
        if (*initial_hash != frame.digest ||
            std::filesystem::file_size(image) != frame.encoded_bytes)
            return fail(error_code::INVALID_DATA, "Decoded image checksum/size changed");
        auto command = state_->config.decoder_arguments;
        const auto image_utf8 = image.u8string();
        const std::vector<std::string> arguments{"-nostdin",
                                                 "-hide_banner",
                                                 "-loglevel",
                                                 "error",
                                                 "-max_alloc",
                                                 "67108864",
                                                 "-noautorotate",
                                                 "-threads",
                                                 "1",
                                                 "-i",
                                                 std::string(image_utf8.begin(), image_utf8.end()),
                                                 "-frames:v",
                                                 "1",
                                                 "-threads",
                                                 "1",
                                                 "-f",
                                                 "rawvideo",
                                                 "-pix_fmt",
                                                 "rgb24",
                                                 "pipe:1"};
        command.insert(command.end(), arguments.begin(), arguments.end());
        auto pixels = std::make_shared<std::vector<std::byte>>();
        pixels->reserve(frame.image_bytes);
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] { cancellation.request_stop(); });
        bool oversized{};
        std::string diagnostic;
        const auto decoded = execute_process(
            command, cancellation.get_token(),
            [&](const std::string& output) {
                if (output.size() > frame.image_bytes - pixels->size()) {
                    oversized = true;
                    cancellation.request_stop();
                    return;
                }
                const auto bytes = std::as_bytes(std::span(output.data(), output.size()));
                pixels->insert(pixels->end(), bytes.begin(), bytes.end());
            },
            [&](const std::string& output) {
                diagnostic.append(output, 0,
                                  std::min(output.size(), std::size_t{4096} - diagnostic.size()));
            });
        if (oversized)
            return fail(error_code::RESOURCE_EXHAUSTED, "Decoder output exceeds image budget");
        if (!decoded)
            return std::unexpected(decoded.error());
        if (*decoded != 0 || pixels->size() != frame.image_bytes)
            return fail(error_code::BACKEND_FAILURE,
                        "Image decoder failed or returned short RGB output: " + diagnostic);
        const auto final_hash = sha256_file(image, frame.encoded_bytes, stop);
        if (!final_hash)
            return std::unexpected(final_hash.error());
        if (*final_hash != frame.digest)
            return fail(error_code::INVALID_DATA, "Decoded image changed during pixel read");
        frame_packet packet{
            frame.id,
            state_->capture,
            frame.capture_time,
            frame.original,
            frame.calibration,
            {std::move(pixels), frame.width, frame.height, frame.width * 3, pixel_format::RGB8},
            {},
            {}};
        ++state_->position;
        return packet;
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Frame replay failed: ") + exception.what());
    }
}
result<void> decoded_frame_source::seek(source_timestamp target) {
    try {
        const auto time = std::chrono::nanoseconds(nanoseconds(target));
        const auto unchanged = state_->unchanged({});
        if (!unchanged)
            return std::unexpected(unchanged.error());
        if (state_->epoch.value == std::numeric_limits<std::uint64_t>::max())
            return fail(error_code::RESOURCE_EXHAUSTED, "Source reset epoch exhausted");
        const auto found =
            std::ranges::lower_bound(state_->frames, time, {}, &decoded_frame::capture_time);
        state_->position = static_cast<std::size_t>(found - state_->frames.begin());
        ++state_->epoch.value;
        state_->reset = true;
        return {};
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_ARGUMENT, std::string("Invalid seek: ") + exception.what());
    }
}
} // namespace world_explorer
