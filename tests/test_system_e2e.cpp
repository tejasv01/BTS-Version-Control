#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "bts/cli.h"
#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace fs = std::filesystem;

namespace {

int run_cli(const std::vector<std::string>& args) {
    std::vector<std::string> storage = args;
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& s : storage) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    return bts::cli_main(static_cast<int>(argv.size()), argv.data());
}

void write_file_str(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
}

}  // namespace

class SystemE2ETest : public ::testing::Test {
protected:
    fs::path repo_;
    fs::path prev_cwd_;

    void SetUp() override {
        prev_cwd_ = fs::current_path();
        repo_ = fs::temp_directory_path() /
                ("bts_test_e2e_" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()));
        fs::create_directories(repo_);
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

// ─── Full end-to-end CLI workflow (init -> config -> add -> commit -> branch ->
//     checkout -> diff -> log -> integrity exit 3) ────────────────────────────

TEST_F(SystemE2ETest, FullWorkflowAndIntegrityExitCode3) {
    ASSERT_EQ(run_cli({"bts", "init"}), 0);
    ASSERT_EQ(run_cli({"bts", "config", "user.name", "Tejasv Lata"}), 0);
    ASSERT_EQ(run_cli({"bts", "config", "user.email", "tejasv@example.com"}), 0);

    write_file_str(repo_ / "README.md", "line 1\nline 2\n");
    write_file_str(repo_ / "src" / "app.cpp", "int main() { return 0; }\n");
    ASSERT_EQ(run_cli({"bts", "add", "."}), 0);

    {
        testing::internal::CaptureStdout();
        ASSERT_EQ(run_cli({"bts", "status"}), 0);
        std::string st = testing::internal::GetCapturedStdout();
        EXPECT_NE(st.find("no commits yet"), std::string::npos);
        EXPECT_NE(st.find("new file:   README.md"), std::string::npos);
    }

    ASSERT_EQ(run_cli({"bts", "commit", "-m", "Initial import"}), 0);
    ASSERT_EQ(run_cli({"bts", "branch", "feature"}), 0);
    ASSERT_EQ(run_cli({"bts", "checkout", "feature"}), 0);

    write_file_str(repo_ / "README.md", "line 1\nline two\nline 3\n");
    {
        testing::internal::CaptureStdout();
        ASSERT_EQ(run_cli({"bts", "diff"}), 0);
        std::string diff_out = testing::internal::GetCapturedStdout();
        EXPECT_NE(diff_out.find("--- a/README.md"), std::string::npos);
        EXPECT_NE(diff_out.find("+++ b/README.md"), std::string::npos);
        EXPECT_NE(diff_out.find("-line 2"), std::string::npos);
        EXPECT_NE(diff_out.find("+line two"), std::string::npos);
    }

    ASSERT_EQ(run_cli({"bts", "add", "README.md"}), 0);
    ASSERT_EQ(run_cli({"bts", "commit", "-m", "Feature edit"}), 0);

    {
        testing::internal::CaptureStdout();
        ASSERT_EQ(run_cli({"bts", "log", "--oneline"}), 0);
        std::string log_out = testing::internal::GetCapturedStdout();
        auto pos_feat = log_out.find("Feature edit");
        auto pos_init = log_out.find("Initial import");
        EXPECT_NE(pos_feat, std::string::npos);
        EXPECT_NE(pos_init, std::string::npos);
        EXPECT_LT(pos_feat, pos_init);
    }

    // Corrupt stored index footer byte -> CLI must exit 3 (REQ-ERR-3).
    {
        fs::path idx_path = repo_ / ".bts" / "index";
        std::fstream fs_idx(idx_path,
                            std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(fs_idx.is_open());
        fs_idx.seekg(-1, std::ios::end);
        char last = 0;
        fs_idx.get(last);
        fs_idx.seekp(-1, std::ios::end);
        char corrupted = static_cast<char>(last ^ 0xFF);
        fs_idx.put(corrupted);
        fs_idx.flush();
    }

    testing::internal::CaptureStderr();
    int corrupt_rc = run_cli({"bts", "status"});
    std::string corrupt_err = testing::internal::GetCapturedStderr();
    EXPECT_EQ(corrupt_rc, 3);
    EXPECT_FALSE(corrupt_err.empty());
}

#ifndef _WIN32
// ─── Process-level SIGKILL crash-safety fault injection (RR-1) ───────────────

TEST_F(SystemE2ETest, FaultInjection_SigkillPreservesConsistentRepoState) {
    ASSERT_EQ(run_cli({"bts", "init"}), 0);
    ASSERT_EQ(run_cli({"bts", "config", "user.name", "Tester"}), 0);
    ASSERT_EQ(run_cli({"bts", "config", "user.email", "t@example.com"}), 0);
    write_file_str(repo_ / "seed.txt", "seed content\n");
    ASSERT_EQ(run_cli({"bts", "add", "seed.txt"}), 0);
    ASSERT_EQ(run_cli({"bts", "commit", "-m", "Seed commit"}), 0);

    // Fork a child repeatedly creating files, staging, and committing; kill it
    // asynchronously with SIGKILL to simulate sudden power loss / crash.
    pid_t pid = ::fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        for (int iter = 0; iter < 500; ++iter) {
            std::string name = "burst_" + std::to_string(iter) + ".txt";
            write_file_str(repo_ / name,
                           std::string(4096, static_cast<char>('a' + (iter % 26))));
            (void)run_cli({"bts", "add", name});
            (void)run_cli({"bts", "commit", "-m", "Iter " + std::to_string(iter)});
        }
        ::_exit(0);
    }

    ::usleep(4000);  // interrupt mid-execution
    ::kill(pid, SIGKILL);
    int status = 0;
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);

    // Clean stale lock files if child died while holding one, then verify all
    // atomic committed state (HEAD, branch ref, index, commit objects) is valid.
    std::error_code ec;
    fs::remove(repo_ / ".bts" / "index.lock", ec);
    fs::remove(repo_ / ".bts" / "HEAD.lock", ec);
    fs::remove(repo_ / ".bts" / "refs" / "heads" / "main.lock", ec);

    // 1. Index must deserialize cleanly and pass SHA-256 footer check.
    std::vector<bts::IndexEntry> idx;
    EXPECT_NO_THROW(idx = bts::read_index(repo_));
    EXPECT_GE(idx.size(), 1u);

    // 2. HEAD and current branch commit chain must resolve and verify hashes.
    auto head_hash = bts::resolve_head(repo_);
    ASSERT_TRUE(head_hash.has_value());
    EXPECT_NO_THROW({
        auto commit_obj = bts::read_object(repo_, *head_hash);
        ASSERT_TRUE(commit_obj.has_value());
        bts::CommitData cd = bts::parse_commit(*commit_obj);
        EXPECT_FALSE(cd.tree_hash.empty());
        EXPECT_TRUE(bts::read_object(repo_, cd.tree_hash).has_value());
    });
}
#endif
