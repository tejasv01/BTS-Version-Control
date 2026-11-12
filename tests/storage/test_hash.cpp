#include <gtest/gtest.h>
#include "../../src/storage/hash.hpp"

namespace bts {
namespace {

TEST(HashTest, KnownSha256Vectors) {
    // NIST / standard known SHA-256 test vectors:
    // 1. Empty string:
    EXPECT_EQ(sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // 2. "The quick brown fox jumps over the lazy dog":
    EXPECT_EQ(
        sha256("The quick brown fox jumps over the lazy dog"),
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"
    );

    // 3. "The quick brown fox jumps over the lazy cog":
    EXPECT_EQ(
        sha256("The quick brown fox jumps over the lazy cog"),
        "e4c4d8f3bf76b692de791a173e05321150f7a345b46484fe427f6acc7ecc81be"
    );

    // 4. "The quick brown fox jumps over the lazy dog." (with period):
    EXPECT_EQ(
        sha256("The quick brown fox jumps over the lazy dog."),
        "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c"
    );
}

TEST(HashTest, RawHashAndConversionRoundTrip) {
    std::string text = "BTS-Version-Control content addressing test";
    std::string hex = sha256(text);
    std::string raw = sha256_raw(text);

    EXPECT_EQ(raw.size(), 32u);
    EXPECT_EQ(raw_to_hex(raw), hex);
    EXPECT_EQ(hex_to_raw(hex), raw);
}

TEST(HashTest, HexToRawValidation) {
    EXPECT_THROW(hex_to_raw("abc"), std::invalid_argument);
    EXPECT_THROW(hex_to_raw("zz"), std::invalid_argument);

    std::string valid_hex = "0123456789abcdefABCDEF1234567890abcdef1234567890abcdef1234567890";
    EXPECT_NO_THROW({
        std::string raw = hex_to_raw(valid_hex);
        EXPECT_EQ(raw.size(), 32u);
    });
}

TEST(HashTest, IsValidSha256Hex) {
    EXPECT_TRUE(is_valid_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    EXPECT_TRUE(is_valid_sha256_hex("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"));
    EXPECT_FALSE(is_valid_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85")); // 63 chars
    EXPECT_FALSE(is_valid_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b8555")); // 65 chars
    EXPECT_FALSE(is_valid_sha256_hex("g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")); // non-hex 'g'
}

} // namespace
} // namespace bts
