#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/checkout.h"
#include "bts/cli.h"
#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"
#include "../src/storage/compress.hpp"
#include "../src/storage/hash.hpp"

namespace fs = std::filesystem;

class AddStatusCommitTest : public ::testing::Test {
protected:
    fs::path repo_;
    fs::path prev_cwd_;

    void SetUp() override {
        prev_cwd_ = fs::current_path();
        repo_ = fs::temp_directory_path() /
                ("bts_test_asc_" +
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

    std::size_t count_objects() const {
        std::size_t count = 0;
        std::error_code ec;
        for (const auto& entry :
             fs::recursive_directory_iterator(repo_ / ".bts" / "objects", ec)) {
            if (entry.is_regular_file()) ++count;
        }
        return count;
    }
};

// ─── Staging: regular, executable, symlink, zlib, 0444 (REQ-ADD-1..5, SR-1) ─

TEST_F(AddStatusCommitTest, AddRegularExecutableAndSymlink) {
    {
        std::ofstream f(repo_ / "readme.txt");
        f << "hello world\n";
    }
    fs::create_directories(repo_ / "scripts");
    {
        std::ofstream s(repo_ / "scripts" / "run.sh");
        s << "#!/bin/sh\necho hi\n";
    }
    fs::permissions(repo_ / "scripts" / "run.sh",
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);

    // Dangling symlink (REQ-ADD-5: must store target without following)
    fs::create_symlink("scripts/run.sh", repo_ / "run_link");
    fs::create_symlink("nonexistent/target", repo_ / "dangling_link");

    ASSERT_EQ(bts::cmd_add(repo_, {"."}), 0);

    auto entries = bts::read_index(repo_);
    ASSERT_EQ(entries.size(), 4u);

    // Entries sorted: dangling_link, readme.txt, run_link, scripts/run.sh
    EXPECT_EQ(entries[0].path, "dangling_link");
    EXPECT_EQ(entries[0].mode, 120000u);

    EXPECT_EQ(entries[1].path, "readme.txt");
    EXPECT_EQ(entries[1].mode, 100644u);

    EXPECT_EQ(entries[2].path, "run_link");
    EXPECT_EQ(entries[2].mode, 120000u);

    EXPECT_EQ(entries[3].path, "scripts/run.sh");
    EXPECT_EQ(entries[3].mode, 100755u);

    // Never include .bts/ (REQ-ADD-7)
    for (const auto& e : entries) {
        EXPECT_EQ(e.path.rfind(".bts", 0), std::string::npos);
    }

    // Verify stored object is zlib-compressed, matches SHA-256, and has mode 0444 (SR-1, REQ-OBJ-3)
    const std::string& h = entries[1].sha256;
    std::string expected_raw = "blob 12\0hello world\n";
    std::string full_header_payload = std::string("blob 12") + '\0' + "hello world\n";
    EXPECT_EQ(h, bts::sha256(full_header_payload));

    fs::path obj_file = repo_ / ".bts" / "objects" / h.substr(0, 2) / h.substr(2);
    ASSERT_TRUE(fs::exists(obj_file));

    auto perms = fs::status(obj_file).permissions();
    EXPECT_EQ(perms & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write),
              fs::perms::none);

    std::ifstream obj_in(obj_file, std::ios::binary);
    std::string compressed((std::istreambuf_iterator<char>(obj_in)),
                           std::istreambuf_iterator<char>());
    EXPECT_EQ(bts::decompress_data(compressed), full_header_payload);

    // Symlink blob contains link target string
    auto sym_blob = bts::read_object(repo_, entries[0].sha256);
    ASSERT_TRUE(sym_blob.has_value());
    EXPECT_EQ(*sym_blob, "nonexistent/target");
}

TEST_F(AddStatusCommitTest, AddEmptyDirAndMissingPathAndLock) {
    fs::create_directories(repo_ / "empty_dir");
    EXPECT_EQ(bts::cmd_add(repo_, {"empty_dir"}), 0);
    EXPECT_TRUE(bts::read_index(repo_).empty());

    {
        std::ofstream f(repo_ / "ok.txt");
        f << "ok\n";
    }
    testing::internal::CaptureStderr();
    int rc = bts::cmd_add(repo_, {"ok.txt", "does_not_exist.txt"});
    std::string err = testing::internal::GetCapturedStderr();
    EXPECT_NE(rc, 0);
    EXPECT_NE(err.find("does_not_exist.txt"), std::string::npos);
    EXPECT_TRUE(bts::read_index(repo_).empty());

    // Pre-existing index.lock aborts without writing objects or index (RR-3)
    {
        std::ofstream lock(repo_ / ".bts" / "index.lock");
        lock << "lock";
    }
    std::size_t objs_before = count_objects();
    testing::internal::CaptureStderr();
    rc = bts::cmd_add(repo_, {"ok.txt"});
    err = testing::internal::GetCapturedStderr();
    EXPECT_NE(rc, 0);
    EXPECT_EQ(count_objects(), objs_before);
    EXPECT_TRUE(bts::read_index(repo_).empty());
}

// ─── Status: unborn branch, 3 categories, stat cache (REQ-STAT-1..3) ─────────

TEST_F(AddStatusCommitTest, StatusUnbornAndThreeCategoriesAndStatCache) {
    {
        std::ofstream a(repo_ / "a.txt");
        a << "one\n";
    }
    {
        std::ofstream u(repo_ / "untracked.txt");
        u << "u\n";
    }
    ASSERT_EQ(bts::cmd_add(repo_, {"a.txt"}), 0);

    testing::internal::CaptureStdout();
    int rc = bts::cmd_status(repo_);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("no commits yet"), std::string::npos);
    EXPECT_NE(out.find("Changes to be committed:"), std::string::npos);
    EXPECT_NE(out.find("new file:   a.txt"), std::string::npos);
    EXPECT_NE(out.find("Untracked files:"), std::string::npos);
    EXPECT_NE(out.find("untracked.txt"), std::string::npos);

    // Configure identity and make initial commit
    ASSERT_EQ(bts::cmd_config(repo_, "user.name", "Tejasv Lata", true), 0);
    ASSERT_EQ(bts::cmd_config(repo_, "user.email", "tejasv@example.com", true), 0);
    ASSERT_EQ(bts::cmd_add(repo_, {"untracked.txt"}), 0);
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_commit(repo_, "Initial commit"), 0);
    testing::internal::GetCapturedStdout();

    // Clean repository stat-cache check: hash call counter MUST be 0 (REQ-STAT-2)
    bts::reset_status_hash_count();
    testing::internal::CaptureStdout();
    EXPECT_EQ(bts::cmd_status(repo_), 0);
    out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(bts::get_status_hash_count(), 0u);
    EXPECT_NE(out.find("nothing to commit"), std::string::npos);

    // Touch mtime without modifying content -> re-hashes once, still clean
    auto idx = bts::read_index(repo_);
    idx[0].mtime_sec = idx[0].mtime_sec - 10;
    ASSERT_TRUE(bts::write_index(repo_, idx));

    bts::reset_status_hash_count();
    testing::internal::CaptureStdout();
    EXPECT_EQ(bts::cmd_status(repo_), 0);
    out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(bts::get_status_hash_count(), 1u);
    EXPECT_NE(out.find("nothing to commit"), std::string::npos);

    // Create staged mod, staged new, unstaged mod, and untracked
    {
        std::ofstream a(repo_ / "a.txt");
        a << "one_staged\n";
    }
    {
        std::ofstream c(repo_ / "c.txt");
        c << "new_staged\n";
    }
    ASSERT_EQ(bts::cmd_add(repo_, {"a.txt", "c.txt"}), 0);
    {
        std::ofstream u(repo_ / "untracked.txt");
        u << "modified_unstaged\n";
    }
    {
        std::ofstream n(repo_ / "brand_new.txt");
        n << "untracked\n";
    }

    testing::internal::CaptureStdout();
    EXPECT_EQ(bts::cmd_status(repo_), 0);
    out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("Changes to be committed:"), std::string::npos);
    EXPECT_NE(out.find("modified:   a.txt"), std::string::npos);
    EXPECT_NE(out.find("new file:   c.txt"), std::string::npos);
    EXPECT_NE(out.find("Changes not staged for commit:"), std::string::npos);
    EXPECT_NE(out.find("modified:   untracked.txt"), std::string::npos);
    EXPECT_NE(out.find("Untracked files:"), std::string::npos);
    EXPECT_NE(out.find("brand_new.txt"), std::string::npos);
}

// ─── Config & Commit validation (REQ-CFG-2, REQ-CFG-3, REQ-CMT-1..5) ─────────

TEST_F(AddStatusCommitTest, ConfigValidationAndEmptyCommitRejection) {
    // Unsupported config key -> exit 2 listing user.name & user.email (REQ-CFG-3)
    testing::internal::CaptureStderr();
    int cfg_rc = bts::cmd_config(repo_, "core.editor", "vim", true);
    std::string cfg_err = testing::internal::GetCapturedStderr();
    EXPECT_EQ(cfg_rc, 2);
    EXPECT_NE(cfg_err.find("user.name"), std::string::npos);
    EXPECT_NE(cfg_err.find("user.email"), std::string::npos);

    {
        std::ofstream f(repo_ / "file.txt");
        f << "hello\n";
    }
    ASSERT_EQ(bts::cmd_add(repo_, {"file.txt"}), 0);
    std::size_t objs_before = count_objects();

    // Missing user.name -> fail naming user.name, object count unchanged (REQ-CFG-2)
    testing::internal::CaptureStderr();
    int c1 = bts::cmd_commit(repo_, "msg");
    std::string e1 = testing::internal::GetCapturedStderr();
    EXPECT_NE(c1, 0);
    EXPECT_NE(e1.find("user.name"), std::string::npos);
    EXPECT_EQ(count_objects(), objs_before);

    // Missing user.email -> fail naming user.email, object count unchanged (REQ-CFG-2)
    ASSERT_EQ(bts::cmd_config(repo_, "user.name", "Tejasv Lata", true), 0);
    testing::internal::CaptureStderr();
    int c2 = bts::cmd_commit(repo_, "msg");
    std::string e2 = testing::internal::GetCapturedStderr();
    EXPECT_NE(c2, 0);
    EXPECT_NE(e2.find("user.email"), std::string::npos);
    EXPECT_EQ(count_objects(), objs_before);

    ASSERT_EQ(bts::cmd_config(repo_, "user.email", "tejasv@example.com", true), 0);

    // Empty index on unborn branch rejected (REQ-CMT-1)
    ASSERT_TRUE(bts::write_index(repo_, {}));
    testing::internal::CaptureStderr();
    EXPECT_NE(bts::cmd_commit(repo_, "empty"), 0);
    testing::internal::GetCapturedStderr();

    // Valid commit
    ASSERT_EQ(bts::cmd_add(repo_, {"file.txt"}), 0);
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_commit(repo_, "First commit"), 0);
    testing::internal::GetCapturedStdout();

    // Re-committing unchanged index rejected (REQ-CMT-1)
    testing::internal::CaptureStderr();
    EXPECT_NE(bts::cmd_commit(repo_, "Duplicate commit"), 0);
    testing::internal::GetCapturedStderr();
}

TEST_F(AddStatusCommitTest, NestedTreesDetachedCommitAndCheckoutRestore) {
    ASSERT_EQ(bts::cmd_config(repo_, "user.name", "Tejasv Lata", true), 0);
    ASSERT_EQ(bts::cmd_config(repo_, "user.email", "tejasv@example.com", true), 0);

    fs::create_directories(repo_ / "src" / "sub");
    {
        std::ofstream f(repo_ / "src" / "sub" / "tool.sh");
        f << "#!/bin/sh\necho v1\n";
    }
    fs::permissions(repo_ / "src" / "sub" / "tool.sh",
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);
    fs::create_symlink("sub/tool.sh", repo_ / "src" / "link");

    ASSERT_EQ(bts::cmd_add(repo_, {"src"}), 0);
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_commit(repo_, "Add nested dir"), 0);
    testing::internal::GetCapturedStdout();

    auto c1_hash = bts::resolve_head(repo_);
    ASSERT_TRUE(c1_hash.has_value());
    EXPECT_FALSE(fs::exists(repo_ / ".bts" / "refs" / "heads" / "main.lock"));

    // Create branch side, checkout side, modify tool.sh to regular file, commit
    ASSERT_TRUE(bts::update_branch_ref(repo_, "side", *c1_hash));
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_checkout(repo_, "side"), 0);
    testing::internal::GetCapturedStdout();

    {
        std::ofstream f(repo_ / "src" / "sub" / "tool.sh");
        f << "#!/bin/sh\necho v2\n";
    }
    fs::permissions(repo_ / "src" / "sub" / "tool.sh",
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
    ASSERT_EQ(bts::cmd_add(repo_, {"src/sub/tool.sh"}), 0);
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_commit(repo_, "Update tool on side"), 0);
    testing::internal::GetCapturedStdout();

    // Checkout main -> nested trees must restore executable bit and symlink cleanly (REQ-CHK-1, REQ-ADD-4)
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_checkout(repo_, "main"), 0);
    testing::internal::GetCapturedStdout();

    auto restored_perms = fs::status(repo_ / "src" / "sub" / "tool.sh").permissions();
    EXPECT_NE(restored_perms & fs::perms::owner_exec, fs::perms::none);
    EXPECT_TRUE(fs::is_symlink(repo_ / "src" / "link"));

    // Status immediately after checkout should be clean with 0 re-hashes (REQ-STAT-2)
    bts::reset_status_hash_count();
    testing::internal::CaptureStdout();
    EXPECT_EQ(bts::cmd_status(repo_), 0);
    std::string status_out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(bts::get_status_hash_count(), 0u);
    EXPECT_NE(status_out.find("nothing to commit"), std::string::npos);

    // Detached HEAD commit (REQ-CMT-5)
    testing::internal::CaptureStderr();
    ASSERT_EQ(bts::cmd_checkout(repo_, c1_hash->substr(0, 6)), 0);
    testing::internal::GetCapturedStderr();
    EXPECT_TRUE(bts::is_head_detached(repo_));

    {
        std::ofstream d(repo_ / "detached.txt");
        d << "detached work\n";
    }
    ASSERT_EQ(bts::cmd_add(repo_, {"detached.txt"}), 0);

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    int det_rc = bts::cmd_commit(repo_, "Detached commit");
    std::string det_out = testing::internal::GetCapturedStdout();
    std::string det_err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(det_rc, 0);
    EXPECT_NE(det_err.find("detached"), std::string::npos);
    EXPECT_NE(det_err.find("not on any branch"), std::string::npos);

    auto c_det = bts::resolve_head(repo_);
    ASSERT_TRUE(c_det.has_value());
    EXPECT_EQ(bts::read_head(repo_), *c_det);
    auto c_det_raw = bts::read_object(repo_, *c_det);
    ASSERT_TRUE(c_det_raw.has_value());
    auto c_det_data = bts::parse_commit(*c_det_raw);
    ASSERT_EQ(c_det_data.parents.size(), 1u);
    EXPECT_EQ(c_det_data.parents[0], *c1_hash);
}

// ─── Object & Index integrity exit status 3 (REQ-OBJ-4, REQ-ERR-3, RR-2) ─────

TEST_F(AddStatusCommitTest, CorruptObjectYieldsExitCode3WithHash) {
    ASSERT_EQ(bts::cmd_config(repo_, "user.name", "Tejasv Lata", true), 0);
    ASSERT_EQ(bts::cmd_config(repo_, "user.email", "tejasv@example.com", true), 0);

    {
        std::ofstream f(repo_ / "data.txt");
        f << "important\n";
    }
    ASSERT_EQ(bts::cmd_add(repo_, {"data.txt"}), 0);
    testing::internal::CaptureStdout();
    ASSERT_EQ(bts::cmd_commit(repo_, "Commit data"), 0);
    testing::internal::GetCapturedStdout();

    auto idx = bts::read_index(repo_);
    ASSERT_EQ(idx.size(), 1u);
    std::string blob_hash = idx[0].sha256;
    fs::path obj_path = repo_ / ".bts" / "objects" / blob_hash.substr(0, 2) / blob_hash.substr(2);

    fs::permissions(obj_path, fs::perms::owner_all, fs::perm_options::add);
    {
        std::ofstream out(obj_path, std::ios::binary | std::ios::trunc);
        out << "corrupted zlib payload";
    }

    // Trigger read of corrupted blob via `bts diff` after modifying working tree
    {
        std::ofstream f(repo_ / "data.txt");
        f << "changed\n";
    }

    testing::internal::CaptureStderr();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("diff")};
    int rc = bts::cli_main(2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 3);
    EXPECT_NE(err.find(blob_hash), std::string::npos);
}

