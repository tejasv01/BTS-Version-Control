#include "hash.hpp"

#include <openssl/evp.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace bts {

namespace {

struct EvpMdCtxDeleter {
    void operator()(EVP_MD_CTX* ctx) const {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
        }
    }
};

using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

} // namespace

std::string sha256_raw(const void* data, std::size_t size) {
    EvpMdCtxPtr ctx(EVP_MD_CTX_new());
    if (!ctx) {
        throw std::runtime_error("Failed to allocate OpenSSL EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("EVP_DigestInit_ex failed for SHA-256");
    }

    if (size > 0 && EVP_DigestUpdate(ctx.get(), data, size) != 1) {
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (EVP_DigestFinal_ex(ctx.get(), md_value, &md_len) != 1) {
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    return std::string(reinterpret_cast<const char*>(md_value), md_len);
}

std::string sha256_raw(const std::string& data) {
    return sha256_raw(data.data(), data.size());
}

std::string raw_to_hex(const std::string& raw) {
    static constexpr char hex_digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(raw.size() * 2);
    for (unsigned char byte : raw) {
        hex.push_back(hex_digits[(byte >> 4) & 0x0F]);
        hex.push_back(hex_digits[byte & 0x0F]);
    }
    return hex;
}

std::string sha256(const void* data, std::size_t size) {
    std::string raw = sha256_raw(data, size);
    return raw_to_hex(raw);
}

std::string sha256(const std::string& data) {
    return sha256(data.data(), data.size());
}

std::string hex_to_raw(const std::string& hex) {
    // Issue #7: when used for SHA-256 object hashes, require exactly 64 hex characters.
    // We enforce this here because hex_to_raw is only called for SHA-256 digests in bts.
    if (hex.size() != 64) {
        throw std::invalid_argument(
            "hex_to_raw requires exactly 64 hex characters (SHA-256); got " +
            std::to_string(hex.size()));
    }

    auto parse_nibble = [](char c) -> unsigned char {
        if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
        throw std::invalid_argument("Invalid hex character in string");
    };

    std::string raw;
    raw.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        unsigned char high = parse_nibble(hex[i]);
        unsigned char low = parse_nibble(hex[i + 1]);
        raw.push_back(static_cast<char>((high << 4) | low));
    }
    return raw;
}

bool is_valid_sha256_hex(const std::string& hex) {
    if (hex.size() != 64) {
        return false;
    }
    for (char c : hex) {
        bool is_hex = (c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F');
        if (!is_hex) {
            return false;
        }
    }
    return true;
}

} // namespace bts
