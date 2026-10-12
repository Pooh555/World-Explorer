#include "map_files.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <utility>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace world_explorer::map_files {
namespace {
constexpr std::size_t IO_BLOCK_BYTES = std::size_t{256} * 1024;
struct staged_file {
    std::filesystem::path path;
    staged_file() = default;
    staged_file(staged_file&& other) noexcept : path(std::exchange(other.path, {})) {}
    staged_file(const staged_file&) = delete;
    staged_file& operator=(const staged_file&) = delete;
    ~staged_file() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
};
struct file_closer {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
result<staged_file> stage_bytes(const std::filesystem::path& destination,
                                std::span<const std::byte> bytes, const std::stop_token& stop) {
    try {
        if (destination.filename().empty())
            return fail(error_code::INVALID_ARGUMENT, "Pose destination must name a file");
        auto parent = destination.parent_path();
        if (parent.empty())
            parent = ".";
        if (!std::filesystem::is_directory(parent))
            return fail(error_code::IO_ERROR, "Pose destination parent is missing");
        static std::atomic<std::uint64_t> sequence{};
        staged_file staging;
        for (std::size_t attempt = 0; attempt < 32; ++attempt) {
            const auto candidate =
                parent /
                (".we-map-tmp-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(sequence.fetch_add(1)));
            if (std::filesystem::create_directory(candidate)) {
                staging.path = candidate;
                break;
            }
        }
        if (staging.path.empty())
            return fail(error_code::IO_ERROR, "Cannot reserve pose staging directory");
        const auto temporary = staging.path / "poses.weposes";
        std::FILE* raw{};
#ifdef _WIN32
        if (_wfopen_s(&raw, temporary.c_str(), L"wb") != 0)
            raw = nullptr;
#else
        raw = std::fopen(temporary.c_str(), "wb");
#endif
        std::unique_ptr<std::FILE, file_closer> file(raw);
        if (!file)
            return fail(error_code::IO_ERROR, "Cannot open staged pose asset");
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Pose asset cancelled before publication");
            const auto count = std::min(IO_BLOCK_BYTES, bytes.size() - offset);
            const auto block = bytes.subspan(offset, count);
            if (std::fwrite(block.data(), 1, block.size(), file.get()) != block.size())
                return fail(error_code::IO_ERROR, "Pose asset write failed");
            offset += count;
        }
        if (std::fflush(file.get()) != 0)
            return fail(error_code::IO_ERROR, "Pose asset flush failed");
#ifdef _WIN32
        const bool flushed = _commit(_fileno(file.get())) == 0;
#else
        const bool flushed = fsync(fileno(file.get())) == 0;
#endif
        const bool closed = std::fclose(file.release()) == 0;
        if (!flushed || !closed)
            return fail(error_code::IO_ERROR, "Pose asset durable write failed");
        return staging;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "File staging allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("File staging failed: ") + e.what());
    }
}
result<void> flush_parent(const std::filesystem::path& destination) {
#ifndef _WIN32
    auto parent = destination.parent_path();
    if (parent.empty())
        parent = ".";
    const int descriptor = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return fail(error_code::IO_ERROR, "Cannot open publication directory for durability");
    const int status = fsync(descriptor);
    close(descriptor);
    if (status != 0)
        return fail(error_code::IO_ERROR, "Directory flush failed after publication");
#else
    (void)destination;
#endif
    return {};
}
} // namespace
result<void> flush_directory_entry(const std::filesystem::path& directory) {
    return flush_parent(directory);
}
result<std::vector<std::byte>> read_bytes(const std::filesystem::path& path, std::size_t maximum,
                                          const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose asset cancelled");
        if (!maximum || maximum > std::size_t{256} * 1024 * 1024)
            return fail(error_code::INVALID_ARGUMENT, "Invalid file byte budget");
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
            return fail(error_code::INVALID_DATA, "Pose asset must be a regular nonsymlink file");
        const auto size = std::filesystem::file_size(path);
        if (!size || size > maximum)
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset file exceeds budget");
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return fail(error_code::IO_ERROR, "Cannot open pose asset");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Pose asset cancelled");
            const auto count = std::min(IO_BLOCK_BYTES, bytes.size() - offset);
            input.read(reinterpret_cast<char*>(bytes.data() + offset),
                       static_cast<std::streamsize>(count));
            if (input.gcount() != static_cast<std::streamsize>(count))
                return fail(error_code::IO_ERROR, "Pose asset truncated during read");
            offset += count;
        }
        if (input.peek() != std::char_traits<char>::eof() || input.bad())
            return fail(error_code::INVALID_DATA, "Pose asset grew or failed during read");
        return bytes;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose asset allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Pose asset read failed: ") + e.what());
    }
}

result<std::string> publish_asset(const std::filesystem::path& destination,
                                  std::span<const std::byte> bytes, const std::stop_token& stop) {
    try {
        auto staged = stage_bytes(destination, bytes, stop);
        if (!staged)
            return std::unexpected(staged.error());
        sha256_builder digest;
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Asset publication cancelled");
            const auto count = std::min(IO_BLOCK_BYTES, bytes.size() - offset);
            digest.update(bytes.subspan(offset, count));
            offset += count;
        }
        auto hash = digest.finish();
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Asset publication cancelled");
        std::filesystem::create_hard_link(staged->path / "poses.weposes", destination);
        const auto flushed = flush_parent(destination);
        if (!flushed)
            return std::unexpected(flushed.error());
        return hash;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Asset publication allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Asset publication failed: ") + e.what());
    }
}
result<void> replace_manifest(const std::filesystem::path& destination,
                              std::span<const std::byte> bytes, const std::stop_token& stop,
                              bool& replaced) {
    replaced = false;
    try {
        auto staged = stage_bytes(destination, bytes, stop);
        if (!staged)
            return std::unexpected(staged.error());
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Manifest publication cancelled");
        const auto temporary = staged->path / "poses.weposes";
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return fail(error_code::IO_ERROR, "Manifest atomic replacement failed");
#else
        std::filesystem::rename(temporary, destination);
#endif
        replaced = true;
        return flush_parent(destination);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Manifest publication allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Manifest publication failed: ") + e.what());
    }
}
} // namespace world_explorer::map_files
