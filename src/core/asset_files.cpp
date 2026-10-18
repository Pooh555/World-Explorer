#include "world_explorer/core/asset_files.hpp"

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
namespace world_explorer::asset_files {
namespace {

constexpr std::size_t K_IO_BLOCK_BYTES { 256uz * 1024uz };

struct StagedFile {
    std::filesystem::path m_path;
    StagedFile() = default;

    StagedFile(StagedFile&& other) noexcept : m_path(std::exchange(other.m_path, {})) { }

    StagedFile(const StagedFile&) = delete;
    StagedFile& operator=(const StagedFile&) = delete;

    ~StagedFile() {
        if (!m_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
    }
};

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

Result<StagedFile> stage_bytes(const std::filesystem::path& destination,
                               std::span<const std::byte> bytes,
                               const std::stop_token& stop) {
    try {
        if (destination.filename().empty())
            return fail(ErrorCode::INVALID_ARGUMENT, "Asset destination must name a file");
        auto parent = destination.parent_path();
        if (parent.empty()) parent = ".";
        if (!std::filesystem::is_directory(parent))
            return fail(ErrorCode::IO_ERROR, "Asset destination parent is missing");
        static std::atomic<std::uint64_t> sequence {};
        StagedFile staging;
        for (std::size_t attempt { 0uz }; attempt < 32uz; ++attempt) {
            const auto K_CANDIDATE =
                parent /
                (".we-asset-tmp-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(sequence.fetch_add(1)));
            if (std::filesystem::create_directory(K_CANDIDATE)) {
                staging.m_path = K_CANDIDATE;
                break;
            }
        }
        if (staging.m_path.empty())
            return fail(ErrorCode::IO_ERROR, "Cannot reserve asset staging directory");
        const auto K_TEMPORARY = staging.m_path / "payload.bin";
        std::FILE* raw {};
#ifdef _WIN32
        if (_wfopen_s(&raw, temporary.c_str(), L"wb") != 0) raw = nullptr;
#else
        raw = std::fopen(K_TEMPORARY.c_str(), "wb");
#endif
        std::unique_ptr<std::FILE, FileCloser> file(raw);
        if (!file) return fail(ErrorCode::IO_ERROR, "Cannot open staged asset asset");
        for (std::size_t offset { 0uz }; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Asset asset cancelled before publication");
            const auto K_COUNT = std::min(K_IO_BLOCK_BYTES, bytes.size() - offset);
            const auto K_BLOCK = bytes.subspan(offset, K_COUNT);
            if (std::fwrite(K_BLOCK.data(), 1, K_BLOCK.size(), file.get()) != K_BLOCK.size())
                return fail(ErrorCode::IO_ERROR, "Asset asset write failed");
            offset += K_COUNT;
        }
        if (std::fflush(file.get()) != 0)
            return fail(ErrorCode::IO_ERROR, "Asset asset flush failed");
#ifdef _WIN32
        const bool flushed = _commit(_fileno(file.get())) == 0;
#else
        const bool K_FLUSHED = fsync(fileno(file.get())) == 0;
#endif
        const bool K_CLOSED = std::fclose(file.release()) == 0;
        if (!K_FLUSHED || !K_CLOSED)
            return fail(ErrorCode::IO_ERROR, "Asset asset durable write failed");
        return staging;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "File staging allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("File staging failed: ") + e.what());
    }
}

Result<void> flush_parent(const std::filesystem::path& destination) {
#ifndef _WIN32
    auto parent = destination.parent_path();
    if (parent.empty()) parent = ".";
    const int K_DESCRIPTOR = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (K_DESCRIPTOR < 0)
        return fail(ErrorCode::IO_ERROR, "Cannot open publication directory for durability");
    const int K_STATUS = fsync(K_DESCRIPTOR);
    close(K_DESCRIPTOR);
    if (K_STATUS != 0) return fail(ErrorCode::IO_ERROR, "Directory flush failed after publication");
#else
    (void)destination;
#endif
    return {};
}

}  // namespace

Result<void> flush_directory_entry(const std::filesystem::path& directory) {
    return flush_parent(directory);
}

Result<std::vector<std::byte>>
read_bytes(const std::filesystem::path& path, std::size_t maximum, const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Asset asset cancelled");
        if (!maximum || maximum > 256uz * 1024 * 1024)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid file byte budget");
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
            return fail(ErrorCode::INVALID_DATA, "Asset asset must be a regular nonsymlink file");
        const auto K_SIZE = std::filesystem::file_size(path);
        if (!K_SIZE || K_SIZE > maximum)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset asset file exceeds budget");
        std::ifstream input(path, std::ios::binary);
        if (!input) return fail(ErrorCode::IO_ERROR, "Cannot open asset asset");
        std::vector<std::byte> bytes(static_cast<std::size_t>(K_SIZE));
        for (std::size_t offset { 0uz }; offset < bytes.size();) {
            if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Asset asset cancelled");
            const auto K_COUNT = std::min(K_IO_BLOCK_BYTES, bytes.size() - offset);
            input.read(reinterpret_cast<char*>(bytes.data() + offset),
                       static_cast<std::streamsize>(K_COUNT));
            if (input.gcount() != static_cast<std::streamsize>(K_COUNT))
                return fail(ErrorCode::IO_ERROR, "Asset asset truncated during read");
            offset += K_COUNT;
        }
        if (input.peek() != std::char_traits<char>::eof() || input.bad())
            return fail(ErrorCode::INVALID_DATA, "Asset asset grew or failed during read");
        return bytes;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset asset allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Asset asset read failed: ") + e.what());
    }
}

Result<std::string> publish_asset(const std::filesystem::path& destination,
                                  std::span<const std::byte> bytes,
                                  const std::stop_token& stop) {
    try {
        auto staged = stage_bytes(destination, bytes, stop);
        if (!staged) return std::unexpected(staged.error());
        Sha256Builder digest;
        for (std::size_t offset { 0uz }; offset < bytes.size();) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Asset publication cancelled");
            const auto K_COUNT = std::min(K_IO_BLOCK_BYTES, bytes.size() - offset);
            digest.update(bytes.subspan(offset, K_COUNT));
            offset += K_COUNT;
        }
        auto hash = digest.finish();
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Asset publication cancelled");
        std::filesystem::create_hard_link(staged->m_path / "payload.bin", destination);
        const auto K_FLUSHED = flush_parent(destination);
        if (!K_FLUSHED) return std::unexpected(K_FLUSHED.error());
        return hash;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Asset publication allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Asset publication failed: ") + e.what());
    }
}

Result<void> replace_manifest(const std::filesystem::path& destination,
                              std::span<const std::byte> bytes,
                              const std::stop_token& stop,
                              bool& replaced) {
    replaced = false;
    try {
        auto staged = stage_bytes(destination, bytes, stop);
        if (!staged) return std::unexpected(staged.error());
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Manifest publication cancelled");
        const auto K_TEMPORARY = staged->m_path / "payload.bin";
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),
                         destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return fail(ErrorCode::IO_ERROR, "Manifest atomic replacement failed");
#else
        std::filesystem::rename(K_TEMPORARY, destination);
#endif
        replaced = true;
        return flush_parent(destination);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Manifest publication allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Manifest publication failed: ") + e.what());
    }
}

}  // namespace world_explorer::asset_files
