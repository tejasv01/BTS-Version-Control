#include <gtest/gtest.h>
#include "../../src/objects/commit.hpp"
#include "../../src/storage/hash.hpp"
#include "../../src/storage/object_store.hpp"

namespace bts {
namespace {

TEST(CommitTest, RootCommitRoundTrip) {
    Commit original;
    original.tree_hash = sha256("root tree content");
    original.author_name = "Bharat";
    original.author_email = "bharat@example.com";
    original.timestamp = 1763000000;
    original.timezone = "+0530";
    original.message = "Initial repository commit\n\nCreated project structure.";

    std::string serialized = original.serialize();
    EXPECT_NE(serialized.find("tree " + original.tree_hash), std::string::npos);
    EXPECT_EQ(serialized.find("parent "), std::string::npos); // No parent in root commit

    Commit parsed = Commit::deserialize(serialized);
    EXPECT_EQ(parsed.tree_hash, original.tree_hash);
    EXPECT_TRUE(parsed.parent_hashes.empty());
    EXPECT_EQ(parsed.author_name, original.author_name);
    EXPECT_EQ(parsed.author_email, original.author_email);
    EXPECT_EQ(parsed.timestamp, original.timestamp);
    EXPECT_EQ(parsed.timezone, original.timezone);
    EXPECT_EQ(parsed.message, original.message);
}

TEST(CommitTest, CommitWithMultipleParentsRoundTrip) {
    Commit commit;
    commit.tree_hash = sha256("merged tree");
    commit.parent_hashes.push_back(sha256("parent 1"));
    commit.parent_hashes.push_back(sha256("parent 2"));
    commit.author_name = "Tejasv Lata";
    commit.author_email = "tejasv@example.com";
    commit.timestamp = 1763001234;
    commit.timezone = "+0000";
    commit.message = "Merge branch 'feature'";

    std::string serialized = commit.serialize();
    Commit parsed = Commit::deserialize(serialized);

    EXPECT_EQ(parsed.tree_hash, commit.tree_hash);
    ASSERT_EQ(parsed.parent_hashes.size(), 2u);
    EXPECT_EQ(parsed.parent_hashes[0], commit.parent_hashes[0]);
    EXPECT_EQ(parsed.parent_hashes[1], commit.parent_hashes[1]);
    EXPECT_EQ(parsed.author_name, commit.author_name);
    EXPECT_EQ(parsed.author_email, commit.author_email);
    EXPECT_EQ(parsed.message, commit.message);
}

TEST(CommitTest, FormatsWithAngleBracketsOrDoubleBrackets) {
    std::string raw_commit =
        "tree " + sha256("tree") + "\n" +
        "author Sarthak <<sarthak@example.com>> 1763005678 +0530\n" +
        "\n" +
        "Test double bracket author parsing\n";

    Commit parsed = Commit::deserialize(raw_commit);
    EXPECT_EQ(parsed.author_name, "Sarthak");
    EXPECT_EQ(parsed.author_email, "sarthak@example.com");
    EXPECT_EQ(parsed.timestamp, 1763005678);
    EXPECT_EQ(parsed.timezone, "+0530");
}

TEST(CommitTest, MalformedCommitThrows) {
    // Missing blank line separating headers from message
    EXPECT_THROW(Commit::deserialize("tree 123\nauthor a <b> 1 +0000\nmessage"), CorruptObjectException);

    // Missing tree header
    EXPECT_THROW(Commit::deserialize("author a <b> 1 +0000\n\nmessage"), CorruptObjectException);

    // Missing author header
    EXPECT_THROW(Commit::deserialize("tree " + sha256("t") + "\n\nmessage"), CorruptObjectException);
}

} // namespace
} // namespace bts
