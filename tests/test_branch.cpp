#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/branch.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace fs = std::filesystem;

class BranchTest : public ::testing::Test {
protected:
    fs::path repo_;
    std::string init_commit_;

    void SetUp() override {
        repo_ = fs::temp_directory_path() /
                ("bts_test_branch_" + std::to_string(std::chrono::steady_clock::now()
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

        // Setup an initial commit so we are not in unborn state
        std::string tree_hash = bts::write_tree(repo_, {});
        init_commit_ = bts::write_commit(repo_, tree_hash, {}, "Sarthak <s@s.com>", "root commit");
        bts::update_branch_ref(repo_, "main", init_commit_);
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
};

// ─── List branches (REQ-BRN-1) ───────────────────────────────────────────────

TEST_F(BranchTest, ListBranches_OnlyMain) {
    testing::internal::CaptureStdout();
    const char* argv[] = {nullptr};
    int rc = bts::cmd_branch(repo_, 0, argv);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("* main"), std::string::npos);
}

TEST_F(BranchTest, ListBranches_Multiple) {
    bts::update_branch_ref(repo_, "dev", init_commit_);

    testing::internal::CaptureStdout();
    int rc = bts::cmd_branch(repo_, 0, nullptr);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("  dev"), std::string::npos);
    EXPECT_NE(out.find("* main"), std::string::npos);
}

TEST_F(BranchTest, ListBranches_DetachedNoStar) {
    bts::write_head(repo_, init_commit_);  // detached

    testing::internal::CaptureStdout();
    int rc = bts::cmd_branch(repo_, 0, nullptr);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.find("*"), std::string::npos);
    EXPECT_NE(out.find("  main"), std::string::npos);
}

// ─── Create branch (REQ-BRN-2, REQ-BRN-4, REQ-BRN-5) ─────────────────────────

TEST_F(BranchTest, CreateBranch_Success) {
    const char* argv[] = {"feature-login"};
    int rc = bts::cmd_branch(repo_, 1, argv);
    EXPECT_EQ(rc, 0);

    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/feature-login"), init_commit_);
}

TEST_F(BranchTest, CreateBranch_InvalidNameFails) {
    testing::internal::CaptureStderr();
    const char* argv[] = {"bad..name"};
    int rc = bts::cmd_branch(repo_, 1, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_FALSE(err.empty());
}

TEST_F(BranchTest, CreateBranch_AlreadyExistsFails) {
    const char* argv[] = {"feature-a"};
    EXPECT_EQ(bts::cmd_branch(repo_, 1, argv), 0);

    testing::internal::CaptureStderr();
    int rc = bts::cmd_branch(repo_, 1, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("already exists"), std::string::npos);
}

TEST_F(BranchTest, CreateBranch_UnbornStateFails) {
    // Fresh repo without commits
    fs::path fresh = fs::temp_directory_path() / "bts_unborn_br_test";
    fs::create_directories(fresh / ".bts" / "refs" / "heads");
    {
        std::ofstream h(fresh / ".bts" / "HEAD");
        h << "ref: refs/heads/main\n";
    }

    testing::internal::CaptureStderr();
    const char* argv[] = {"new-branch"};
    int rc = bts::cmd_branch(fresh, 1, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("no commits"), std::string::npos);

    fs::remove_all(fresh);
}

// ─── Delete branch (REQ-BRN-3) ───────────────────────────────────────────────

TEST_F(BranchTest, DeleteBranch_Success) {
    bts::update_branch_ref(repo_, "temp-branch", init_commit_);

    const char* argv[] = {"-d", "temp-branch"};
    int rc = bts::cmd_branch(repo_, 2, argv);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/temp-branch"), std::nullopt);
}

TEST_F(BranchTest, DeleteBranch_CurrentBranchFails) {
    testing::internal::CaptureStderr();
    const char* argv[] = {"-d", "main"};
    int rc = bts::cmd_branch(repo_, 2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("Cannot delete branch"), std::string::npos);
    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/main"), init_commit_);
}

TEST_F(BranchTest, DeleteBranch_NotFoundFails) {
    testing::internal::CaptureStderr();
    const char* argv[] = {"-d", "nonexistent"};
    int rc = bts::cmd_branch(repo_, 2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("not found"), std::string::npos);
}
