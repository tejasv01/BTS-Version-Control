#include <gtest/gtest.h>
#include "../../src/objects/blob.hpp"

namespace bts {
namespace {

TEST(BlobTest, EmptyBlobRoundTrip) {
    Blob original{""};
    std::string serialized = original.serialize();
    EXPECT_TRUE(serialized.empty());

    Blob parsed = Blob::deserialize(serialized);
    EXPECT_EQ(parsed.content, original.content);
}

TEST(BlobTest, TextBlobRoundTrip) {
    Blob original{"#include <iostream>\nint main() { return 0; }\n"};
    std::string serialized = original.serialize();
    EXPECT_EQ(serialized, original.content);

    Blob parsed = Blob::deserialize(serialized);
    EXPECT_EQ(parsed.content, original.content);
}

TEST(BlobTest, BinaryBlobRoundTrip) {
    std::string binary_data = std::string("test\0binary\xff\xfe\x00data", 18);
    Blob original{binary_data};
    std::string serialized = original.serialize();
    EXPECT_EQ(serialized.size(), 18u);

    Blob parsed = Blob::deserialize(serialized);
    EXPECT_EQ(parsed.content, original.content);
}

} // namespace
} // namespace bts
