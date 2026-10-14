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
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>

namespace {
using namespace world_explorer;
using json = nlohmann::json;
using file_tree = std::map<std::string, std::string>;
constexpr std::size_t HEADER_BYTES = 8;
constexpr std::size_t MAX_INPUT_BYTES = HEADER_BYTES + 4096;
constexpr std::size_t INDEX_ENTRY_BYTES = 158; // 84 fixed bytes plus 74-byte chunk filename.

void require(bool condition) {
    if (!condition)
        std::abort();
}
std::string read_bytes(const std::filesystem::path& path) {
    require(std::filesystem::file_size(path) <= 16384);
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input));
    std::string bytes{std::istreambuf_iterator<char>(input), {}};
    require(!input.bad());
    return bytes;
}
void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Cannot write private synthetic bundle input");
}
std::string digest(const std::string& bytes) { return sha256(std::as_bytes(std::span(bytes))); }
file_tree tree(const std::filesystem::path& root) {
    file_tree result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).generic_string();
        const auto status = entry.symlink_status();
        if (std::filesystem::is_directory(status))
            result.emplace(name, "directory");
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

scene_snapshot synthetic_scene() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {42};
    scene->epoch = {3};
    scene->revision = {7};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"}, {"fixture", "bundle-fuzz-v1"}};
    for (std::uint64_t id = 1; id <= 2; ++id) {
        auto chunk = std::make_shared<scene_chunk>();
        chunk->id = {id};
        chunk->generation = 4;
        chunk->pose_revision = {5};
        chunk->bounds = {{-10, -10, -10}, {10, 10, 10}};
        chunk->source_frames = {{11}, {12}};
        chunk->splats.emplace_back();
        chunk->splats[0].mean = {static_cast<float>(id), 2, 3};
        chunk->splats[0].sh_degree = 3;
        chunk->splats[0].sh[47] = .75F;
        scene->chunks.emplace(chunk->id, chunk);
    }
    return scene;
}

// One bounded edit; all filesystem output paths come from the trusted synthetic template.
void edit(std::string& bytes, const std::array<std::uint8_t, HEADER_BYTES>& header,
          std::span<const std::uint8_t> payload) {
    const auto offset =
        (std::size_t{header[3]} | (std::size_t{header[4]} << 8U)) % (bytes.size() + 1);
    const std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
    switch (header[1] % 5) {
    case 0: // Overwrite; bytes beyond the old end extend the file.
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

class fuzz_bundle {
  public:
    fuzz_bundle() {
        const std::filesystem::path root(WE_FUZZ_SCRATCH_ROOT);
        std::filesystem::create_directories(root);
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(root)))
            throw std::runtime_error("Fuzz scratch root must be an owned build directory");
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        bool reserved{};
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            root_ = root / ("bundle-" + std::to_string(nonce) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(root_)) {
                reserved = true;
                break;
            }
        }
        if (!reserved)
            throw std::runtime_error("Cannot reserve a private fuzz bundle directory");
        directory_ = root_ / "synthetic.wexplore";
        require(static_cast<bool>(write_bundle(directory_, synthetic_scene())));
        write_bytes(root_ / "caller.txt", "unrelated synthetic caller bytes\n");
        for (const auto& entry : std::filesystem::directory_iterator(directory_))
            original_.emplace(entry.path().filename().string(), read_bytes(entry.path()));
        manifest_ = json::parse(original_.at("manifest.json"));
        index_name_ = manifest_.at("index").get<std::string>();
        provenance_name_ = manifest_.at("provenance").get<std::string>();
        const auto& index = original_.at(index_name_);
        require(index.size() == 16 + 2 * INDEX_ENTRY_BYTES);
        for (std::size_t number = 0; number < 2; ++number)
            chunk_names_[number] = index.substr(16 + number * INDEX_ENTRY_BYTES + 84, 74);
        auto loaded = load_bundle(directory_, limits());
        require(static_cast<bool>(loaded));
        retained_ = *loaded;
        retained_hash_ = scene_content_hash(**loaded);
    }
    ~fuzz_bundle() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::cerr << "synthetic_bundle accepted=" << accepted_ << " rejected=" << rejected_
                  << " rehashed_index=" << rehashed_index_ << " rehashed_chunk=" << rehashed_chunk_
                  << " rehashed_provenance=" << rehashed_provenance_
                  << " cleanup_error=" << error.value() << '\n';
    }
    void exercise(std::span<const std::uint8_t> input) {
        std::array<std::uint8_t, HEADER_BYTES> header{};
        std::copy_n(input.begin(), std::min(input.size(), header.size()), header.begin());
        const auto payload = input.subspan(std::min(input.size(), header.size()));
        auto files = original_;
        auto manifest = manifest_;
        const auto target = header[0] % 7;
        const bool rehash = (header[2] & 1U) != 0;
        if (target == 0) {
            edit(files.at("manifest.json"), header, payload);
        } else if (target == 1) {
            edit(files.at(index_name_), header, payload);
            if (rehash) {
                ++rehashed_index_;
                rehash_index(files, manifest);
            }
        } else if (target == 2 || target == 3) {
            const auto number = static_cast<std::size_t>(target - 2);
            auto& chunk = files.at(chunk_names_[number]);
            edit(chunk, header, payload);
            if (rehash) {
                ++rehashed_chunk_;
                auto& index = files.at(index_name_);
                const auto offset = 16 + number * INDEX_ENTRY_BYTES;
                put_integer(std::span(index).subspan(offset + 8, 8), chunk.size());
                index.replace(offset + 16, 64, digest(chunk));
                rehash_index(files, manifest);
            }
        } else if (target == 4) {
            auto& provenance = files.at(provenance_name_);
            edit(provenance, header, payload);
            if (rehash) {
                ++rehashed_provenance_;
                manifest["provenance_sha256"] = digest(provenance);
            }
        } else if (target == 5) {
            // Arbitrary JSON values in known fields reach typed manifest admission.
            auto value = json::parse(payload.begin(), payload.end(), nullptr, false);
            if (value.is_discarded())
                value = std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
            constexpr std::array FIELDS{"/schema_version",
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
            if ((header[2] & 2U) != 0)
                manifest = value;
            else
                manifest[field] = std::move(value);
        }
        if (target != 0)
            files.at("manifest.json") =
                manifest.dump(-1, ' ', false, json::error_handler_t::replace);
        for (const auto& [name, bytes] : files)
            write_bytes(directory_ / name, bytes);
        const auto before = tree(root_);
        auto budget = limits();
        switch (header[6] % 7) {
        case 1:
            budget.max_manifest_bytes = header[7];
            break;
        case 2:
            budget.scene.max_chunks = header[7] % 4;
            break;
        case 3:
            budget.scene.max_splats = header[7] % 5;
            break;
        case 4:
            budget.scene.max_payload_bytes = std::size_t{header[7]} * 8;
            break;
        case 5:
            budget.scene.max_source_frames = header[7] % 5;
            break;
        case 6:
            budget.scene.max_chunks = std::numeric_limits<std::size_t>::max();
            break;
        default:
            break;
        }
        const auto loaded = load_bundle(directory_, budget);
        if (loaded) {
            ++accepted_;
            require(*loaded && static_cast<bool>(validate_scene(**loaded, budget.scene)));
            // Loading a later file version must not invalidate a previously admitted snapshot.
            require(scene_content_hash(*retained_) == retained_hash_);
            retained_ = *loaded;
            retained_hash_ = scene_content_hash(**loaded);
        } else {
            ++rejected_;
        }
        require(scene_content_hash(*retained_) == retained_hash_);
        require(tree(root_) == before);
    }

  private:
    static bundle_limits limits() {
        bundle_limits result;
        result.max_manifest_bytes = 8192;
        result.scene = {
            .max_chunks = 2, .max_splats = 8, .max_payload_bytes = 8192, .max_source_frames = 4};
        return result;
    }
    void rehash_index(file_tree& files, json& manifest) const {
        manifest["index_sha256"] = digest(files.at(index_name_));
        manifest["index_bytes"] = files.at(index_name_).size();
    }
    std::filesystem::path root_, directory_;
    file_tree original_;
    json manifest_;
    std::string index_name_, provenance_name_;
    std::array<std::string, 2> chunk_names_;
    scene_snapshot retained_;
    std::string retained_hash_;
    std::size_t accepted_{}, rejected_{}, rehashed_index_{}, rehashed_chunk_{},
        rehashed_provenance_{};
};
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's required external C entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > MAX_INPUT_BYTES)
        return 0;
    // libFuzzer serializes callbacks; processes reserve independent synthetic file trees.
    static fuzz_bundle bundle;
    bundle.exercise({data, size});
    return 0;
}
