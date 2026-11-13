#include <gtest/gtest.h>
#include "../../src/objects/tree.hpp"
#include "../../src/storage/hash.hpp"
#include "../../src/storage/object_store.hpp"

namespace bts {
namespace {

TEST(TreeTest, SortEntriesByNameInByteOrder) {
    std::string hash_a = sha256("content a");
    std::string hash_b = sha256("content b");
    std::string hash_z = sha256("content z");

    Tree tree;
    // Add in reverse/unsorted order
    tree.entries.push_back(TreeEntry{"100644", "zebra.txt", hash_z});
    tree.entries.push_back(TreeEntry{"100644", "apple.txt", hash_a});
    tree.entries.push_back(TreeEntry{"100755", "build.sh", hash_b});

    std::string serialized = tree.serialize();

    // When deserialized, entries should be in sorted order ("apple.txt", "build.sh", "zebra.txt")
    Tree parsed = Tree::deserialize(serialized);
    ASSERT_EQ(parsed.entries.size(), 3u);
    EXPECT_EQ(parsed.entries[0].name, "apple.txt");
    EXPECT_EQ(parsed.entries[0].mode, "100644");
    EXPECT_EQ(parsed.entries[0].sha256_hex, hash_a);

    EXPECT_EQ(parsed.entries[1].name, "build.sh");
    EXPECT_EQ(parsed.entries[1].mode, "100755");
    EXPECT_EQ(parsed.entries[1].sha256_hex, hash_b);

    EXPECT_EQ(parsed.entries[2].name, "zebra.txt");
    EXPECT_EQ(parsed.entries[2].mode, "100644");
    EXPECT_EQ(parsed.entries[2].sha256_hex, hash_z);
}

TEST(TreeTest, SubtreeAndModesRoundTrip) {
    std::string file_hash = sha256("file");
    std::string dir_hash = sha256("dir");
    std::string link_hash = sha256("link");

    Tree tree;
    tree.entries.push_back(TreeEntry{"100644", "README.md", file_hash});
    tree.entries.push_back(TreeEntry{"40000", "src", dir_hash});
    tree.entries.push_back(TreeEntry{"120000", "link_target", link_hash});

    std::string serialized = tree.serialize();
    Tree deserialized = Tree::deserialize(serialized);

    ASSERT_EQ(deserialized.entries.size(), 3u);
    // Byte order: "README.md" (0x52) < "link_target" (0x6C) < "src" (0x73)
    EXPECT_EQ(deserialized.entries[0].name, "README.md");
    EXPECT_EQ(deserialized.entries[1].name, "link_target");
    EXPECT_EQ(deserialized.entries[2].name, "src");
}

TEST(TreeTest, MalformedPayloadThrows) {
    // Missing space after mode
    EXPECT_THROW(Tree::deserialize("100644"), CorruptObjectException);

    // Missing null terminator
    EXPECT_THROW(Tree::deserialize("100644 filename_without_null"), CorruptObjectException);

    // Truncated raw hash (fewer than 32 bytes)
    std::string truncated = "100644 file\0" + std::string(10, 'x');
    EXPECT_THROW(Tree::deserialize(truncated), CorruptObjectException);
}

} // namespace
} // namespace bts
