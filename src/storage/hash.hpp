#pragma once

#include <cstddef>
#include <string>

namespace bts {

/**
 * @brief Computes SHA-256 digest of binary buffer and returns a 64-character lowercase hex string.
 * @param data Pointer to buffer.
 * @param size Length of buffer in bytes.
 * @return 64-character lowercase hex string representation of SHA-256.
 */
std::string sha256(const void* data, std::size_t size);

/**
 * @brief Computes SHA-256 digest of a std::string and returns a 64-character lowercase hex string.
 * @param data String data to hash.
 * @return 64-character lowercase hex string.
 */
std::string sha256(const std::string& data);

/**
 * @brief Computes SHA-256 digest of binary buffer and returns 32 raw digest bytes.
 * @param data Pointer to buffer.
 * @param size Length of buffer in bytes.
 * @return 32-byte raw binary digest.
 */
std::string sha256_raw(const void* data, std::size_t size);

/**
 * @brief Computes SHA-256 digest of a std::string and returns 32 raw digest bytes.
 * @param data String data to hash.
 * @return 32-byte raw binary digest.
 */
std::string sha256_raw(const std::string& data);

/**
 * @brief Converts a 64-character hex string to a 32-byte raw binary string.
 * @param hex 64-character hexadecimal string.
 * @return 32-byte raw string.
 * @throws std::invalid_argument if hex string length is not 64 or contains non-hex characters.
 */
std::string hex_to_raw(const std::string& hex);

/**
 * @brief Converts raw bytes to a lowercase hexadecimal string.
 * @param raw Raw byte string (typically 32 bytes for SHA-256).
 * @return Lowercase hex string (2 hex characters per input byte).
 */
std::string raw_to_hex(const std::string& raw);

/**
 * @brief Validates whether a string is a valid 64-character lowercase hex SHA-256 digest.
 * @param hex String to validate.
 * @return True if length is 64 and characters are hex [0-9a-fA-F].
 */
bool is_valid_sha256_hex(const std::string& hex);

} // namespace bts
