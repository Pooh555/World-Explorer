#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using namespace world_explorer;
using json = nlohmann::json;
using FileTree = std::map<std::string, std::string>;
constexpr std::size_t K_HEADER_BYTES { 8uz };
constexpr std::size_t K_MAX_INPUT_BYTES { K_HEADER_BYTES + 4096uz };
constexpr std::size_t K_INDEX_ENTRY_BYTES { 158uz };  // 84 fixed bytes plus 74-byte chunk filename.

void require(bool condition) {
    if (!condition) std::abort();
}

std::string read_bytes(const std::filesystem::path& path) {
    require(std::filesystem::file_size(path) <= 16384);
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input));
    std::string bytes { std::istreambuf_iterator<char>(input), {} };
    require(!input.bad());
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("Cannot write private synthetic bundle input");
}

std::string digest(const std::string& bytes) {
    return sha256(std::as_bytes(std::span(bytes)));
}

FileTree tree(const std::filesystem::path& root) {
    FileTree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).generic_string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_directory(status)) result.emplace(name, "directory");
        else {
            require(std::filesystem::is_regular_file(status));
            result.emplace(name, "file:" + read_bytes(entry.path()));
        }
    }
    return result;
}

void put_integer(std::span<char> bytes, std::uint64_t value) {
    require(bytes.size() <= 8);
    for (auto& byte : bytes) {
        byte = static_cast<char>(value & 255U);
        value >>= 8U;
    }
}

SceneSnapshot synthetic_scene() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 42 };
    scene->m_epoch = { 3 };
    scene->m_revision = { 7 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" }, { "fixture", "bundle-fuzz-v1" } };
    for (std::uint64_t id { 1 }; id <= 2; ++id) {
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { id };
        chunk->m_generation = 4uz;
        chunk->m_pose_revision = { 5 };
        chunk->m_bounds = { { -10, -10, -10 }, { 10, 10, 10 } };
        chunk->m_source_frames = { { 11 }, { 12 } };
        chunk->m_splats.emplace_back();
        chunk->m_splats[0].m_mean = { static_cast<float>(id), 2, 3 };
        chunk->m_splats[0].m_sh_degree = 3;
        chunk->m_splats[0].m_sh[47] = 0.75F;
        scene->m_chunks.emplace(chunk->m_id, chunk);
    }
    return scene;
}

// One bounded edit; all filesystem output paths come from the trusted synthetic template.
void edit(std::string& bytes,
          const std::array<std::uint8_t, K_HEADER_BYTES>& header,
          std::span<const std::uint8_t> payload) {
    const auto offset =
        (static_cast<std::size_t>(header[3]) | (static_cast<std::size_t>(header[4]) << 8U)) %
        (bytes.size() + 1uz);
    const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    switch (header[1] % 5) {
    case 0:  // Overwrite; bytes beyond the old end extend the file.
        bytes.replace(offset, std::min(text.size(), bytes.size() - offset), text);
        break;
    case 1:
        bytes.resize(offset);
        break;
    case 2:
        bytes += text;
        break;
    case 3:
        bytes = text;
        break;
    default:
        if (!bytes.empty()) {
            auto& byte = bytes[offset % bytes.size()];
            byte = std::bit_cast<char>(
                static_cast<std::uint8_t>(static_cast<std::uint8_t>(byte) ^ header[5]));
        }
        break;
    }
}

class FuzzBundle {
  public:
    FuzzBundle() {
        const std::filesystem::path root { WE_FUZZ_SCRATCH_ROOT };
        std::filesystem::create_directories(root);
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(root))) {
            throw std::runtime_error("Fuzz scratch root must be an owned build directory");
        }

        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        bool reserved {};

        for (std::size_t attempt { 0uz }; attempt < 64uz; ++attempt) {
            m_root = root / ("bundle-" + std::to_string(nonce) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(m_root)) {
                reserved = true;
                break;
            }
        }
        if (!reserved) {
            throw std::runtime_error("Cannot reserve a private fuzz bundle directory");
        }

        m_directory = m_root / "synthetic.wexplore";
        require(static_cast<bool>(write_bundle(m_directory, synthetic_scene())));

        write_bytes(m_root / "caller.txt", "unrelated synthetic caller bytes\n");
        for (const auto& entry : std::filesystem::directory_iterator(m_directory))
            m_original.emplace(entry.path().filename().string(), read_bytes(entry.path()));
        m_manifest = json::parse(m_original.at("manifest.json"));
        m_index_name = m_manifest.at("index").get<std::string>();
        m_provenance_name = m_manifest.at("provenance").get<std::string>();
        const auto& index = m_original.at(m_index_name);
        require(index.size() == 16uz + 2uz * K_INDEX_ENTRY_BYTES);

        for (std::size_t number { 0uz }; number < 2uz; ++number) {
            m_chunk_names[number] = index.substr(16uz + number * K_INDEX_ENTRY_BYTES + 84uz, 74uz);
        }

        auto loaded = load_bundle(m_directory, limits());
        require(static_cast<bool>(loaded));
        m_retained = *loaded;
        m_retained_hash = scene_content_hash(**loaded);
    }

    ~FuzzBundle() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
        std::cerr << "synthetic_bundle accepted=" << m_accepted << " rejected=" << m_rejected
                  << " rehashed_index=" << m_rehashed_index
                  << " rehashed_chunk=" << m_rehashed_chunk
                  << " rehashed_provenance=" << m_rehashed_provenance
                  << " cleanup_error=" << error.value() << '\n';
    }

    void exercise(std::span<const std::uint8_t> input) {
        std::array<std::uint8_t, K_HEADER_BYTES> header {};
        std::copy_n(input.begin(), std::min(input.size(), header.size()), header.begin());
        const auto payload = input.subspan(std::min(input.size(), header.size()));
        auto files = m_original;
        auto manifest = m_manifest;
        const auto target = header[0] % 7;
        const bool rehash = (header[2] & 1U) != 0;
        if (target == 0) {
            edit(files.at("manifest.json"), header, payload);
        } else if (target == 1) {
            edit(files.at(m_index_name), header, payload);
            if (rehash) {
                ++m_rehashed_index;
                rehash_index(files, manifest);
            }
        } else if (target == 2 || target == 3) {
            const auto number = static_cast<std::size_t>(target - 2);
            auto& chunk = files.at(m_chunk_names[number]);
            edit(chunk, header, payload);
            if (rehash) {
                ++m_rehashed_chunk;
                auto& index = files.at(m_index_name);
                const auto offset = 16 + number * K_INDEX_ENTRY_BYTES;
                put_integer(std::span(index).subspan(offset + 8, 8), chunk.size());
                index.replace(offset + 16, 64, digest(chunk));
                rehash_index(files, manifest);
            }
        } else if (target == 4) {
            auto& provenance = files.at(m_provenance_name);
            edit(provenance, header, payload);
            if (rehash) {
                ++m_rehashed_provenance;
                manifest["provenance_sha256"] = digest(provenance);
            }
        } else if (target == 5) {
            // Arbitrary JSON values in known fields reach typed manifest admission.
            auto value = json::parse(payload.begin(), payload.end(), nullptr, false);
            if (value.is_discarded())
                value = std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
            constexpr std::array FIELDS {"/schema_version",
                                         "/scene_id",
                                         "/epoch",
                                         "/revision",
                                         "/index",
                                         "/index_sha256",
                                         "/index_bytes",
                                         "/provenance",
                                         "/provenance_sha256",
                                         "/coordinates/handedness",
                                         "/coordinates/world_up",
                                         "/coordinates/camera_axes",
                                         "/coordinates/pose_convention",
                                         "/coordinates/quaternion_order",
                                         "/coordinates/pixel_centers",
                                         "/coordinates/scale_status",
                                         "/appearance/representation",
                                         "/appearance/lighting",
                                         "/appearance/color_domain",
                                         "/appearance/sh_basis",
                                         "/compression"};
            const json::json_pointer field(FIELDS[header[5] % FIELDS.size()]);
            if ((header[2] & 2U) != 0) manifest = value;
            else manifest[field] = std::move(value);
        }
        if (target != 0)
            files.at("manifest.json") =
                manifest.dump(-1, ' ', false, json::error_handler_t::replace);
        for (const auto& [name, bytes] : files) write_bytes(m_directory / name, bytes);
        const auto before = tree(m_root);
        auto budget = limits();
        switch (header[6] % 7) {
        case 1:
            budget.m_max_manifest_bytes = header[7];
            break;
        case 2:
            budget.m_scene.m_max_chunks = header[7] % 4uz;
            break;
        case 3:
            budget.m_scene.m_max_splats = header[7] % 5uz;
            break;
        case 4:
            budget.m_scene.m_max_payload_bytes = static_cast<std::size_t>(header[7]) * 8uz;
            break;
        case 5:
            budget.m_scene.m_max_source_frames = header[7] % 5uz;
            break;
        case 6:
            budget.m_scene.m_max_chunks = std::numeric_limits<std::size_t>::max();
            break;
        default:
            break;
        }
        const auto loaded = load_bundle(m_directory, budget);
        if (loaded) {
            ++m_accepted;
            require(*loaded && static_cast<bool>(validate_scene(**loaded, budget.m_scene)));
            // Loading a later file version must not invalidate a previously admitted snapshot.
            require(scene_content_hash(*m_retained) == m_retained_hash);
            m_retained = *loaded;
            m_retained_hash = scene_content_hash(**loaded);
        } else {
            ++m_rejected;
        }
        require(scene_content_hash(*m_retained) == m_retained_hash);
        require(tree(m_root) == before);
    }

  private:
    static BundleLimits limits() {
        BundleLimits result;
        result.m_max_manifest_bytes = 8192;
        result.m_scene = { .m_max_chunks = 2,
                          .m_max_splats = 8,
                          .m_max_payload_bytes = 8192,
                          .m_max_source_frames = 4 };
        return result;
    }

    void rehash_index(FileTree& files, json& manifest) const {
        manifest["index_sha256"] = digest(files.at(m_index_name));
        manifest["index_bytes"] = files.at(m_index_name).size();
    }

    std::filesystem::path m_root, m_directory;
    FileTree m_original;
    json m_manifest;
    std::string m_index_name, m_provenance_name;
    std::array<std::string, 2> m_chunk_names;
    SceneSnapshot m_retained;
    std::string m_retained_hash;
    std::size_t m_accepted {}, m_rejected {}, m_rehashed_index {}, m_rehashed_chunk {},
        m_rehashed_provenance {};
};

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's required external C entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > K_MAX_INPUT_BYTES) return 0;
    // libFuzzer serializes callbacks; processes reserve independent synthetic file trees.
    static FuzzBundle bundle;
    bundle.exercise({data, size});
    return 0;
}
