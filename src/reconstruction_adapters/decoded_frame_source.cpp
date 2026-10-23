#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include "observation_identity.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>

namespace world_explorer {
namespace {

constexpr std::size_t K_MAX_MANIFEST_BYTES { 1024uz * 1024uz };
constexpr std::size_t K_MAX_ENCODED_BYTES { 64uz * 1024uz * 1024uz };
constexpr std::int64_t K_MAX_CAPTURE_NS { 1200LL * 1000000000 };

bool digest_valid(std::string_view value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos;
}

std::string bounded_file(const std::filesystem::path& path) {
    const auto K_STATUS = std::filesystem::symlink_status(path);
    if (!std::filesystem::is_regular_file(K_STATUS) ||
        std::filesystem::file_size(path) > K_MAX_MANIFEST_BYTES) {
        throw std::invalid_argument("Missing, linked or oversized decode manifest");
    }
    std::ifstream file(path, std::ios::binary);
    std::string bytes(K_MAX_MANIFEST_BYTES + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto K_COUNT = static_cast<std::size_t>(file.gcount());
    if (file.bad() || !file.eof() || K_COUNT > K_MAX_MANIFEST_BYTES) {
        throw std::invalid_argument("Decode manifest read failed or exceeded its budget");
    }
    bytes.resize(K_COUNT);
    return bytes;
}

nlohmann::json parse(const std::string& bytes) {
    return nlohmann::json::parse(
        bytes, [](int depth, nlohmann::json::parse_event_t, const nlohmann::json&) {
            if (depth > 32) {
                throw std::invalid_argument("Decode manifest nesting exceeded");
            }
            return true;
        });
}

std::uint64_t unsigned_value(const nlohmann::json& value) {
    if (!value.is_number_unsigned()) {
        throw std::invalid_argument("Expected unsigned decode metadata");
    }
    return value.get<std::uint64_t>();
}

std::filesystem::path contained(const std::filesystem::path& root, const std::string& name) {
    if (name.empty() || name.size() > 4096 || name.find('\0') != std::string::npos ||
        name.find('\\') != std::string::npos || name.find(':') != std::string::npos) {
        throw std::invalid_argument("Invalid decoded asset path");
    }
    const std::filesystem::path K_RELATIVE(std::u8string(name.begin(), name.end()));
    if (K_RELATIVE.is_absolute() || K_RELATIVE.has_root_name()) {
        throw std::invalid_argument("Decoded asset path must be relative");
    }
    for (const auto& part : K_RELATIVE) {
        if (part == "." || part == "..") {
            throw std::invalid_argument("Unsafe decoded asset path");
        }
    }
    const auto K_PATH = std::filesystem::canonical(root / K_RELATIVE);
    if (K_PATH.lexically_relative(root) != K_RELATIVE ||
        !std::filesystem::is_regular_file(std::filesystem::symlink_status(root / K_RELATIVE))) {
        throw std::invalid_argument("Decoded image escapes its stage or uses a symlink");
    }
    return K_PATH;
}

SourceTimestamp timestamp(std::int64_t pts, const std::string& time_base) {
    SourceTimestamp value { pts };
    const auto K_SLASH = time_base.find('/');
    if (K_SLASH == time_base.npos || time_base.size() > 32) {
        throw std::invalid_argument("Invalid original timestamp time base");
    }
    const auto K_NUMERATOR =
        std::from_chars(time_base.data(), time_base.data() + K_SLASH, value.m_time_base_numerator);
    const auto K_DENOMINATOR = std::from_chars(time_base.data() + K_SLASH + 1,
                                               time_base.data() + time_base.size(),
                                               value.m_time_base_denominator);
    if (K_NUMERATOR.ec != std::errc {} || K_NUMERATOR.ptr != time_base.data() + K_SLASH ||
        K_DENOMINATOR.ec != std::errc {} ||
        K_DENOMINATOR.ptr != time_base.data() + time_base.size() ||
        value.m_time_base_numerator <= 0 || value.m_time_base_denominator <= 0) {
        throw std::invalid_argument("Invalid original timestamp time base");
    }
    return value;
}

std::int64_t nanoseconds(SourceTimestamp value) {
    const auto K_FACTOR = static_cast<std::int64_t>(value.m_time_base_numerator) * 1000000000;
    if (value.m_pts < 0 || value.m_time_base_numerator <= 0 || value.m_time_base_denominator <= 0 ||
        value.m_pts > std::numeric_limits<std::int64_t>::max() / K_FACTOR) {
        throw std::invalid_argument("Negative or overflowing offline timestamp");
    }
    const auto K_RESULT = value.m_pts * K_FACTOR / value.m_time_base_denominator;
    if (K_RESULT > K_MAX_CAPTURE_NS) {
        throw std::invalid_argument("Offline timestamp exceeds the 1200-second decode profile");
    }
    return K_RESULT;
}

double quality_number(const nlohmann::json& value, double minimum, double maximum) {
    if (!value.is_number()) {
        throw std::invalid_argument("Expected numeric quality metric");
    }
    const auto K_VALUE = value.get<double>();
    if (!std::isfinite(K_VALUE) || K_VALUE < minimum || K_VALUE > maximum) {
        throw std::invalid_argument("Quality metric exceeds its finite range");
    }
    return K_VALUE;
}

std::pair<std::uint32_t, std::uint32_t> png_dimensions(const std::filesystem::path& path);

Result<void>
validate_quality(const std::filesystem::path& root,
                 const nlohmann::json& metadata,
                 const std::map<std::string, std::pair<std::uint64_t, std::string>>& assets,
                 const std::string& source,
                 const std::stop_token& stop) {
    constexpr std::string_view K_POLICY_SHA {
        "cdf0f819989e98fbcc222ee3187a8d8a8d11d967c60003d4b35ddfb2047c39f1" };
    if (metadata.at("source_sha256") != source || metadata.at("quality_ledger") != "quality.json") {
        throw std::invalid_argument("Quality source/ledger identity mismatch");
    }
    const auto K_BYTES = bounded_file(contained(root, "quality.json"));
    const auto K_DIGEST = sha256(std::as_bytes(std::span(K_BYTES)));
    const auto K_ENTRY = assets.find("quality.json");
    if (K_ENTRY == assets.end() || K_ENTRY->second != std::pair { K_BYTES.size(), K_DIGEST } ||
        metadata.at("quality_ledger_sha256") != K_DIGEST) {
        throw std::invalid_argument("Quality ledger checksum/size mismatch");
    }
    const auto K_LEDGER = parse(K_BYTES);
    const auto K_POLICY = K_LEDGER.at("policy").dump();
    if (K_LEDGER.at("schema_version") != 1 || K_LEDGER.at("source_sha256") != source ||
        K_LEDGER.at("profile") != "quality_keyframes_candidate_v1" ||
        K_LEDGER.at("policy_sha256") != K_POLICY_SHA ||
        sha256(std::as_bytes(std::span(K_POLICY))) != K_POLICY_SHA ||
        !digest_valid(K_LEDGER.at("implementation_sha256").get<std::string>())) {
        throw std::invalid_argument("Unsupported quality policy/provenance");
    }
    const auto& decisions = K_LEDGER.at("decisions");
    const auto K_LIMIT = unsigned_value(K_LEDGER.at("candidate_limit"));
    if (!decisions.is_array() || decisions.size() < 3uz || decisions.size() > 480uz ||
        decisions.size() > K_LIMIT || K_LIMIT > 480 || metadata.at("frames").size() < 3uz) {
        throw std::invalid_argument("Quality candidate count exceeds profile");
    }
    const auto K_FPS = quality_number(K_LEDGER.at("sample_fps"), 2, 4);
    const auto& thumbnails = K_LEDGER.at("thumbnails");
    const auto K_THUMBNAIL_BYTES = decisions.size() * 6144uz;
    const auto K_THUMBNAIL_ENTRY = assets.find("candidates.gray");
    if (thumbnails.at("file") != "candidates.gray" || thumbnails.at("format") != "GRAY8" ||
        unsigned_value(thumbnails.at("bytes")) != K_THUMBNAIL_BYTES ||
        K_THUMBNAIL_ENTRY == assets.end() || K_THUMBNAIL_ENTRY->second.first != K_THUMBNAIL_BYTES ||
        thumbnails.at("sha256") != K_THUMBNAIL_ENTRY->second.second) {
        throw std::invalid_argument("Quality thumbnail inventory mismatch");
    }
    const auto K_WIDTH_COUNT = unsigned_value(thumbnails.at("display_width"));
    const auto K_HEIGHT_COUNT = unsigned_value(thumbnails.at("display_height"));
    if (K_WIDTH_COUNT == 0 || K_HEIGHT_COUNT == 0 || K_WIDTH_COUNT > 8192 ||
        K_HEIGHT_COUNT > 8192 || K_WIDTH_COUNT * K_HEIGHT_COUNT > 4096uz * 4096uz) {
        throw std::invalid_argument("Quality display dimensions exceed profile");
    }
    const auto K_WIDTH = static_cast<double>(K_WIDTH_COUNT);
    const auto K_HEIGHT = static_cast<double>(K_HEIGHT_COUNT);
    if (quality_number(thumbnails.at("scale_x"), 0, 96) != 96 / K_WIDTH ||
        quality_number(thumbnails.at("scale_y"), 0, 64) != 64 / K_HEIGHT ||
        quality_number(thumbnails.at("offset_x"), -.5, 48) != (96 / K_WIDTH - 1) / 2 ||
        quality_number(thumbnails.at("offset_y"), -.5, 32) != (64 / K_HEIGHT - 1) / 2) {
        throw std::invalid_argument("Quality analysis transform mismatch");
    }
    std::ifstream input(contained(root, "candidates.gray"), std::ios::binary);
    std::array<char, 6144uz> pixels {};
    Sha256Builder thumbnail_hash;
    std::size_t selected {};
    std::uint64_t previous_index {};
    std::int64_t previous_time {};
    const std::set<std::string> K_REASONS { "seed",
                                           "motion_coverage",
                                           "repeated",
                                           "exposure",
                                           "blur_or_texture",
                                           "exposure_change",
                                           "low_motion" };
    for (std::size_t index { 0uz }; index < decisions.size(); ++index) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Quality admission cancelled");
        }
        const auto& decision = decisions[index];
        const auto& frame = decision.at("frame");
        const auto K_INDEX = unsigned_value(frame.at("source_index"));
        const auto K_PTS = unsigned_value(frame.at("pts"));
        if (K_PTS > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("Quality PTS exceeds int64");
        }
        const auto K_TIME = nanoseconds(
            timestamp(static_cast<std::int64_t>(K_PTS), frame.at("time_base").get<std::string>()));
        const auto K_PREFIX = source.substr(0uz, 16uz);
        if (K_INDEX >= unsigned_value(metadata.at("source_frame_count")) ||
            (index && (K_INDEX <= previous_index || K_TIME <= previous_time ||
                       static_cast<double>(K_TIME - previous_time) + 1 < 1e9 / K_FPS)) ||
            frame.at("frame_id") != K_PREFIX + "-" + std::to_string(K_INDEX) ||
            frame.at("calibration_id") != K_PREFIX + "-lens-0" ||
            unsigned_value(frame.at("capture_time_ns")) != static_cast<std::uint64_t>(K_TIME)) {
            throw std::invalid_argument("Quality candidate identity/order mismatch");
        }
        previous_index = K_INDEX;
        previous_time = K_TIME;
        const auto K_REASON = decision.at("reason").get<std::string>();
        const auto K_SELECTED = decision.at("selected").get<bool>();
        if (!K_REASONS.contains(K_REASON) ||
            K_SELECTED != (K_REASON == "seed" || K_REASON == "motion_coverage") ||
            (K_REASON == "seed" && selected != 0uz)) {
            throw std::invalid_argument("Invalid quality decision reason");
        }
        const auto& metrics = decision.at("metrics");
        const auto K_MEAN = quality_number(metrics.at("mean_luma"), 0, 255);
        const auto K_BLUR = quality_number(metrics.at("laplacian_variance"), 0, 1040400);
        const auto K_CLIPPED = quality_number(metrics.at("clipped_fraction"), 0, 1);
        const auto K_DISPLACEMENT =
            quality_number(metrics.at("displacement_px"), 0, std::sqrt(72.0));
        (void)quality_number(metrics.at("histogram_distance"), 0, 1);
        if (unsigned_value(metrics.at("feature_count")) > 12 ||
            unsigned_value(metrics.at("tracked_cells")) > 12) {
            throw std::invalid_argument("Quality feature count exceeds grid");
        }
        if (K_SELECTED && (K_MEAN < 12 || K_MEAN > 243 || K_BLUR < 25 || K_CLIPPED > .8 ||
                           unsigned_value(metrics.at("feature_count")) < 4 ||
                           (K_REASON == "motion_coverage" &&
                            (selected == 0uz || K_DISPLACEMENT < 1.5 ||
                             unsigned_value(metrics.at("tracked_cells")) < 4)))) {
            throw std::invalid_argument("Selected quality metrics contradict the frozen policy");
        }
        input.read(pixels.data(), static_cast<std::streamsize>(pixels.size()));
        const auto K_PIXELS = std::as_bytes(std::span(pixels));
        if (input.gcount() != static_cast<std::streamsize>(pixels.size()) ||
            decision.at("thumbnail_sha256") != sha256(K_PIXELS)) {
            throw std::invalid_argument("Quality candidate thumbnail mismatch");
        }
        thumbnail_hash.update(K_PIXELS);
        if (K_SELECTED) {
            if (selected >= metadata.at("frames").size() ||
                metadata.at("frames")[selected] != frame) {
                throw std::invalid_argument("Quality selection differs from admitted frames");
            }
            if (png_dimensions(contained(root, frame.at("file").get<std::string>())) !=
                std::pair { static_cast<std::uint32_t>(K_WIDTH_COUNT),
                           static_cast<std::uint32_t>(K_HEIGHT_COUNT) }) {
                throw std::invalid_argument("Quality analysis grid differs from selected image");
            }
            ++selected;
        } else if (frame.contains("file") || frame.contains("sha256")) {
            throw std::invalid_argument("Rejected quality candidate claims a selected image");
        }
    }
    if (selected != metadata.at("frames").size() || input.peek() != std::char_traits<char>::eof() ||
        thumbnail_hash.finish() != K_THUMBNAIL_ENTRY->second.second) {
        throw std::invalid_argument("Quality thumbnail/selection totals mismatch");
    }
    return {};
}

std::pair<std::uint32_t, std::uint32_t> png_dimensions(const std::filesystem::path& path) {
    std::array<unsigned char, 24> header {};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    constexpr std::array<unsigned char, 16> K_SIGNATURE {
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    if (file.gcount() != static_cast<std::streamsize>(header.size()) ||
        !std::equal(K_SIGNATURE.begin(), K_SIGNATURE.end(), header.begin())) {
        throw std::invalid_argument("Invalid decoded PNG header");
    }
    const auto K_NUMBER = [&](std::size_t offset) {
        std::uint32_t value {};
        for (std::size_t index { offset }; index < offset + 4uz; ++index) {
            value = (value << 8U) | header[index];
        }
        return value;
    };
    return { K_NUMBER(16), K_NUMBER(20) };
}

struct DecodedFrame {
    FrameId m_id;
    CalibrationId m_calibration;
    SourceTimestamp m_original;
    std::chrono::nanoseconds m_capture_time;
    std::filesystem::path m_path;
    std::string m_asset_name, m_digest, m_external_frame, m_external_calibration;
    std::size_t m_encoded_bytes {}, m_image_bytes {};
    std::uint32_t m_width {}, m_height {};
};

struct DecodedSourceState {
    DecodedSourceConfig m_config;
    std::filesystem::path m_root;
    std::string m_completion_digest;
    std::optional<std::string> m_journal_sha256;
    CaptureId m_capture;
    std::vector<DecodedFrame> m_frames;
    std::size_t m_position {};
    WorldEpoch m_epoch;
    bool m_reset { true };

    Result<void> unchanged(const std::stop_token& stop) const {
        if (!std::filesystem::is_regular_file(
                std::filesystem::symlink_status(m_root / "completion.json"))) {
            return fail(ErrorCode::INVALID_DATA, "Decode completion is missing or linked");
        }
        const auto K_DIGEST = sha256_file(m_root / "completion.json", K_MAX_MANIFEST_BYTES, stop);
        if (!K_DIGEST) {
            return std::unexpected(K_DIGEST.error());
        }
        if (*K_DIGEST != m_completion_digest) {
            return fail(ErrorCode::INVALID_DATA, "Decode completion changed during replay");
        }
        return {};
    }
};

class DecodedFrameSource final : public FrameSource {
  public:
    explicit DecodedFrameSource(std::unique_ptr<DecodedSourceState> state)
        : m_state(std::move(state)) { }

    SourceCapabilities capabilities() const noexcept override;
    Result<SourceEvent> read_next(std::stop_token stop) override;
    Result<void> seek(SourceTimestamp target) override;

    DecodedFrameCatalog catalog() const {
        DecodedFrameCatalog result {
            m_state->m_capture, m_state->m_completion_digest, {}, m_state->m_journal_sha256 };
        for (const auto& frame : m_state->m_frames) {
            result.m_observations.push_back({ frame.m_id,
                                             frame.m_calibration,
                                             frame.m_external_frame,
                                             frame.m_external_calibration,
                                             frame.m_original,
                                             frame.m_capture_time,
                                             frame.m_path,
                                             frame.m_digest,
                                             frame.m_encoded_bytes,
                                             frame.m_width,
                                             frame.m_height });
        }
        return result;
    }

  private:
    std::unique_ptr<DecodedSourceState> m_state;
};

}  // namespace

Result<std::unique_ptr<FrameSource>> open_decoded_frame_source(DecodedSourceConfig config,
                                                               const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Opening decoded source cancelled");
        }
        if (!digest_valid(config.m_source_sha256) || config.m_decoder_arguments.empty() ||
            config.m_max_image_bytes.m_bytes == 0 ||
            config.m_max_image_bytes.m_bytes > K_MAX_ENCODED_BYTES) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid decoded source configuration");
        }
        auto state = std::make_unique<DecodedSourceState>();
        state->m_config = std::move(config);
        state->m_root = std::filesystem::canonical(state->m_config.m_stage_directory);
        const auto K_COMPLETION_BYTES = bounded_file(state->m_root / "completion.json");
        const auto K_COMPLETION = parse(K_COMPLETION_BYTES);
        const auto K_KEY = K_COMPLETION.at("key").get<std::string>();
        if (K_COMPLETION.at("schema_version") != 1 || K_COMPLETION.at("status") != "complete" ||
            !digest_valid(K_KEY) || state->m_root.filename() != "decode-" + K_KEY) {
            return fail(ErrorCode::INVALID_DATA, "Not a committed decode stage");
        }
        const auto& inventory = K_COMPLETION.at("files");
        if (!inventory.is_array() || inventory.empty() || inventory.size() > 4096) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Invalid decode inventory count");
        }
        std::map<std::string, std::pair<std::uint64_t, std::string>> assets;
        for (const auto& entry : inventory) {
            const auto K_NAME = entry.at("path").get<std::string>();
            const auto K_BYTES = unsigned_value(entry.at("bytes"));
            const auto K_DIGEST = entry.at("sha256").get<std::string>();
            if (!digest_valid(K_DIGEST) ||
                !assets.emplace(K_NAME, std::pair { K_BYTES, K_DIGEST }).second) {
                return fail(ErrorCode::INVALID_DATA, "Duplicate or invalid decode inventory entry");
            }
        }
        const auto K_RESULT_BYTES = bounded_file(state->m_root / "result.json");
        const auto K_MANIFEST_ENTRY = assets.find("result.json");
        if (K_MANIFEST_ENTRY == assets.end() ||
            K_MANIFEST_ENTRY->second.first != K_RESULT_BYTES.size() ||
            K_MANIFEST_ENTRY->second.second !=
                sha256(std::as_bytes(std::span(K_RESULT_BYTES.data(), K_RESULT_BYTES.size())))) {
            return fail(ErrorCode::INVALID_DATA, "Decode metadata checksum/size mismatch");
        }
        const auto K_METADATA = parse(K_RESULT_BYTES);
        if ((K_METADATA.at("selection") != "timestamp_sampling_v1" &&
             K_METADATA.at("selection") != "native_rgb_journal_v1" &&
             K_METADATA.at("selection") != "quality_keyframes_candidate_v1") ||
            K_METADATA.at("crop") != "none" || K_METADATA.at("resize") != "none") {
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported decode replay profile");
        }
        const auto K_SOURCE_COUNT = unsigned_value(K_METADATA.at("source_frame_count"));
        if (K_METADATA.at("selection") == "native_rgb_journal_v1" &&
            (K_METADATA.at("source_sha256") != state->m_config.m_source_sha256 ||
             !digest_valid(K_METADATA.at("journal_sha256").get<std::string>()))) {
            return fail(ErrorCode::INVALID_DATA, "Native RGB journal source/provenance mismatch");
        }
        if (K_METADATA.at("selection") == "native_rgb_journal_v1") {
            state->m_journal_sha256 = K_METADATA.at("journal_sha256").get<std::string>();
        }
        const auto& frames = K_METADATA.at("frames");
        if (K_SOURCE_COUNT == 0 || K_SOURCE_COUNT > 36000 || !frames.is_array() || frames.empty() ||
            frames.size() > 120) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Decode frame count exceeds profile");
        }
        if (K_METADATA.at("selection") == "quality_keyframes_candidate_v1") {
            const auto K_QUALITY = validate_quality(
                state->m_root, K_METADATA, assets, state->m_config.m_source_sha256, stop);
            if (!K_QUALITY) {
                return std::unexpected(K_QUALITY.error());
            }
        }
        std::set<std::uint64_t> frame_ids;
        std::set<std::string> names;
        std::map<std::uint64_t, std::string> calibrations;
        std::map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> dimensions;
        std::uint64_t previous_index {};
        state->m_capture =
            CaptureId { adapter_detail::identity(state->m_config.m_source_sha256, "capture", "") };
        for (const auto& entry : frames) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Opening decoded source cancelled");
            }
            const auto K_INDEX = unsigned_value(entry.at("source_index"));
            const auto K_EXTERNAL_FRAME = entry.at("frame_id").get<std::string>();
            const auto K_EXTERNAL_CALIBRATION = entry.at("calibration_id").get<std::string>();
            const auto K_PREFIX = state->m_config.m_source_sha256.substr(0, 16);
            if (K_INDEX >= K_SOURCE_COUNT ||
                (!state->m_frames.empty() && K_INDEX <= previous_index) ||
                K_EXTERNAL_FRAME != K_PREFIX + "-" + std::to_string(K_INDEX) ||
                K_EXTERNAL_CALIBRATION != K_PREFIX + "-lens-0") {
                return fail(ErrorCode::INVALID_DATA, "Invalid source/frame/calibration identity");
            }
            previous_index = K_INDEX;
            DecodedFrame frame;
            frame.m_external_frame = K_EXTERNAL_FRAME;
            frame.m_external_calibration = K_EXTERNAL_CALIBRATION;
            frame.m_id = FrameId { adapter_detail::identity(
                state->m_config.m_source_sha256, "frame", K_EXTERNAL_FRAME) };
            frame.m_calibration = CalibrationId { adapter_detail::identity(
                state->m_config.m_source_sha256, "calibration", K_EXTERNAL_CALIBRATION) };
            if (!frame_ids.insert(frame.m_id.m_value).second) {
                return fail(ErrorCode::INVALID_DATA, "Duplicate or colliding frame identity");
            }
            const auto [K_CALIBRATION, K_INSERTED] =
                calibrations.emplace(frame.m_calibration.m_value, K_EXTERNAL_CALIBRATION);
            if (!K_INSERTED && K_CALIBRATION->second != K_EXTERNAL_CALIBRATION) {
                return fail(ErrorCode::INVALID_DATA, "Colliding calibration identity");
            }
            const auto K_PTS = unsigned_value(entry.at("pts"));
            if (K_PTS > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return fail(ErrorCode::INVALID_DATA, "Original PTS exceeds int64");
            }
            frame.m_original = timestamp(static_cast<std::int64_t>(K_PTS),
                                         entry.at("time_base").get<std::string>());
            const auto K_CAPTURE_TIME = unsigned_value(entry.at("capture_time_ns"));
            const auto K_EXPECTED_TIME = nanoseconds(frame.m_original);
            if (K_CAPTURE_TIME != static_cast<std::uint64_t>(K_EXPECTED_TIME) ||
                (!state->m_frames.empty() &&
                 K_EXPECTED_TIME <= state->m_frames.back().m_capture_time.count())) {
                return fail(ErrorCode::INVALID_DATA, "Inconsistent or nonmonotonic capture time");
            }
            frame.m_capture_time = std::chrono::nanoseconds(K_EXPECTED_TIME);
            const auto K_NAME = entry.at("file").get<std::string>();
            frame.m_asset_name = K_NAME;
            frame.m_path = contained(state->m_root, K_NAME);
            frame.m_digest = entry.at("sha256").get<std::string>();
            const auto K_ASSET = assets.find(K_NAME);
            if (!names.insert(K_NAME).second || frame.m_path.extension() != ".png" ||
                K_ASSET == assets.end() || K_ASSET->second.second != frame.m_digest ||
                K_ASSET->second.first == 0 || K_ASSET->second.first > K_MAX_ENCODED_BYTES ||
                std::filesystem::file_size(frame.m_path) != K_ASSET->second.first) {
                return fail(ErrorCode::INVALID_DATA, "Invalid decoded image inventory");
            }
            frame.m_encoded_bytes = static_cast<std::size_t>(K_ASSET->second.first);
            const auto [K_WIDTH, K_HEIGHT] = png_dimensions(frame.m_path);
            if (K_WIDTH == 0 || K_HEIGHT == 0 || K_WIDTH > 8192 || K_HEIGHT > 8192 ||
                static_cast<std::uint64_t>(K_WIDTH) * K_HEIGHT > 4096ULL * 4096 ||
                static_cast<std::uint64_t>(K_WIDTH) * K_HEIGHT * 3 >
                    state->m_config.m_max_image_bytes.m_bytes) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Decoded RGB image exceeds budget");
            }
            const auto [K_SIZE, K_NEW_CALIBRATION] =
                dimensions.emplace(frame.m_calibration.m_value, std::pair { K_WIDTH, K_HEIGHT });
            if (!K_NEW_CALIBRATION && K_SIZE->second != std::pair { K_WIDTH, K_HEIGHT }) {
                return fail(ErrorCode::INVALID_DATA,
                            "Image dimensions changed without calibration");
            }
            frame.m_width = K_WIDTH;
            frame.m_height = K_HEIGHT;
            frame.m_image_bytes = static_cast<std::size_t>(K_WIDTH) * K_HEIGHT * 3;
            state->m_frames.push_back(std::move(frame));
        }
        state->m_completion_digest =
            sha256(std::as_bytes(std::span(K_COMPLETION_BYTES.data(), K_COMPLETION_BYTES.size())));
        const auto K_UNCHANGED = state->unchanged(stop);
        if (!K_UNCHANGED) {
            return std::unexpected(K_UNCHANGED.error());
        }
        return std::unique_ptr<FrameSource>(std::make_unique<DecodedFrameSource>(std::move(state)));
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Invalid decoded source: ") + exception.what());
    }
}

Result<DecodedFrameCatalog> inspect_decoded_frame_source(DecodedSourceConfig config,
                                                         const std::stop_token& stop) {
    try {
        auto source = open_decoded_frame_source(std::move(config), stop);
        if (!source) {
            return std::unexpected(source.error());
        }
        return static_cast<const DecodedFrameSource&>(**source).catalog();
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Frame catalog allocation failed");
    }
}

SourceCapabilities DecodedFrameSource::capabilities() const noexcept {
    return { true, false, false, m_state->m_config.m_max_image_bytes };
}

Result<SourceEvent> DecodedFrameSource::read_next(std::stop_token stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Frame replay cancelled");
        }
        const auto K_UNCHANGED = m_state->unchanged(stop);
        if (!K_UNCHANGED) {
            return std::unexpected(K_UNCHANGED.error());
        }
        if (m_state->m_reset) {
            m_state->m_reset = false;
            return SourceReset { m_state->m_capture, m_state->m_epoch };
        }
        if (m_state->m_position == m_state->m_frames.size()) {
            return SourceEnd {};
        }
        const auto& frame = m_state->m_frames[m_state->m_position];
        const auto K_IMAGE = contained(m_state->m_root, frame.m_asset_name);
        const auto K_INITIAL_HASH = sha256_file(K_IMAGE, frame.m_encoded_bytes, stop);
        if (!K_INITIAL_HASH) {
            return std::unexpected(K_INITIAL_HASH.error());
        }
        if (*K_INITIAL_HASH != frame.m_digest ||
            std::filesystem::file_size(K_IMAGE) != frame.m_encoded_bytes) {
            return fail(ErrorCode::INVALID_DATA, "Decoded image checksum/size changed");
        }
        auto command = m_state->m_config.m_decoder_arguments;
        const auto K_IMAGE_UTF8 = K_IMAGE.u8string();
        const std::vector<std::string> K_ARGUMENTS {
            "-nostdin",
            "-hide_banner",
            "-loglevel",
            "error",
            "-max_alloc",
            "67108864",
            "-noautorotate",
            "-threads",
            "1",
            "-i",
            std::string(K_IMAGE_UTF8.begin(), K_IMAGE_UTF8.end()),
            "-frames:v",
            "1",
            "-threads",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "pipe:1" };
        command.insert(command.end(), K_ARGUMENTS.begin(), K_ARGUMENTS.end());
        auto pixels = std::make_shared<std::vector<std::byte>>();
        pixels->reserve(frame.m_image_bytes);
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] {
            cancellation.request_stop();
        });
        bool oversized {};
        std::string diagnostic;
        const auto K_DECODED = execute_process(
            command,
            cancellation.get_token(),
            [&](const std::string& output) {
                if (output.size() > frame.m_image_bytes - pixels->size()) {
                    oversized = true;
                    cancellation.request_stop();
                    return;
                }
                const auto K_BYTES = std::as_bytes(std::span(output.data(), output.size()));
                pixels->insert(pixels->end(), K_BYTES.begin(), K_BYTES.end());
            },
            [&](const std::string& output) {
                diagnostic.append(output, 0, std::min(output.size(), 4096uz - diagnostic.size()));
            });
        if (oversized) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Decoder output exceeds image budget");
        }
        if (!K_DECODED) {
            return std::unexpected(K_DECODED.error());
        }
        if (*K_DECODED != 0 || pixels->size() != frame.m_image_bytes) {
            return fail(ErrorCode::BACKEND_FAILURE,
                        "Image decoder failed or returned short RGB output: " + diagnostic);
        }
        const auto K_FINAL_HASH = sha256_file(K_IMAGE, frame.m_encoded_bytes, stop);
        if (!K_FINAL_HASH) {
            return std::unexpected(K_FINAL_HASH.error());
        }
        if (*K_FINAL_HASH != frame.m_digest) {
            return fail(ErrorCode::INVALID_DATA, "Decoded image changed during pixel read");
        }
        FramePacket packet { frame.m_id,
                            m_state->m_capture,
                            frame.m_capture_time,
                            frame.m_original,
                            frame.m_calibration,
                            { std::move(pixels),
                             frame.m_width,
                             frame.m_height,
                             frame.m_width * 3,
                             PixelFormat::RGB8 },
                            {},
                            {} };
        ++m_state->m_position;
        return packet;
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Frame replay failed: ") + exception.what());
    }
}

Result<void> DecodedFrameSource::seek(SourceTimestamp target) {
    try {
        const auto K_TIME = std::chrono::nanoseconds(nanoseconds(target));
        const auto K_UNCHANGED = m_state->unchanged({});
        if (!K_UNCHANGED) {
            return std::unexpected(K_UNCHANGED.error());
        }
        if (m_state->m_epoch.m_value == std::numeric_limits<std::uint64_t>::max()) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Source reset epoch exhausted");
        }
        const auto K_FOUND =
            std::ranges::lower_bound(m_state->m_frames, K_TIME, {}, &DecodedFrame::m_capture_time);
        m_state->m_position = static_cast<std::size_t>(K_FOUND - m_state->m_frames.begin());
        ++m_state->m_epoch.m_value;
        m_state->m_reset = true;
        return {};
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_ARGUMENT, std::string("Invalid seek: ") + exception.what());
    }
}

}  // namespace world_explorer
