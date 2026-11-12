#include "compress.hpp"

#include <zlib.h>
#include <array>
#include <stdexcept>

namespace bts {

std::string compress_data(const std::string& data, int level) {
    if (level < 0 || level > 9) {
        throw std::invalid_argument("Invalid zlib compression level: must be between 0 and 9");
    }

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit(&strm, level);
    if (ret != Z_OK) {
        throw std::runtime_error("zlib deflateInit failed with error code: " + std::to_string(ret));
    }

    strm.avail_in = static_cast<uInt>(data.size());
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));

    std::string compressed;
    std::array<char, 16384> buffer;

    do {
        strm.avail_out = static_cast<uInt>(buffer.size());
        strm.next_out = reinterpret_cast<Bytef*>(buffer.data());

        ret = deflate(&strm, Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            throw std::runtime_error("zlib deflate encountered Z_STREAM_ERROR");
        }

        std::size_t have = buffer.size() - strm.avail_out;
        compressed.append(buffer.data(), have);
    } while (strm.avail_out == 0);

    deflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib deflate failed to finish stream");
    }

    return compressed;
}

std::string decompress_data(const std::string& compressed_data) {
    if (compressed_data.empty()) {
        throw std::runtime_error("Cannot decompress empty zlib payload");
    }

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(compressed_data.size());
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data.data()));

    int ret = inflateInit(&strm);
    if (ret != Z_OK) {
        throw std::runtime_error("zlib inflateInit failed with error code: " + std::to_string(ret));
    }

    std::string decompressed;
    std::array<char, 16384> buffer;

    do {
        strm.avail_out = static_cast<uInt>(buffer.size());
        strm.next_out = reinterpret_cast<Bytef*>(buffer.data());

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            throw std::runtime_error("zlib inflate error code: " + std::to_string(ret));
        }

        std::size_t have = buffer.size() - strm.avail_out;
        decompressed.append(buffer.data(), have);
    } while (strm.avail_out == 0);

    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib inflate did not reach end of stream (corrupted data)");
    }

    return decompressed;
}

} // namespace bts
