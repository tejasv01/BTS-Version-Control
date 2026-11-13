#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/cli.h"
#include "bts/diff.h"
#include "bts/index.h"
#include "bts/object_store.h"

namespace fs = std::filesystem;

class DiffTest : public ::testing::Test {
protected:
    fs::path repo_;
    fs::path prev_cwd_;

    void SetUp() override {
        prev_cwd_ = fs::current_path();
        repo_ = fs::temp_directory_path() /
                ("bts_test_diff_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(repo_);
        ASSERT_EQ(bts::cmd_init(repo_), 0);
        fs::current_path(repo_);
    }

    void TearDown() override {
        fs::current_path(prev_cwd_);
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(
                 repo_, fs::directory_options::skip_permission_denied, ec)) {
            fs::permissions(entry.path(), fs::perms::owner_all,
                            fs::perm_options::add, ec);
        }
        fs::remove_all(repo_, ec);
    }
};

TEST_F(DiffTest, SplitLinesAndBinaryDetection) {
    EXPECT_TRUE(bts::split_lines("").empty());

    auto lines = bts::split_lines("a\nb\nc\n");
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_EQ(lines[2], "c");

    auto no_trail = bts::split_lines("x\ny");
    ASSERT_EQ(no_trail.size(), 2u);
    EXPECT_EQ(no_trail[0], "x");
    EXPECT_EQ(no_trail[1], "y");

    EXPECT_FALSE(bts::is_binary_content("plain text\nline 2\n"));
    std::string with_nul = "abc";
    with_nul.push_back('\0');
    with_nul += "def";
    EXPECT_TRUE(bts::is_binary_content(with_nul));
}

TEST_F(DiffTest, MyersIdenticalAndEmptyBoundaries) {
    EXPECT_TRUE(bts::format_unified_diff("a.txt", "same\n", "same\n").empty());

    std::string add_diff = bts::format_unified_diff("a.txt", "", "one\ntwo\n");
    EXPECT_NE(add_diff.find("--- a/a.txt\n+++ b/a.txt\n"), std::string::npos);
    EXPECT_NE(add_diff.find("@@ -0,0 +1,2 @@\n+one\n+two\n"), std::string::npos);

    std::string del_diff = bts::format_unified_diff("a.txt", "one\ntwo\n", "");
    EXPECT_NE(del_diff.find("@@ -1,2 +0,0 @@\n-one\n-two\n"), std::string::npos);
}

TEST_F(DiffTest, MyersReplacementAndUnifiedHunkHeaders) {
    std::string old_text =
        "line 1\n"
        "line 2\n"
        "line 3\n"
        "line 4\n"
        "line 5\n"
        "line 6\n"
        "line 7\n"
        "line 8\n"
        "line 9\n"
        "line 10\n";
    std::string new_text =
        "line 1\n"
        "line 2 modified\n"
        "line 3\n"
        "line 4\n"
        "line 5\n"
        "line 6\n"
        "line 7\n"
        "line 8\n"
        "line 9\n"
        "line 10\n"
        "line 11\n";

    std::string diff = bts::format_unified_diff("notes.txt", old_text, new_text);
    EXPECT_NE(diff.find("--- a/notes.txt\n+++ b/notes.txt\n"), std::string::npos);
    // Two separate hunks because lines 3-10 have >6 unchanged lines between edits
    EXPECT_NE(diff.find("@@ -1,5 +1,5 @@\n line 1\n-line 2\n+line 2 modified\n"),
              std::string::npos);
    EXPECT_NE(diff.find("@@ -8,3 +8,4 @@\n line 8\n line 9\n line 10\n+line 11\n"),
              std::string::npos);
}

TEST_F(DiffTest, BinaryFilesDifferNotice) {
    std::string bin1 = "PNG";
    bin1.push_back('\0');
    bin1 += "v1";

    std::string bin2 = "PNG";
    bin2.push_back('\0');
    bin2 += "v2";

    std::string diff = bts::format_unified_diff("img.bin", bin1, bin2);
    EXPECT_NE(diff.find("--- a/img.bin\n+++ b/img.bin\nBinary files differ\n"),
              std::string::npos);
}

TEST_F(DiffTest, CmdDiffWorkingTreeVsIndex) {
    {
        std::ofstream f(repo_ / "code.txt");
        f << "alpha\nbeta\ngamma\n";
    }
    {
        std::ofstream b(repo_ / "data.bin", std::ios::binary);
        std::string payload = "hdr";
        payload.push_back('\0');
        payload += "123";
        b.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    {
        std::ofstream clean(repo_ / "clean.txt");
        clean << "unchanged\n";
    }

    ASSERT_EQ(bts::cmd_add(repo_, {"code.txt", "data.bin", "clean.txt"}), 0);

    // Modify code.txt and data.bin in working tree
    {
        std::ofstream f(repo_ / "code.txt");
        f << "alpha\nbeta_updated\ngamma\ndelta\n";
    }
    {
        std::ofstream b(repo_ / "data.bin", std::ios::binary);
        std::string payload = "hdr";
        payload.push_back('\0');
        payload += "456";
        b.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    testing::internal::CaptureStdout();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("diff")};
    int rc = bts::cli_main(2, argv);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("--- a/code.txt\n+++ b/code.txt\n"), std::string::npos);
    EXPECT_NE(out.find("-beta\n+beta_updated\n"), std::string::npos);
    EXPECT_NE(out.find("+delta\n"), std::string::npos);
    EXPECT_NE(out.find("--- a/data.bin\n+++ b/data.bin\nBinary files differ\n"),
              std::string::npos);
    EXPECT_EQ(out.find("clean.txt"), std::string::npos);
}

TEST_F(DiffTest, CorruptIndexReturnsExitCode3) {
    {
        std::ofstream idx(repo_ / ".bts" / "index", std::ios::binary | std::ios::trunc);
        std::string garbage(60, 'Z');
        idx.write(garbage.data(), static_cast<std::streamsize>(garbage.size()));
    }

    testing::internal::CaptureStderr();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("diff")};
    int rc = bts::cli_main(2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 3);
    EXPECT_NE(err.find(".bts/index"), std::string::npos);
}

