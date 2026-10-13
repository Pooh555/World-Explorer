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
constexpr std::uint64_t MAX_SOURCE_BYTES = 16ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t MAX_FREE_BYTES = 32ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t MAX_INSPECTION_BYTES = 16ULL * 1024 * 1024;

std::int64_t positive_integer(std::string_view text) {
    std::int64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0)
        throw std::invalid_argument("Invalid original stream time base");
    return value;
}
std::uint64_t unsigned_integer(const nlohmann::json& value) {
    if (!value.is_number_unsigned())
        throw std::invalid_argument("Expected an unsigned metadata integer");
    return value.get<std::uint64_t>();
}
} // namespace
namespace video_detail {
bool overlaps(const std::filesystem::path& first, const std::filesystem::path& second) {
    const auto relative = first.lexically_relative(second);
    const auto reverse = second.lexically_relative(first);
    return first == second || (!relative.empty() && *relative.begin() != "..") ||
           (!reverse.empty() && *reverse.begin() != "..");
}
result<void> check_storage(const std::vector<std::filesystem::path>& outputs,
                           std::uint64_t minimum_free_bytes, const std::stop_token& stop) {
    const auto report = inspect_output_storage(outputs, minimum_free_bytes, stop);
    if (!report)
        return std::unexpected(report.error());
    return {};
}
} // namespace video_detail

result<std::vector<output_storage_report>>
inspect_output_storage(const std::vector<std::filesystem::path>& outputs,
                       std::uint64_t minimum_free_bytes, const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Output storage inspection cancelled");
        if (minimum_free_bytes == 0 || minimum_free_bytes > MAX_FREE_BYTES || outputs.empty() ||
            outputs.size() > 4)
            return fail(error_code::INVALID_ARGUMENT, "Invalid output storage requirements");
        std::vector<output_storage_report> report;
        report.reserve(outputs.size());
        for (const auto& output : outputs) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Video preflight cancelled");
            if (output.empty())
                return fail(error_code::INVALID_ARGUMENT, "Empty output storage path");
            const auto selected = std::filesystem::weakly_canonical(output);
            auto ancestor = selected;
            while (!std::filesystem::exists(ancestor)) {
                const auto parent = ancestor.parent_path();
                if (parent == ancestor || parent.empty())
                    return fail(error_code::IO_ERROR, "No existing output volume for preflight");
                ancestor = parent;
            }
            if (!std::filesystem::is_directory(ancestor))
                return fail(error_code::INVALID_ARGUMENT, "Output ancestor is not a directory");
            const auto available = std::filesystem::space(ancestor).available;
            if (available == std::numeric_limits<std::uintmax_t>::max())
                return fail(error_code::IO_ERROR, "Available output storage is unknown");
            if (available < minimum_free_bytes)
                return fail(error_code::RESOURCE_EXHAUSTED,
                            "Insufficient available space at output: " + output.string());
            report.push_back({selected, ancestor, available});
        }
        return report;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Output storage inspection allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        return fail(error_code::IO_ERROR,
                    std::string("Output storage inspection failed: ") + exception.what());
    }
}

result<video_preflight_report> preflight_video(const video_preflight_request& request,
                                               const std::stop_token& stop,
                                               const log_callback& log) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Video preflight cancelled");
        const auto& selected = request.inspection;
        if (request.video.empty() || selected.stage.empty() ||
            !job_detail::valid_digest(selected.completion_sha256) ||
            selected.minimum_free_bytes == 0 || selected.minimum_free_bytes > MAX_FREE_BYTES ||
            request.outputs.empty() || request.outputs.size() > 3)
            return fail(error_code::INVALID_ARGUMENT, "Invalid video preflight request");
        const auto stage = std::filesystem::canonical(selected.stage);
        const auto name = stage.filename().string();
        if (std::filesystem::is_symlink(selected.stage) ||
            stage.parent_path().filename() != "stages" || !name.starts_with("inspect-") ||
            !job_detail::valid_digest(name.substr(8)))
            return fail(error_code::INVALID_DATA, "Select a committed inspect stage explicitly");
        const auto video = std::filesystem::canonical(request.video);
        const auto inspection_job = stage.parent_path().parent_path();
        for (const auto& output : request.outputs) {
            if (output.empty())
                return fail(error_code::INVALID_ARGUMENT, "Empty preflight output path");
            const auto path = std::filesystem::weakly_canonical(output);
            if (video_detail::overlaps(path, video) || video_detail::overlaps(path, inspection_job))
                return fail(error_code::INVALID_ARGUMENT, "Preflight output overlaps its inputs");
        }
        const auto checked =
            verify_committed_stage(stage, name.substr(8), stop,
                                   {.maximum_bytes = MAX_INSPECTION_BYTES,
                                    .maximum_files = 16,
                                    .maximum_manifest_bytes = std::size_t{64} * 1024});
        if (!checked)
            return std::unexpected(checked.error());
        if (checked->completion_sha256 != selected.completion_sha256 ||
            !checked->assets.contains("result.json"))
            return fail(error_code::INVALID_DATA, "Inspection completion differs from selection");
        const auto metadata = job_detail::read_json(stage / "result.json", std::size_t{64} * 1024,
                                                    checked->assets.at("result.json").sha256);
        if (!metadata)
            return std::unexpected(metadata.error());
        const auto& stream = metadata->at("stream");
        const auto codec_name = stream.at("codec_name").get<std::string>();
        const auto width = unsigned_integer(stream.at("width"));
        const auto height = unsigned_integer(stream.at("height"));
        const auto duration = metadata->at("duration_seconds").get<double>();
        const auto& rotation_value = metadata->at("rotation_degrees");
        if (!rotation_value.is_number_integer() ||
            (rotation_value.is_number_unsigned() && rotation_value.get<std::uint64_t>() > 270))
            return fail(error_code::INVALID_DATA, "Invalid display rotation");
        const auto rotation = rotation_value.get<std::int64_t>();
        if (!metadata->at("schema_version").is_number_unsigned() ||
            metadata->at("schema_version") != 1 || stream.at("codec_type") != "video" ||
            codec_name.empty() || codec_name.size() > 256 ||
            codec_name.find('\0') != std::string::npos || width == 0 || height == 0 ||
            width > std::uint64_t{4096} * 4096 || height > std::uint64_t{4096} * 4096 ||
            width * height > std::uint64_t{4096} * 4096 || !std::isfinite(duration) ||
            duration <= 0 || duration > 1200 || rotation < -270 || rotation > 270 ||
            rotation % 90 != 0 || metadata->at("audio") != "ignored" ||
            metadata->at("scale_status") != "ARBITRARY" || metadata->at("color_domain") != "srgb")
            return fail(error_code::INVALID_DATA, "Unsupported or invalid video metadata");
        const auto transfer = stream.value("color_transfer", std::string{});
        if (transfer == "smpte2084" || transfer == "arib-std-b67")
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "HDR requires an explicitly calibrated conversion");
        const auto time_base = stream.at("time_base").get<std::string>();
        const auto slash = time_base.find('/');
        if (slash == std::string::npos || time_base.size() > 64)
            return fail(error_code::INVALID_DATA, "Invalid original stream time base");
        video_preflight_report report;
        report.time_base_numerator = positive_integer(std::string_view(time_base).substr(0, slash));
        report.time_base_denominator =
            positive_integer(std::string_view(time_base).substr(slash + 1));
        report.source_bytes = unsigned_integer(metadata->at("source").at("bytes"));
        if (!report.source_bytes || report.source_bytes > MAX_SOURCE_BYTES ||
            std::filesystem::file_size(video) != report.source_bytes)
            return fail(error_code::INVALID_DATA, "Inspection source byte count differs");
        const auto digest = sha256_file(video, MAX_SOURCE_BYTES, stop);
        if (!digest)
            return std::unexpected(digest.error());
        if (metadata->at("source").at("sha256") != *digest)
            return fail(error_code::INVALID_DATA, "Inspection belongs to a different video");
        report.source_sha256 = *digest;
        report.inspection_completion_sha256 = selected.completion_sha256;
        report.width = static_cast<std::uint32_t>(width);
        report.height = static_cast<std::uint32_t>(height);
        report.duration_seconds = duration;
        report.rotation_degrees = static_cast<std::int32_t>(rotation);
        report.codec_name = codec_name;
        const auto storage =
            inspect_output_storage(request.outputs, selected.minimum_free_bytes, stop);
        if (!storage)
            return std::unexpected(storage.error());
        report.storage = *storage;
        const auto final_hash =
            sha256_file(stage / "completion.json", std::size_t{64} * 1024, stop);
        if (!final_hash)
            return std::unexpected(final_hash.error());
        if (*final_hash != selected.completion_sha256)
            return fail(error_code::INVALID_DATA, "Inspection changed during preflight");
        if (log)
            log("Retained video metadata checked: " + std::to_string(report.width) + "x" +
                std::to_string(report.height) + "; original time base " + time_base +
                ". Current output storage checked; no disk reservation.\n");
        return report;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Video preflight allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Video preflight failed: ") + exception.what());
    }
}
} // namespace world_explorer
