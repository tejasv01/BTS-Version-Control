#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/log.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace fs = std::filesystem;

class LogTest : public ::testing::Test {
protected:
    fs::path repo_;

    void SetUp() override {
        repo_ = fs::temp_directory_path() /
                ("bts_test_log_" + std::to_string(std::chrono::steady_clock::now()
                                                       .time_since_epoch()
                                                       .count()));
        fs::create_directories(repo_);

        auto dot_bts = repo_ / ".bts";
        fs::create_directories(dot_bts / "objects");
        fs::create_directories(dot_bts / "refs" / "heads");
        {
            std::ofstream idx(dot_bts / "index", std::ios::binary);
        }
        {
            std::ofstream head(dot_bts / "HEAD");
            head << "ref: refs/heads/main\n";
        }
    }

    void TearDown() override {
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(
                 repo_, fs::directory_options::skip_permission_denied, ec)) {
            fs::permissions(entry.path(), fs::perms::owner_all,
                           fs::perm_options::add, ec);
        }
        fs::remove_all(repo_, ec);
    }

    std::string make_commit(const std::string& message) {
        std::string tree_hash = bts::write_tree(repo_, {});
        std::vector<std::string> parents;
        auto head = bts::resolve_head(repo_);
        if (head) parents.push_back(*head);

        std::string commit_hash = bts::write_commit(
            repo_, tree_hash, parents, "Sarthak <s@s.com>", message);

        auto branch = bts::current_branch(repo_);
        if (branch) {
            bts::update_branch_ref(repo_, *branch, commit_hash);
        } else {
            bts::write_head(repo_, commit_hash);
        }
        return commit_hash;
    }
};

// ─── Unborn branch (REQ-LOG-4) ───────────────────────────────────────────────

TEST_F(LogTest, Log_UnbornBranch) {
    testing::internal::CaptureStderr();
    int rc = bts::cmd_log(repo_, false);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("does not have any commits"), std::string::npos);
}

// ─── Single commit (REQ-LOG-1, REQ-LOG-2) ────────────────────────────────────

TEST_F(LogTest, Log_SingleCommit) {
    std::string c = make_commit("Initial commit message");

    testing::internal::CaptureStdout();
    int rc = bts::cmd_log(repo_, false);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find(c), std::string::npos);
    EXPECT_NE(out.find("Author: Sarthak <s@s.com>"), std::string::npos);
    EXPECT_NE(out.find("Initial commit message"), std::string::npos);
}

TEST_F(LogTest, Log_SingleCommit_Oneline) {
    std::string c = make_commit("Initial commit message");

    testing::internal::CaptureStdout();
    int rc = bts::cmd_log(repo_, true);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find(c.substr(0, 7)), std::string::npos);
    EXPECT_NE(out.find("Initial commit message"), std::string::npos);
}

// ─── Multiple commits ordered newest-first ───────────────────────────────────

TEST_F(LogTest, Log_MultipleCommits) {
    std::string c1 = make_commit("First");
    std::string c2 = make_commit("Second");
    std::string c3 = make_commit("Third");

    testing::internal::CaptureStdout();
    int rc = bts::cmd_log(repo_, false);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    auto pos3 = out.find("Third");
    auto pos2 = out.find("Second");
    auto pos1 = out.find("First");

    EXPECT_NE(pos3, std::string::npos);
    EXPECT_NE(pos2, std::string::npos);
    EXPECT_NE(pos1, std::string::npos);

    // Newest first order
    EXPECT_LT(pos3, pos2);
    EXPECT_LT(pos2, pos1);
}

TEST_F(LogTest, Log_MultipleCommits_Oneline) {
    std::string c1 = make_commit("First");
    std::string c2 = make_commit("Second");

    testing::internal::CaptureStdout();
    int rc = bts::cmd_log(repo_, true);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    auto pos2 = out.find(c2.substr(0, 7));
    auto pos1 = out.find(c1.substr(0, 7));

    EXPECT_NE(pos2, std::string::npos);
    EXPECT_NE(pos1, std::string::npos);
    EXPECT_LT(pos2, pos1);
}

// ─── Detached HEAD log ───────────────────────────────────────────────────────

TEST_F(LogTest, Log_Detached) {
    std::string c1 = make_commit("Commit 1");
    std::string c2 = make_commit("Commit 2");

    bts::write_head(repo_, c1);  // detach at c1

    testing::internal::CaptureStdout();
    int rc = bts::cmd_log(repo_, true);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find(c1.substr(0, 7)), std::string::npos);
    EXPECT_EQ(out.find(c2.substr(0, 7)), std::string::npos);
}
