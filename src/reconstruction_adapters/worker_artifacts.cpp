#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"
#include "artifact_io.hpp"
#include "world_explorer/core/hash.hpp"
#include <new>

namespace world_explorer {
result<verified_worker_stage> verify_committed_stage(const std::filesystem::path& directory,
                                                     const std::string& key,
                                                     const std::stop_token& stop) {
    try {
        constexpr std::size_t MAX_MANIFEST = std::size_t{16} * 1024 * 1024;
        constexpr std::uint64_t MAX_BYTES = 32ULL * 1024 * 1024 * 1024;
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Stage verification cancelled");
        if (!adapter_detail::valid_digest(key))
            return fail(error_code::INVALID_ARGUMENT, "Invalid committed stage key");
        verified_worker_stage stage;
        stage.directory = std::filesystem::canonical(directory);
        const auto completion_file = stage.directory / "completion.json";
        const auto initial_hash = sha256_file(completion_file, MAX_MANIFEST, stop);
        if (!initial_hash)
            return std::unexpected(initial_hash.error());
        stage.completion_sha256 = *initial_hash;
        const auto manifest =
            adapter_detail::read_json(completion_file, MAX_MANIFEST, *initial_hash);
        const auto& files = manifest.at("files");
        if (manifest.at("schema_version") != 1 || manifest.at("status") != "complete" ||
            manifest.at("key") != key || !files.is_array() || files.empty() || files.size() > 4096)
            return fail(error_code::INVALID_DATA, "Invalid committed stage inventory");
        std::uint64_t total{};
        for (const auto& entry : files) {
            const auto name = entry.at("path").get<std::string>();
            const auto bytes = adapter_detail::unsigned_value(entry.at("bytes"));
            const auto digest = entry.at("sha256").get<std::string>();
            if (name == "completion.json" || !adapter_detail::valid_digest(digest) ||
                !stage.assets.emplace(name, verified_worker_asset{bytes, digest}).second)
                return fail(error_code::INVALID_DATA, "Invalid or duplicate stage asset");
            if (bytes > MAX_BYTES - total)
                return fail(error_code::RESOURCE_EXHAUSTED, "Stage exceeds 32 GiB");
            total += bytes;
            const auto path = adapter_detail::contained_file(stage.directory, name);
            if (std::filesystem::file_size(path) != bytes)
                return fail(error_code::INVALID_DATA, "Stage asset byte count changed: " + name);
            const auto actual = sha256_file(path, static_cast<std::size_t>(bytes), stop);
            if (!actual)
                return std::unexpected(actual.error());
            if (*actual != digest)
                return fail(error_code::INVALID_DATA, "Stage asset checksum changed: " + name);
        }
        std::size_t actual_files{}, entries{};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(stage.directory)) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Stage verification cancelled");
            if (++entries > 8192)
                return fail(error_code::RESOURCE_EXHAUSTED, "Stage entry count exceeded");
            if (entry.is_symlink())
                return fail(error_code::INVALID_DATA, "Stage contains a symlink");
            if (entry.is_regular_file() && entry.path() != completion_file) {
                const auto relative =
                    entry.path().lexically_relative(stage.directory).generic_u8string();
                if (!stage.assets.contains(std::string(relative.begin(), relative.end())))
                    return fail(error_code::INVALID_DATA, "Uncommitted file in stage");
                ++actual_files;
            }
        }
        const auto final_hash = sha256_file(completion_file, MAX_MANIFEST, stop);
        if (!final_hash)
            return std::unexpected(final_hash.error());
        if (actual_files != files.size() || *final_hash != *initial_hash)
            return fail(error_code::INVALID_DATA, "Stage inventory changed during verification");
        return stage;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Stage verification allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Stage verification failed: ") + exception.what());
    }
}
} // namespace world_explorer
