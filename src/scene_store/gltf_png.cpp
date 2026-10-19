#include "gltf_internal.hpp"

#include <algorithm>

namespace world_explorer::gltf_detail {
namespace {

std::uint32_t crc(std::span<const std::byte> bytes) {
    static constexpr auto K_TABLE = [] {
        std::array<std::uint32_t, 256> table {};
        for (std::size_t i { 0uz }; i < table.size(); ++i) {
            auto value = static_cast<std::uint32_t>(i);
            for (unsigned bit { 0 }; bit < 8; ++bit)
                value = (value >> 1U) ^ ((value & 1U) ? 0xedb88320U : 0U);
            table[i] = value;
        }
        return table;
    }();
    std::uint32_t value { 0xffffffffU };
    for (auto byte : bytes)
        value = K_TABLE[(value ^ std::to_integer<std::uint32_t>(byte)) & 255U] ^ (value >> 8U);
    return value ^ 0xffffffffU;
}

std::uint32_t adler(std::span<const std::byte> bytes) {
    std::uint32_t a { 1 }, b {};
    for (std::size_t begin { 0uz }; begin < bytes.size(); begin += 5552uz) {
        for (auto value : bytes.subspan(begin, std::min(5552uz, bytes.size() - begin))) {
            a += std::to_integer<std::uint32_t>(value);
            b += a;
        }
        a %= 65521U;
        b %= 65521U;
    }
    return (b << 16U) | a;
}

void big(Writer& out, std::uint32_t value) {
    for (unsigned i { 4 }; i > 0; --i) out.integer((value >> ((i - 1U) * 8U)) & 255U, 1uz);
}

std::uint32_t big(Reader& input) {
    std::uint32_t result {};
    for (auto byte : input.take(4uz))
        result = (result << 8U) | std::to_integer<std::uint32_t>(byte);
    return result;
}

void chunk(Writer& out, std::string_view kind, std::span<const std::byte> payload) {
    big(out, static_cast<std::uint32_t>(payload.size()));
    const auto K_BEGIN = out.m_data.size();
    out.append(std::as_bytes(std::span(kind)));
    out.append(payload);
    big(out, crc(std::span(out.m_data).subspan(K_BEGIN)));
}

constexpr std::string_view K_SIGNATURE { "\x89PNG\r\n\x1a\n", 8uz };

}  // namespace

std::vector<std::byte> encode_png(const Image& image, const std::stop_token& stop) {
    cancelled(stop);
    require(image.m_width && image.m_height && image.m_width <= 4096 && image.m_height <= 4096 &&
                image.m_rgba.size() ==
                    static_cast<std::size_t>(image.m_width) * image.m_height * 4uz,
            "Invalid PNG dimensions");
    Writer scan;
    for (std::size_t y { 0uz }; y < image.m_height; ++y) {
        cancelled(stop);
        scan.integer(0, 1uz);
        scan.append(std::span(image.m_rgba).subspan(y * image.m_width * 4uz, image.m_width * 4uz));
    }
    Writer zlib;
    zlib.integer(0x78, 1uz);
    zlib.integer(0x01, 1uz);
    for (std::size_t begin { 0uz }; begin < scan.m_data.size();) {
        cancelled(stop);
        const auto K_LENGTH = std::min(65535uz, scan.m_data.size() - begin);
        zlib.integer(begin + K_LENGTH == scan.m_data.size() ? 1 : 0, 1uz);
        zlib.integer(K_LENGTH, 2uz);
        zlib.integer(K_LENGTH ^ 65535uz, 2uz);
        zlib.append(std::span(scan.m_data).subspan(begin, K_LENGTH));
        begin += K_LENGTH;
    }
    big(zlib, adler(scan.m_data));
    Writer out;
    out.append(std::as_bytes(std::span(K_SIGNATURE)));
    Writer header;
    big(header, image.m_width);
    big(header, image.m_height);
    for (auto value : { 8U, 6U, 0U, 0U, 0U }) header.integer(value, 1uz);
    chunk(out, "IHDR", header.m_data);
    chunk(out, "IDAT", zlib.m_data);
    chunk(out, "IEND", {});
    return std::move(out.m_data);
}

Image decode_png(std::span<const std::byte> bytes, const std::stop_token& stop) {
    cancelled(stop);
    Reader input { bytes };
    require(std::ranges::equal(input.take(8uz), std::as_bytes(std::span(K_SIGNATURE))),
            "Invalid PNG signature");
    std::array<std::span<const std::byte>, 3> blocks;
    const std::array<std::string_view, 3> K_KINDS { "IHDR", "IDAT", "IEND" };
    for (std::size_t i { 0uz }; i < 3uz; ++i) {
        const auto K_LENGTH = big(input);
        const auto K_BEGIN = input.m_position;
        require(std::ranges::equal(input.take(4uz), std::as_bytes(std::span(K_KINDS[i]))),
                "Unsupported PNG chunk layout");
        blocks[i] = input.take(K_LENGTH);
        const auto K_CRC = crc(bytes.subspan(K_BEGIN, static_cast<std::size_t>(K_LENGTH) + 4uz));
        require(big(input) == K_CRC, "PNG CRC mismatch");
    }
    require(input.m_position == bytes.size() && blocks[0].size() == 13uz && blocks[2].empty(),
            "Invalid PNG lengths");
    Reader header { blocks[0] };
    Image image;
    image.m_width = big(header);
    image.m_height = big(header);
    require(image.m_width && image.m_height && image.m_width <= 4096 && image.m_height <= 4096,
            "PNG dimensions exceed budget");
    for (auto value : { 8U, 6U, 0U, 0U, 0U })
        require(header.integer(1uz) == value, "Unsupported PNG format");
    const auto K_ROW = static_cast<std::size_t>(image.m_width) * 4uz;
    const auto K_SIZE = (K_ROW + 1uz) * image.m_height;
    Reader zlib { blocks[1] };
    require(zlib.integer(2uz) == 0x0178, "Unsupported PNG deflate profile");
    Writer scan;
    bool final = false;
    while (!final) {
        cancelled(stop);
        const auto K_FLAGS = zlib.integer(1uz);
        require(K_FLAGS <= 1, "Only stored deflate blocks supported");
        final = K_FLAGS == 1;
        const auto K_LENGTH = zlib.integer(2uz), K_COMPLEMENT = zlib.integer(2uz);
        require((K_LENGTH ^ K_COMPLEMENT) == 65535 && K_LENGTH <= K_SIZE - scan.m_data.size(),
                "Invalid PNG deflate length");
        scan.append(zlib.take(static_cast<std::size_t>(K_LENGTH)));
    }
    require(scan.m_data.size() == K_SIZE && big(zlib) == adler(scan.m_data) &&
                zlib.m_position == zlib.m_data.size(),
            "PNG decoded size/checksum mismatch");
    image.m_rgba.reserve(K_ROW * image.m_height);
    for (std::size_t y { 0uz }; y < image.m_height; ++y) {
        cancelled(stop);
        require(scan.m_data[y * (K_ROW + 1uz)] == std::byte {}, "Unsupported PNG row filter");
        const auto K_ROW_BYTES = std::span(scan.m_data).subspan(y * (K_ROW + 1uz) + 1uz, K_ROW);
        image.m_rgba.insert(image.m_rgba.end(), K_ROW_BYTES.begin(), K_ROW_BYTES.end());
    }
    return image;
}

}  // namespace world_explorer::gltf_detail
