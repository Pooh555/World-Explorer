#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <stdexcept>
namespace world_explorer {
namespace {
using digest_owner = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
digest_owner start_digest() {
    digest_owner digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("cannot initialize SHA-256");
    return digest;
}
std::string finish_digest(EVP_MD_CTX* digest) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned length{};
    if (EVP_DigestFinal_ex(digest, bytes.data(), &length) != 1)
        throw std::runtime_error("cannot finalize SHA-256");
    constexpr char HEX[] = "0123456789abcdef";
    std::string result;
    for (unsigned i = 0; i < length; ++i) {
        result.push_back(HEX[static_cast<unsigned>(bytes[i]) >> 4U]);
        result.push_back(HEX[static_cast<unsigned>(bytes[i]) & 15U]);
    }
    return result;
}
} // namespace
struct sha256_builder::implementation {
    digest_owner digest{start_digest()};
    std::array<std::byte, 16384> pending{};
    std::size_t pending_bytes{};
    bool finished{};
    void flush() {
        if (pending_bytes && EVP_DigestUpdate(digest.get(), pending.data(), pending_bytes) != 1)
            throw std::runtime_error("cannot update SHA-256 builder");
        pending_bytes = 0;
    }
};
sha256_builder::sha256_builder() : impl_(std::make_unique<implementation>()) {}
sha256_builder::~sha256_builder() = default;
void sha256_builder::update(std::span<const std::byte> bytes) {
    if (impl_->finished)
        throw std::runtime_error("cannot update SHA-256 builder");
    while (!bytes.empty()) {
        if (!impl_->pending_bytes && bytes.size() >= impl_->pending.size()) {
            if (EVP_DigestUpdate(impl_->digest.get(), bytes.data(), bytes.size()) != 1)
                throw std::runtime_error("cannot update SHA-256 builder");
            return;
        }
        const auto copied = std::min(bytes.size(), impl_->pending.size() - impl_->pending_bytes);
        std::memcpy(impl_->pending.data() + impl_->pending_bytes, bytes.data(), copied);
        impl_->pending_bytes += copied;
        bytes = bytes.subspan(copied);
        if (impl_->pending_bytes == impl_->pending.size())
            impl_->flush();
    }
}
std::string sha256_builder::finish() {
    if (impl_->finished)
        throw std::runtime_error("SHA-256 builder already finalized");
    impl_->finished = true;
    impl_->flush();
    return finish_digest(impl_->digest.get());
}
std::string sha256(std::span<const std::byte> bytes) {
    auto digest = start_digest();
    if (EVP_DigestUpdate(digest.get(), bytes.data(), bytes.size()) != 1)
        throw std::runtime_error("cannot update SHA-256");
    return finish_digest(digest.get());
}
result<std::string> sha256_file(const std::filesystem::path& path, std::size_t max_bytes,
                                const std::stop_token& stop) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return fail(error_code::IO_ERROR, "cannot open hash input");
        auto digest = start_digest();
        std::array<char, 65536> buffer{};
        std::size_t total{};
        while (input) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "Hash verification cancelled");
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > max_bytes - total)
                return fail(error_code::RESOURCE_EXHAUSTED, "hash input exceeds budget");
            total += count;
            if (EVP_DigestUpdate(digest.get(), buffer.data(), count) != 1)
                return fail(error_code::IO_ERROR, "SHA-256 failed");
        }
        if (!input.eof())
            return fail(error_code::IO_ERROR, "hash input read failed");
        return finish_digest(digest.get());
    } catch (const std::exception& exception) {
        return fail(error_code::IO_ERROR, exception.what());
    }
}
} // namespace world_explorer
