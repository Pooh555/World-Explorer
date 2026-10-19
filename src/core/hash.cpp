#include "world_explorer/core/hash.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace world_explorer {
namespace {

using DigestOwner = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

DigestOwner start_digest() {
    DigestOwner digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("cannot initialize SHA-256");
    }
    return digest;
}

std::string finish_digest(EVP_MD_CTX* digest) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes {};
    unsigned length {};
    if (EVP_DigestFinal_ex(digest, bytes.data(), &length) != 1) {
        throw std::runtime_error("cannot finalize SHA-256");
    }
    constexpr char K_HEX[] { "0123456789abcdef" };
    std::string result;
    for (unsigned i { 0 }; i < length; ++i) {
        result.push_back(K_HEX[static_cast<unsigned>(bytes[i]) >> 4U]);
        result.push_back(K_HEX[static_cast<unsigned>(bytes[i]) & 15U]);
    }
    return result;
}

}  // namespace

struct Sha256Builder::Implementation {
    DigestOwner m_digest { start_digest() };
    std::array<std::byte, 16384> m_pending {};
    std::size_t m_pending_bytes {};
    bool m_finished {};

    void flush() {
        if (m_pending_bytes &&
            EVP_DigestUpdate(m_digest.get(), m_pending.data(), m_pending_bytes) != 1) {
            throw std::runtime_error("cannot update SHA-256 builder");
        }
        m_pending_bytes = 0;
    }
};

Sha256Builder::Sha256Builder() : m_impl(std::make_unique<Implementation>()) { }

Sha256Builder::~Sha256Builder() = default;

void Sha256Builder::update(std::span<const std::byte> bytes) {
    if (m_impl->m_finished) {
        throw std::runtime_error("cannot update SHA-256 builder");
    }
    while (!bytes.empty()) {
        if (!m_impl->m_pending_bytes && bytes.size() >= m_impl->m_pending.size()) {
            if (EVP_DigestUpdate(m_impl->m_digest.get(), bytes.data(), bytes.size()) != 1) {
                throw std::runtime_error("cannot update SHA-256 builder");
            }
            return;
        }
        const auto K_COPIED =
            std::min(bytes.size(), m_impl->m_pending.size() - m_impl->m_pending_bytes);
        std::memcpy(m_impl->m_pending.data() + m_impl->m_pending_bytes, bytes.data(), K_COPIED);
        m_impl->m_pending_bytes += K_COPIED;
        bytes = bytes.subspan(K_COPIED);
        if (m_impl->m_pending_bytes == m_impl->m_pending.size()) {
            m_impl->flush();
        }
    }
}

std::string Sha256Builder::finish() {
    if (m_impl->m_finished) {
        throw std::runtime_error("SHA-256 builder already finalized");
    }
    m_impl->m_finished = true;
    m_impl->flush();
    return finish_digest(m_impl->m_digest.get());
}

std::string sha256(std::span<const std::byte> bytes) {
    auto digest = start_digest();
    if (EVP_DigestUpdate(digest.get(), bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("cannot update SHA-256");
    }
    return finish_digest(digest.get());
}

Result<std::string>
sha256_file(const std::filesystem::path& path, std::size_t max_bytes, const std::stop_token& stop) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return fail(ErrorCode::IO_ERROR, "cannot open hash input");
        }
        auto digest = start_digest();
        std::array<char, 65536> buffer {};
        std::size_t total {};
        while (input) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Hash verification cancelled");
            }
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto K_COUNT = static_cast<std::size_t>(input.gcount());
            if (K_COUNT > max_bytes - total) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "hash input exceeds budget");
            }
            total += K_COUNT;
            if (EVP_DigestUpdate(digest.get(), buffer.data(), K_COUNT) != 1) {
                return fail(ErrorCode::IO_ERROR, "SHA-256 failed");
            }
        }
        if (!input.eof()) {
            return fail(ErrorCode::IO_ERROR, "hash input read failed");
        }
        return finish_digest(digest.get());
    } catch (const std::exception& exception) {
        return fail(ErrorCode::IO_ERROR, exception.what());
    }
}

}  // namespace world_explorer
