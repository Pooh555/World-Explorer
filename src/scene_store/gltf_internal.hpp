#pragma once

#include "world_explorer/scene_store/gltf.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace world_explorer::gltf_detail {

using Json = nlohmann::json;
constexpr std::size_t K_MAX_BYTES { 256uz * 1024uz * 1024uz };

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class Cancelled final : public std::exception {
  public:
    const char* what() const noexcept override { return "GLB operation cancelled"; }
};

inline void cancelled(const std::stop_token& stop) {
    if (stop.stop_requested()) throw Cancelled {};
}

struct Writer {
    std::vector<std::byte> m_data;

    void append(std::span<const std::byte> bytes) {
        require(bytes.size() <= K_MAX_BYTES - m_data.size(), "GLB byte budget exceeded");
        m_data.insert(m_data.end(), bytes.begin(), bytes.end());
    }

    void integer(std::uint64_t value, std::size_t count = 4uz) {
        require(count == 8uz || value <= std::numeric_limits<std::uint32_t>::max(),
                "GLB integer overflow");
        require(count <= K_MAX_BYTES - m_data.size(), "GLB byte budget exceeded");
        for (std::size_t i { 0uz }; i < count; ++i)
            m_data.push_back(static_cast<std::byte>((value >> (i * 8uz)) & 255U));
    }

    void real(float value) { integer(std::bit_cast<std::uint32_t>(value)); }

    void align() {
        while (m_data.size() % 4uz) integer(0, 1uz);
    }
};

struct Reader {
    std::span<const std::byte> m_data;
    std::size_t m_position {};

    std::span<const std::byte> take(std::size_t count) {
        require(count <= m_data.size() - m_position, "Truncated GLB data");
        const auto K_RESULT = m_data.subspan(m_position, count);
        m_position += count;
        return K_RESULT;
    }

    std::uint64_t integer(std::size_t count = 4uz) {
        const auto K_BYTES = take(count);
        std::uint64_t value {};
        for (std::size_t i { 0uz }; i < count; ++i)
            value |= std::to_integer<std::uint64_t>(K_BYTES[i]) << (i * 8uz);
        return value;
    }

    float real() { return std::bit_cast<float>(static_cast<std::uint32_t>(integer())); }
};

struct Image {
    std::uint32_t m_width {}, m_height {};
    std::vector<std::byte> m_rgba;
};

std::vector<std::byte> encode_png(const Image& image, const std::stop_token& stop);
Image decode_png(std::span<const std::byte> bytes, const std::stop_token& stop);
Json parse(std::string_view text);
Json chunk_header(const SceneChunk& chunk);
SceneChunk read_header(const Json& document);
Json document_base(const SceneState& scene);
std::size_t count(const Json& value, std::size_t maximum);
std::array<std::uint32_t, 2> image_extent(const BakedMeshAppearance& baked);

}  // namespace world_explorer::gltf_detail
