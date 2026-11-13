#pragma once

#include <string>

namespace bts {

/**
 * @brief Compresses data using zlib.
 * @param data The uncompressed data.
 * @param level Compression level (default 1, per SRS PR-1 / REQ-OBJ-3).
 * @return Zlib-compressed binary data.
 * @throws std::runtime_error if compression fails.
 */
std::string compress_data(const std::string& data, int level = 1);

/**
 * @brief Decompresses zlib-compressed data.
 * @param compressed_data The zlib-compressed binary data.
 * @return The original uncompressed data.
 * @throws std::runtime_error (or CorruptObjectException) if decompression fails.
 */
std::string decompress_data(const std::string& compressed_data);

} // namespace bts
