#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/cli.h"

namespace fs = std::filesystem;

class CliTest : public ::testing::Test {
protected:
    fs::path temp_dir_;
    fs::path prev_cwd_;

    void SetUp() override {
        prev_cwd_ = fs::current_path();
        temp_dir_ = fs::temp_directory_path() /
                    ("bts_test_cli_" + std::to_string(std::chrono::steady_clock::now()
                                                          .time_since_epoch()
                                                          .count()));
        fs::create_directories(temp_dir_);
        fs::current_path(temp_dir_);
    }

    void TearDown() override {
        fs::current_path(prev_cwd_);
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(
                 temp_dir_, fs::directory_options::skip_permission_denied, ec)) {
            fs::permissions(entry.path(), fs::perms::owner_all,
                           fs::perm_options::add, ec);
        }
        fs::remove_all(temp_dir_, ec);
    }
};

// ─── No arguments / --help (REQ-ERR-2) ───────────────────────────────────────

TEST_F(CliTest, Cli_NoArgs) {
    testing::internal::CaptureStderr();
    char* argv[] = {const_cast<char*>("bts")};
    int rc = bts::cli_main(1, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 2);
    EXPECT_NE(err.find("usage: bts"), std::string::npos);
}

TEST_F(CliTest, Cli_Help) {
    testing::internal::CaptureStderr();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("--help")};
    int rc = bts::cli_main(2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 2);
    EXPECT_NE(err.find("Available commands:"), std::string::npos);
}

TEST_F(CliTest, Cli_UnknownCommand) {
    testing::internal::CaptureStderr();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("unknowncmd")};
    int rc = bts::cli_main(2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 2);
    EXPECT_NE(err.find("is not a bts command"), std::string::npos);
}

// ─── Outside repo (REQ-ERR-1) ────────────────────────────────────────────────

TEST_F(CliTest, Cli_OutsideRepo) {
    testing::internal::CaptureStderr();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("status")};
    int rc = bts::cli_main(2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.find("not a bts repository"), std::string::npos);
}

// ─── Init (REQ-INIT-1, REQ-INIT-2) ───────────────────────────────────────────

TEST_F(CliTest, Cli_Init) {
    testing::internal::CaptureStdout();
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    int rc = bts::cli_main(2, argv);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(temp_dir_ / ".bts"));
    EXPECT_TRUE(fs::exists(temp_dir_ / ".bts" / "objects"));
    EXPECT_TRUE(fs::exists(temp_dir_ / ".bts" / "refs" / "heads"));
    EXPECT_TRUE(fs::exists(temp_dir_ / ".bts" / "HEAD"));
}

TEST_F(CliTest, Cli_InitAlreadyExists) {
    char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    EXPECT_EQ(bts::cli_main(2, argv), 0);

    testing::internal::CaptureStderr();
    int rc = bts::cli_main(2, argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.find("already"), std::string::npos);
}

// ─── Config (REQ-CFG-1, REQ-CFG-3) ───────────────────────────────────────────

TEST_F(CliTest, Cli_Config) {
    char* init_argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    ASSERT_EQ(bts::cli_main(2, init_argv), 0);

    char* set_argv[] = {const_cast<char*>("bts"), const_cast<char*>("config"),
                        const_cast<char*>("user.name"), const_cast<char*>("Sarthak")};
    EXPECT_EQ(bts::cli_main(4, set_argv), 0);

    testing::internal::CaptureStdout();
    char* get_argv[] = {const_cast<char*>("bts"), const_cast<char*>("config"),
                        const_cast<char*>("user.name")};
    EXPECT_EQ(bts::cli_main(3, get_argv), 0);
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("Sarthak"), std::string::npos);
}

// ─── Branch list in initialized repo ─────────────────────────────────────────

TEST_F(CliTest, Cli_BranchInRepo) {
    char* init_argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    ASSERT_EQ(bts::cli_main(2, init_argv), 0);

    char* br_argv[] = {const_cast<char*>("bts"), const_cast<char*>("branch")};
    EXPECT_EQ(bts::cli_main(2, br_argv), 0);
}

// ─── Subdirectory repo discovery (REQ-ERR-1) ─────────────────────────────────

TEST_F(CliTest, Cli_FindsRepoFromSubdirectory) {
    char* init_argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    ASSERT_EQ(bts::cli_main(2, init_argv), 0);

    fs::path nested = temp_dir_ / "src" / "deep" / "module";
    fs::create_directories(nested);
    fs::current_path(nested);

    testing::internal::CaptureStdout();
    char* st_argv[] = {const_cast<char*>("bts"), const_cast<char*>("status")};
    int rc = bts::cli_main(2, st_argv);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.find("On branch main"), std::string::npos);
}

// ─── Argument validation (REQ-ERR-2) & exit code 1 errors (REQ-ERR-4) ────────

TEST_F(CliTest, Cli_MalformedFlagsAndExtraArgsExit2) {
    char* init_argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    ASSERT_EQ(bts::cli_main(2, init_argv), 0);

    {
        testing::internal::CaptureStderr();
        char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("log"),
                        const_cast<char*>("--bogus")};
        EXPECT_EQ(bts::cli_main(3, argv), 2);
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
    {
        testing::internal::CaptureStderr();
        char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("commit"),
                        const_cast<char*>("-m")};
        EXPECT_EQ(bts::cli_main(3, argv), 2);
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
    {
        testing::internal::CaptureStderr();
        char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("commit"),
                        const_cast<char*>("-m"), const_cast<char*>("msg"),
                        const_cast<char*>("--unknown")};
        EXPECT_EQ(bts::cli_main(5, argv), 2);
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
    {
        testing::internal::CaptureStderr();
        char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("status"),
                        const_cast<char*>("unexpected")};
        EXPECT_EQ(bts::cli_main(3, argv), 2);
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
    {
        testing::internal::CaptureStderr();
        char* argv[] = {const_cast<char*>("bts"), const_cast<char*>("branch"),
                        const_cast<char*>("-d")};
        EXPECT_EQ(bts::cli_main(3, argv), 2);
        EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    }
}

TEST_F(CliTest, Cli_OperationFailureExits1) {
    char* init_argv[] = {const_cast<char*>("bts"), const_cast<char*>("init")};
    ASSERT_EQ(bts::cli_main(2, init_argv), 0);

    testing::internal::CaptureStderr();
    char* add_argv[] = {const_cast<char*>("bts"), const_cast<char*>("add"),
                        const_cast<char*>("missing_file.txt")};
    int rc = bts::cli_main(3, add_argv);
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_EQ(rc, 1);
    EXPECT_FALSE(err.empty());
}
