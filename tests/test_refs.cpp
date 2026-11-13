#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/object_store.h"
#include "bts/refs.h"

namespace fs = std::filesystem;

class RefsTest : public ::testing::Test {
protected:
    fs::path repo_;

    void SetUp() override {
        repo_ = fs::temp_directory_path() /
                ("bts_test_refs_" + std::to_string(std::chrono::steady_clock::now()
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

    std::string make_dummy_commit(const std::string& msg = "initial commit") {
        std::string tree_hash = bts::write_tree(repo_, {});
        std::vector<std::string> parents;
        auto head = bts::resolve_head(repo_);
        if (head) parents.push_back(*head);
        std::string commit_hash =
            bts::write_commit(repo_, tree_hash, parents,
                              "Sarthak <test@example.com>", msg);
        auto branch = bts::current_branch(repo_);
        if (branch) {
            bts::update_branch_ref(repo_, *branch, commit_hash);
        } else {
            bts::write_head(repo_, commit_hash);
        }
        return commit_hash;
    }
};

// ─── read_head / is_head_detached ────────────────────────────────────────────

TEST_F(RefsTest, ReadHead_DefaultMain) {
    EXPECT_EQ(bts::read_head(repo_), "ref: refs/heads/main");
    EXPECT_FALSE(bts::is_head_detached(repo_));
}

TEST_F(RefsTest, ReadHead_Detached) {
    std::string dummy_hash(64, 'a');
    EXPECT_TRUE(bts::write_head(repo_, dummy_hash));
    EXPECT_EQ(bts::read_head(repo_), dummy_hash);
    EXPECT_TRUE(bts::is_head_detached(repo_));
}

// ─── write_head atomic & lock ────────────────────────────────────────────────

TEST_F(RefsTest, WriteHead_Success) {
    EXPECT_TRUE(bts::write_head(repo_, "ref: refs/heads/feature"));
    EXPECT_EQ(bts::read_head(repo_), "ref: refs/heads/feature");
    EXPECT_FALSE(fs::exists(repo_ / ".bts" / "HEAD.lock"));
}

TEST_F(RefsTest, WriteHead_LockExistsFails) {
    fs::path lock = repo_ / ".bts" / "HEAD.lock";
    {
        std::ofstream ofs(lock);
        ofs << "locking";
    }
    EXPECT_FALSE(bts::write_head(repo_, "ref: refs/heads/fail"));
    EXPECT_EQ(bts::read_head(repo_), "ref: refs/heads/main");
}

// ─── resolve_head ────────────────────────────────────────────────────────────

TEST_F(RefsTest, ResolveHead_UnbornBranch) {
    EXPECT_EQ(bts::resolve_head(repo_), std::nullopt);
}

TEST_F(RefsTest, ResolveHead_WithCommit) {
    std::string c = make_dummy_commit("first commit");
    EXPECT_EQ(bts::resolve_head(repo_), c);
}

TEST_F(RefsTest, ResolveHead_Detached) {
    std::string dummy_hash(64, 'f');
    EXPECT_TRUE(bts::write_head(repo_, dummy_hash));
    EXPECT_EQ(bts::resolve_head(repo_), dummy_hash);
}

// ─── current_branch ──────────────────────────────────────────────────────────

TEST_F(RefsTest, CurrentBranch_Main) {
    EXPECT_EQ(bts::current_branch(repo_), "main");
}

TEST_F(RefsTest, CurrentBranch_Custom) {
    EXPECT_TRUE(bts::write_head(repo_, "ref: refs/heads/develop"));
    EXPECT_EQ(bts::current_branch(repo_), "develop");
}

TEST_F(RefsTest, CurrentBranch_DetachedReturnsNullopt) {
    std::string dummy_hash(64, '1');
    EXPECT_TRUE(bts::write_head(repo_, dummy_hash));
    EXPECT_EQ(bts::current_branch(repo_), std::nullopt);
}

// ─── read_ref / write_ref_atomic / update_branch_ref ─────────────────────────

TEST_F(RefsTest, WriteRef_AtomicAndRead) {
    std::string dummy_hash(64, 'c');
    EXPECT_TRUE(bts::write_ref_atomic(repo_, "refs/heads/feature", dummy_hash));
    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/feature"), dummy_hash);
    EXPECT_FALSE(fs::exists(repo_ / ".bts" / "refs" / "heads" / "feature.lock"));
}

TEST_F(RefsTest, WriteRef_LockExistsFails) {
    fs::path lock = repo_ / ".bts" / "refs" / "heads" / "locked.lock";
    fs::create_directories(lock.parent_path());
    {
        std::ofstream ofs(lock);
        ofs << "locked";
    }
    EXPECT_FALSE(bts::write_ref_atomic(repo_, "refs/heads/locked", std::string(64, '0')));
}

TEST_F(RefsTest, UpdateBranchRef) {
    std::string h1(64, '1');
    std::string h2(64, '2');
    EXPECT_TRUE(bts::update_branch_ref(repo_, "dev", h1));
    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/dev"), h1);
    EXPECT_TRUE(bts::update_branch_ref(repo_, "dev", h2));
    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/dev"), h2);
}

TEST_F(RefsTest, DeleteRef) {
    std::string h(64, '3');
    EXPECT_TRUE(bts::update_branch_ref(repo_, "temp", h));
    EXPECT_TRUE(bts::delete_ref(repo_, "refs/heads/temp"));
    EXPECT_EQ(bts::read_ref(repo_, "refs/heads/temp"), std::nullopt);
    EXPECT_FALSE(bts::delete_ref(repo_, "refs/heads/temp"));
}

// ─── list_branches ───────────────────────────────────────────────────────────

TEST_F(RefsTest, ListBranches) {
    EXPECT_TRUE(bts::list_branches(repo_).empty());

    bts::update_branch_ref(repo_, "beta", std::string(64, 'b'));
    bts::update_branch_ref(repo_, "alpha", std::string(64, 'a'));

    auto list = bts::list_branches(repo_);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], "alpha");
    EXPECT_EQ(list[1], "beta");
}

// ─── validate_branch_name (REQ-BRN-4) ────────────────────────────────────────

TEST_F(RefsTest, ValidateBranchName_Valid) {
    EXPECT_TRUE(bts::validate_branch_name("feature").empty());
    EXPECT_TRUE(bts::validate_branch_name("feature-123").empty());
    EXPECT_TRUE(bts::validate_branch_name("feature_123").empty());
    EXPECT_TRUE(bts::validate_branch_name("fix.bug").empty());
}

TEST_F(RefsTest, ValidateBranchName_Invalid) {
    EXPECT_FALSE(bts::validate_branch_name("").empty());
    EXPECT_FALSE(bts::validate_branch_name("has space").empty());
    EXPECT_FALSE(bts::validate_branch_name("dot..dot").empty());
    EXPECT_FALSE(bts::validate_branch_name("tilde~").empty());
    EXPECT_FALSE(bts::validate_branch_name("caret^").empty());
    EXPECT_FALSE(bts::validate_branch_name("colon:").empty());
    EXPECT_FALSE(bts::validate_branch_name("back\\slash").empty());
    EXPECT_FALSE(bts::validate_branch_name("-leadingdash").empty());
    EXPECT_FALSE(bts::validate_branch_name("/leadingslash").empty());
    EXPECT_FALSE(bts::validate_branch_name("trailingslash/").empty());
    EXPECT_FALSE(bts::validate_branch_name("name.lock").empty());
    EXPECT_FALSE(bts::validate_branch_name("control\x05char").empty());
}
