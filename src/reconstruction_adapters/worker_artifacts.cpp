#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"

#include "artifact_io.hpp"
#include "world_explorer/core/hash.hpp"

#include <new>

namespace world_explorer {

Result<VerifiedWorkerStage> verify_committed_stage(const std::filesystem::path& directory,
                                                   const std::string& key,
                                                   const std::stop_token& stop,
                                                   WorkerStageLimits limits) {
    try {
        constexpr std::size_t K_MAX_MANIFEST { 16uz * 1024uz * 1024uz };
        constexpr std::uint64_t K_MAX_BYTES { 32ULL * 1024 * 1024 * 1024 };
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Stage verification cancelled");
        if (!limits.m_maximum_bytes || limits.m_maximum_bytes > K_MAX_BYTES ||
            !limits.m_maximum_files || limits.m_maximum_files > 4096 ||
            !limits.m_maximum_manifest_bytes || limits.m_maximum_manifest_bytes > K_MAX_MANIFEST)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid committed stage limits");
        if (!adapter_detail::valid_digest(key))
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid committed stage key");
        VerifiedWorkerStage stage;
        stage.m_directory = std::filesystem::canonical(directory);
        const auto K_COMPLETION_FILE = stage.m_directory / "completion.json";
        const auto K_INITIAL_HASH =
            sha256_file(K_COMPLETION_FILE, limits.m_maximum_manifest_bytes, stop);
        if (!K_INITIAL_HASH) return std::unexpected(K_INITIAL_HASH.error());
        stage.m_completion_sha256 = *K_INITIAL_HASH;
        const auto K_MANIFEST = adapter_detail::read_json(
            K_COMPLETION_FILE, limits.m_maximum_manifest_bytes, *K_INITIAL_HASH);
        const auto& files = K_MANIFEST.at("files");
        if (K_MANIFEST.at("schema_version") != 1 || K_MANIFEST.at("status") != "complete" ||
            K_MANIFEST.at("key") != key || !files.is_array() || files.empty() ||
            files.size() > limits.m_maximum_files)
            return fail(ErrorCode::INVALID_DATA, "Invalid committed stage inventory");
        std::uint64_t total {};
        for (const auto& entry : files) {
            const auto K_NAME = entry.at("path").get<std::string>();
            const auto K_BYTES = adapter_detail::unsigned_value(entry.at("bytes"));
            const auto K_DIGEST = entry.at("sha256").get<std::string>();
            if (K_NAME == "completion.json" || !adapter_detail::valid_digest(K_DIGEST) ||
                !stage.m_assets.emplace(K_NAME, VerifiedWorkerAsset { K_BYTES, K_DIGEST }).second)
                return fail(ErrorCode::INVALID_DATA, "Invalid or duplicate stage asset");
            if (K_BYTES > limits.m_maximum_bytes - total)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Stage exceeds byte budget");
            total += K_BYTES;
            const auto K_PATH = adapter_detail::contained_file(stage.m_directory, K_NAME);
            if (std::filesystem::file_size(K_PATH) != K_BYTES)
                return fail(ErrorCode::INVALID_DATA, "Stage asset byte count changed: " + K_NAME);
            const auto K_ACTUAL = sha256_file(K_PATH, static_cast<std::size_t>(K_BYTES), stop);
            if (!K_ACTUAL) return std::unexpected(K_ACTUAL.error());
            if (*K_ACTUAL != K_DIGEST)
                return fail(ErrorCode::INVALID_DATA, "Stage asset checksum changed: " + K_NAME);
        }
        std::size_t actual_files {}, entries {};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(stage.m_directory)) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Stage verification cancelled");
            if (++entries > 8192)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Stage entry count exceeded");
            if (entry.is_symlink())
                return fail(ErrorCode::INVALID_DATA, "Stage contains a symlink");
            if (entry.is_regular_file() && entry.path() != K_COMPLETION_FILE) {
                const auto K_RELATIVE =
                    entry.path().lexically_relative(stage.m_directory).generic_u8string();
                if (!stage.m_assets.contains(std::string(K_RELATIVE.begin(), K_RELATIVE.end())))
                    return fail(ErrorCode::INVALID_DATA, "Uncommitted file in stage");
                ++actual_files;
            }
        }
        const auto K_FINAL_HASH =
            sha256_file(K_COMPLETION_FILE, limits.m_maximum_manifest_bytes, stop);
        if (!K_FINAL_HASH) return std::unexpected(K_FINAL_HASH.error());
        if (actual_files != files.size() || *K_FINAL_HASH != *K_INITIAL_HASH)
            return fail(ErrorCode::INVALID_DATA, "Stage inventory changed during verification");
        return stage;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Stage verification allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Stage verification failed: ") + exception.what());
    }
}

}  // namespace world_explorer
