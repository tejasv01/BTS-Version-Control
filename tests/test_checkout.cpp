#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/checkout.h"
#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace fs = std::filesystem;

class CheckoutTest : public ::testing::Test {
protected:
    fs::path repo_;
    std::string commit1_;
    std::string commit2_;

    void SetUp() override {
        repo_ = fs::temp_directory_path() /
                ("bts_test_checkout_" + std::to_string(std::chrono::steady_clock::now()
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

        // Create file1 and commit1
        std::string blob1 = bts::write_object(repo_, bts::ObjectType::Blob, "hello version 1\n");
        bts::TreeEntry te1{100644, "file.txt", blob1};
        std::string tree1 = bts::write_tree(repo_, {te1});
        commit1_ = bts::write_commit(repo_, tree1, {}, "Sarthak <s@s.com>", "commit 1");
        bts::update_branch_ref(repo_, "main", commit1_);

        // Write file1 to working tree
        {
            std::ofstream f(repo_ / "file.txt");
            f << "hello version 1\n";
        }
        // Write index entry for file1
        bts::IndexEntry ie1{100644, 0, 0, 16, blob1, "file.txt"};
        bts::write_index(repo_, {ie1});

        // Create file2 and commit2
        std::string blob2 = bts::write_object(repo_, bts::ObjectType::Blob, "hello version 2\n");
        bts::TreeEntry te2{100644, "file.txt", blob2};
        std::string tree2 = bts::write_tree(repo_, {te2});
        commit2_ = bts::write_commit(repo_, tree2, {commit1_}, "Sarthak <s@s.com>", "commit 2");
        bts::update_branch_ref(repo_, "feature", commit2_);
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

// ─── Branch checkout (REQ-CHK-1) ─────────────────────────────────────────────

TEST_F(CheckoutTest, CheckoutBranch_Success) {
    int rc = bts::cmd_checkout(repo_, "feature");
    EXPECT_EQ(rc, 0);

    // HEAD should point to feature
    EXPECT_EQ(bts::read_head(repo_), "ref: refs/heads/feature");
    EXPECT_EQ(bts::current_branch(repo_), "feature");

    // Working file should be updated to version 2
    std::ifstream in(repo_ / "file.txt");
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "hello version 2\n");
}

TEST_F(CheckoutTest, CheckoutBranch_NotFound) {
    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, "nonexistent-branch");
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("did not match"), std::string::npos);
}

// ─── Detached HEAD checkout (REQ-CHK-3) ──────────────────────────────────────

TEST_F(CheckoutTest, CheckoutDetached_FullHash) {
    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, commit2_);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(bts::read_head(repo_), commit2_);
    EXPECT_TRUE(bts::is_head_detached(repo_));
    EXPECT_NE(err.find("detached HEAD"), std::string::npos);
}

TEST_F(CheckoutTest, CheckoutDetached_Prefix) {
    std::string prefix = commit2_.substr(0, 8);
    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, prefix);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(bts::read_head(repo_), commit2_);
    EXPECT_TRUE(bts::is_head_detached(repo_));
    EXPECT_NE(err.find("detached HEAD"), std::string::npos);
}

TEST_F(CheckoutTest, CheckoutDetached_TooShortPrefixFails) {
    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, commit2_.substr(0, 3));
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("at least 4 characters"), std::string::npos);
}

// ─── Overwrite protection (REQ-CHK-2) ────────────────────────────────────────

TEST_F(CheckoutTest, Checkout_RefusesOverwriteModifiedTrackedFile) {
    {
        std::ofstream f(repo_ / "file.txt");
        f << "uncommitted local modification\n";
    }

    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, "feature");
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.find("overwritten"), std::string::npos);
    EXPECT_EQ(bts::current_branch(repo_), "main");

    std::ifstream in(repo_ / "file.txt");
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "uncommitted local modification\n");
}

TEST_F(CheckoutTest, Checkout_RefusesOverwriteUntrackedCollision) {
    // Add commit on feature introducing extra.txt while main lacks it.
    std::string extra_blob =
        bts::write_object(repo_, bts::ObjectType::Blob, "from feature branch\n");
    std::string blob2 =
        bts::write_object(repo_, bts::ObjectType::Blob, "hello version 2\n");
    std::string tree3 = bts::write_tree(
        repo_, {{100644, "extra.txt", extra_blob}, {100644, "file.txt", blob2}});
    std::string commit3 =
        bts::write_commit(repo_, tree3, {commit2_}, "Sarthak <s@s.com>", "commit 3");
    bts::update_branch_ref(repo_, "feature", commit3);

    {
        std::ofstream untracked(repo_ / "extra.txt");
        untracked << "precious untracked work\n";
    }

    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, "feature");
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.find("overwritten"), std::string::npos);
    EXPECT_EQ(bts::current_branch(repo_), "main");

    std::ifstream in(repo_ / "extra.txt");
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "precious untracked work\n");
}

// ─── Ambiguous prefix & branch precedence (REQ-CHK-3) ────────────────────────

TEST_F(CheckoutTest, CheckoutDetached_AmbiguousPrefixListsCandidates) {
    std::string fake_hash = commit2_;
    fake_hash.back() = (fake_hash.back() == '0') ? '1' : '0';
    fs::path orig_obj =
        repo_ / ".bts" / "objects" / commit2_.substr(0, 2) / commit2_.substr(2);
    fs::path fake_obj =
        repo_ / ".bts" / "objects" / fake_hash.substr(0, 2) / fake_hash.substr(2);
    fs::copy_file(orig_obj, fake_obj);

    testing::internal::CaptureStderr();
    int rc = bts::cmd_checkout(repo_, commit2_.substr(0, 8));
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.find("ambiguous"), std::string::npos);
    EXPECT_NE(err.find(commit2_), std::string::npos);
    EXPECT_NE(err.find(fake_hash), std::string::npos);
}

TEST_F(CheckoutTest, Checkout_BranchPrecedenceOverCommitPrefix) {
    std::string branch_name = commit2_.substr(0, 8);
    bts::update_branch_ref(repo_, branch_name, commit1_);

    int rc = bts::cmd_checkout(repo_, branch_name);
    EXPECT_EQ(rc, 0);
    EXPECT_FALSE(bts::is_head_detached(repo_));
    EXPECT_EQ(bts::current_branch(repo_), branch_name);
    EXPECT_EQ(bts::resolve_head(repo_), commit1_);
}
