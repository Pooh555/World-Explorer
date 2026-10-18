#include "world_explorer/application/video_preflight.hpp"

#include "job_validation.hpp"
#include "video_preflight_detail.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>

namespace world_explorer {
namespace {

constexpr std::uint64_t K_MAX_SOURCE_BYTES { 16ULL * 1024 * 1024 * 1024 };
constexpr std::uint64_t K_MAX_FREE_BYTES { 32ULL * 1024 * 1024 * 1024 };
constexpr std::uint64_t K_MAX_INSPECTION_BYTES { 16ULL * 1024 * 1024 };

std::int64_t positive_integer(std::string_view text) {
    std::int64_t value {};
    const auto K_PARSED = std::from_chars(text.data(), text.data() + text.size(), value);
    if (K_PARSED.ec != std::errc {} || K_PARSED.ptr != text.data() + text.size() || value <= 0)
        throw std::invalid_argument("Invalid original stream time base");
    return value;
}

std::uint64_t unsigned_integer(const nlohmann::json& value) {
    if (!value.is_number_unsigned())
        throw std::invalid_argument("Expected an unsigned metadata integer");
    return value.get<std::uint64_t>();
}

}  // namespace

namespace video_detail {

bool overlaps(const std::filesystem::path& first, const std::filesystem::path& second) {
    const auto K_RELATIVE = first.lexically_relative(second);
    const auto K_REVERSE = second.lexically_relative(first);
    return first == second || (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") ||
           (!K_REVERSE.empty() && *K_REVERSE.begin() != "..");
}

Result<void> check_storage(const std::vector<std::filesystem::path>& outputs,
                           std::uint64_t minimum_free_bytes,
                           const std::stop_token& stop) {
    const auto K_REPORT = inspect_output_storage(outputs, minimum_free_bytes, stop);
    if (!K_REPORT) return std::unexpected(K_REPORT.error());
    return {};
}

}  // namespace video_detail

Result<std::vector<OutputStorageReport>>
inspect_output_storage(const std::vector<std::filesystem::path>& outputs,
                       std::uint64_t minimum_free_bytes,
                       const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Output storage inspection cancelled");
        if (minimum_free_bytes == 0 || minimum_free_bytes > K_MAX_FREE_BYTES || outputs.empty() ||
            outputs.size() > 4)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid output storage requirements");
        std::vector<OutputStorageReport> report;
        report.reserve(outputs.size());
        for (const auto& output : outputs) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Video preflight cancelled");
            if (output.empty())
                return fail(ErrorCode::INVALID_ARGUMENT, "Empty output storage path");
            const auto K_SELECTED = std::filesystem::weakly_canonical(output);
            auto ancestor = K_SELECTED;
            while (!std::filesystem::exists(ancestor)) {
                const auto K_PARENT = ancestor.parent_path();
                if (K_PARENT == ancestor || K_PARENT.empty())
                    return fail(ErrorCode::IO_ERROR, "No existing output volume for preflight");
                ancestor = K_PARENT;
            }
            if (!std::filesystem::is_directory(ancestor))
                return fail(ErrorCode::INVALID_ARGUMENT, "Output ancestor is not a directory");
            const auto K_AVAILABLE = std::filesystem::space(ancestor).available;
            if (K_AVAILABLE == std::numeric_limits<std::uintmax_t>::max())
                return fail(ErrorCode::IO_ERROR, "Available output storage is unknown");
            if (K_AVAILABLE < minimum_free_bytes)
                return fail(ErrorCode::RESOURCE_EXHAUSTED,
                            "Insufficient available space at output: " + output.string());
            report.push_back({K_SELECTED, ancestor, K_AVAILABLE});
        }
        return report;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Output storage inspection allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        return fail(ErrorCode::IO_ERROR,
                    std::string("Output storage inspection failed: ") + exception.what());
    }
}

Result<VideoPreflightReport> preflight_video(const VideoPreflightRequest& request,
                                             const std::stop_token& stop,
                                             const LogCallback& log) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Video preflight cancelled");
        const auto& selected = request.m_inspection;
        if (request.m_video.empty() || selected.m_stage.empty() ||
            !job_detail::valid_digest(selected.m_completion_sha256) ||
            selected.m_minimum_free_bytes == 0 ||
            selected.m_minimum_free_bytes > K_MAX_FREE_BYTES || request.m_outputs.empty() ||
            request.m_outputs.size() > 3)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid video preflight request");
        const auto K_STAGE = std::filesystem::canonical(selected.m_stage);
        const auto K_NAME = K_STAGE.filename().string();
        if (std::filesystem::is_symlink(selected.m_stage) ||
            K_STAGE.parent_path().filename() != "stages" || !K_NAME.starts_with("inspect-") ||
            !job_detail::valid_digest(K_NAME.substr(8)))
            return fail(ErrorCode::INVALID_DATA, "Select a committed inspect stage explicitly");
        const auto K_VIDEO = std::filesystem::canonical(request.m_video);
        const auto K_INSPECTION_JOB = K_STAGE.parent_path().parent_path();
        for (const auto& output : request.m_outputs) {
            if (output.empty())
                return fail(ErrorCode::INVALID_ARGUMENT, "Empty preflight output path");
            const auto K_PATH = std::filesystem::weakly_canonical(output);
            if (video_detail::overlaps(K_PATH, K_VIDEO) ||
                video_detail::overlaps(K_PATH, K_INSPECTION_JOB))
                return fail(ErrorCode::INVALID_ARGUMENT, "Preflight output overlaps its inputs");
        }
        const auto K_CHECKED = verify_committed_stage(K_STAGE,
                                                      K_NAME.substr(8),
                                                      stop,
                                                      {.m_maximum_bytes = K_MAX_INSPECTION_BYTES,
                                                       .m_maximum_files = 16,
                                                       .m_maximum_manifest_bytes = 64uz * 1024});
        if (!K_CHECKED) return std::unexpected(K_CHECKED.error());
        if (K_CHECKED->m_completion_sha256 != selected.m_completion_sha256 ||
            !K_CHECKED->m_assets.contains("result.json"))
            return fail(ErrorCode::INVALID_DATA, "Inspection completion differs from selection");
        const auto K_METADATA = job_detail::read_json(
            K_STAGE / "result.json", 64uz * 1024, K_CHECKED->m_assets.at("result.json").m_sha256);
        if (!K_METADATA) return std::unexpected(K_METADATA.error());
        const auto& stream = K_METADATA->at("stream");
        const auto K_CODEC_NAME = stream.at("codec_name").get<std::string>();
        const auto K_WIDTH = unsigned_integer(stream.at("width"));
        const auto K_HEIGHT = unsigned_integer(stream.at("height"));
        const auto K_DURATION = K_METADATA->at("duration_seconds").get<double>();
        const auto& rotation_value = K_METADATA->at("rotation_degrees");
        if (!rotation_value.is_number_integer() ||
            (rotation_value.is_number_unsigned() && rotation_value.get<std::uint64_t>() > 270))
            return fail(ErrorCode::INVALID_DATA, "Invalid display rotation");
        const auto K_ROTATION = rotation_value.get<std::int64_t>();
        if (!K_METADATA->at("schema_version").is_number_unsigned() ||
            K_METADATA->at("schema_version") != 1 || stream.at("codec_type") != "video" ||
            K_CODEC_NAME.empty() || K_CODEC_NAME.size() > 256 ||
            K_CODEC_NAME.find('\0') != std::string::npos || K_WIDTH == 0 || K_HEIGHT == 0 ||
            K_WIDTH > std::uint64_t { 4096 } * 4096 || K_HEIGHT > std::uint64_t { 4096 } * 4096 ||
            K_WIDTH * K_HEIGHT > std::uint64_t { 4096 } * 4096 || !std::isfinite(K_DURATION) ||
            K_DURATION <= 0 || K_DURATION > 1200 || K_ROTATION < -270 || K_ROTATION > 270 ||
            K_ROTATION % 90 != 0 || K_METADATA->at("audio") != "ignored" ||
            K_METADATA->at("scale_status") != "ARBITRARY" ||
            K_METADATA->at("color_domain") != "srgb")
            return fail(ErrorCode::INVALID_DATA, "Unsupported or invalid video metadata");
        const auto K_TRANSFER = stream.value("color_transfer", std::string {});
        if (K_TRANSFER == "smpte2084" || K_TRANSFER == "arib-std-b67")
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "HDR requires an explicitly calibrated conversion");
        const auto K_TIME_BASE = stream.at("time_base").get<std::string>();
        const auto K_SLASH = K_TIME_BASE.find('/');
        if (K_SLASH == std::string::npos || K_TIME_BASE.size() > 64)
            return fail(ErrorCode::INVALID_DATA, "Invalid original stream time base");
        VideoPreflightReport report;
        report.m_time_base_numerator =
            positive_integer(std::string_view(K_TIME_BASE).substr(0, K_SLASH));
        report.m_time_base_denominator =
            positive_integer(std::string_view(K_TIME_BASE).substr(K_SLASH + 1));
        report.m_source_bytes = unsigned_integer(K_METADATA->at("source").at("bytes"));
        if (!report.m_source_bytes || report.m_source_bytes > K_MAX_SOURCE_BYTES ||
            std::filesystem::file_size(K_VIDEO) != report.m_source_bytes)
            return fail(ErrorCode::INVALID_DATA, "Inspection source byte count differs");
        const auto K_DIGEST = sha256_file(K_VIDEO, K_MAX_SOURCE_BYTES, stop);
        if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
        if (K_METADATA->at("source").at("sha256") != *K_DIGEST)
            return fail(ErrorCode::INVALID_DATA, "Inspection belongs to a different video");
        report.m_source_sha256 = *K_DIGEST;
        report.m_inspection_completion_sha256 = selected.m_completion_sha256;
        report.m_width = static_cast<std::uint32_t>(K_WIDTH);
        report.m_height = static_cast<std::uint32_t>(K_HEIGHT);
        report.m_duration_seconds = K_DURATION;
        report.m_rotation_degrees = static_cast<std::int32_t>(K_ROTATION);
        report.m_codec_name = K_CODEC_NAME;
        const auto K_STORAGE =
            inspect_output_storage(request.m_outputs, selected.m_minimum_free_bytes, stop);
        if (!K_STORAGE) return std::unexpected(K_STORAGE.error());
        report.m_storage = *K_STORAGE;
        const auto K_FINAL_HASH = sha256_file(K_STAGE / "completion.json", 64uz * 1024, stop);
        if (!K_FINAL_HASH) return std::unexpected(K_FINAL_HASH.error());
        if (*K_FINAL_HASH != selected.m_completion_sha256)
            return fail(ErrorCode::INVALID_DATA, "Inspection changed during preflight");
        if (log)
            log("Retained video metadata checked: " + std::to_string(report.m_width) + "x" +
                std::to_string(report.m_height) + "; original time base " + K_TIME_BASE +
                ". Current output storage checked; no disk reservation.\n");
        return report;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Video preflight allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Video preflight failed: ") + exception.what());
    }
}

}  // namespace world_explorer
