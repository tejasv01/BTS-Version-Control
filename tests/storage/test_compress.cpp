#include <gtest/gtest.h>
#include "../../src/storage/compress.hpp"

#include <numeric>
#include <vector>

namespace bts {
namespace {

TEST(CompressTest, EmptyStringHandling) {
    std::string empty;
    std::string compressed = compress_data(empty);
    EXPECT_FALSE(compressed.empty());

    std::string decompressed = decompress_data(compressed);
    EXPECT_EQ(decompressed, empty);
}

TEST(CompressTest, RoundTripShortAndLongStrings) {
    std::string short_str = "Hello, BTS-Version-Control storage engine!";
    std::string comp_short = compress_data(short_str);
    EXPECT_EQ(decompress_data(comp_short), short_str);

    // Repetitive text (should compress well)
    std::string repeated;
    for (int i = 0; i < 2000; ++i) {
        repeated += "line " + std::to_string(i) + ": content to compress with zlib level 1\n";
    }

    std::string comp_repeated = compress_data(repeated, 1);
    EXPECT_LT(comp_repeated.size(), repeated.size());
    EXPECT_EQ(decompress_data(comp_repeated), repeated);
}

TEST(CompressTest, BinaryDataRoundTrip) {
    // Generate binary buffer with null bytes and non-printable bytes
    std::string binary_data;
    binary_data.resize(65536);
    for (std::size_t i = 0; i < binary_data.size(); ++i) {
        binary_data[i] = static_cast<char>(i % 256);
    }

    std::string compressed = compress_data(binary_data, 1);
    std::string decompressed = decompress_data(compressed);
    EXPECT_EQ(decompressed, binary_data);
}

TEST(CompressTest, CorruptCompressedDataThrows) {
    std::string valid = compress_data("test data for corruption detection");
    EXPECT_GT(valid.size(), 4u);

    // Corrupt bytes in the middle of zlib stream
    valid[valid.size() / 2] ^= 0xFF;
    valid[valid.size() / 2 + 1] ^= 0xAA;

    EXPECT_THROW(decompress_data(valid), std::runtime_error);
}

TEST(CompressTest, DecompressEmptyThrows) {
    EXPECT_THROW(decompress_data(""), std::runtime_error);
}

TEST(CompressTest, InvalidCompressionLevelThrows) {
    EXPECT_THROW(compress_data("test", -1), std::invalid_argument);
    EXPECT_THROW(compress_data("test", 10), std::invalid_argument);
}

} // namespace
} // namespace bts
