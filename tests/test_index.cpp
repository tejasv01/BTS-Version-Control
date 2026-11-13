#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bts/index.h"
#include "bts/object_store.h"
#include "../src/storage/hash.hpp"

namespace fs = std::filesystem;

class IndexTest : public ::testing::Test {
protected:
    fs::path repo_;

    void SetUp() override {
        repo_ = fs::temp_directory_path() /
                ("bts_test_index_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(repo_ / ".bts" / "objects");
        fs::create_directories(repo_ / ".bts" / "refs" / "heads");
        {
            std::ofstream head(repo_ / ".bts" / "HEAD");
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

    static uint32_t read_u32_be(const std::string& data, std::size_t off) {
        return (static_cast<uint32_t>(static_cast<unsigned char>(data[off])) << 24) |
               (static_cast<uint32_t>(static_cast<unsigned char>(data[off + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(data[off + 2])) << 8) |
               static_cast<uint32_t>(static_cast<unsigned char>(data[off + 3]));
    }
};

// ─── Empty and non-existent index ────────────────────────────────────────────

TEST_F(IndexTest, EmptyAndMissingIndex) {
    EXPECT_TRUE(bts::read_index(repo_).empty());

    {
        std::ofstream empty_idx(repo_ / ".bts" / "index", std::ios::binary);
    }
    EXPECT_TRUE(bts::read_index(repo_).empty());
}

// ─── Binary format round-trip, sorting, version, and SHA-256 footer ──────────

TEST_F(IndexTest, BinaryFormatRoundTripAndSorting) {
    bts::IndexEntry e1{100755, 1700000001ULL, 123456789U, 42ULL,
                       bts::sha256("exec content"), "z/run.sh"};
    bts::IndexEntry e2{100644, 1700000002ULL, 987654321U, 100ULL,
                       bts::sha256("regular content"), "a/hello.txt"};
    bts::IndexEntry e3{120000, 1700000003ULL, 555U, 11ULL,
                       bts::sha256("target_path"), "m/link"};

    ASSERT_TRUE(bts::write_index(repo_, {e1, e2, e3}));
    EXPECT_FALSE(fs::exists(repo_ / ".bts" / "index.lock"));

    // Inspect raw binary bytes per Appendix B
    std::ifstream in(repo_ / ".bts" / "index", std::ios::binary);
    ASSERT_TRUE(in.is_open());
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());

    ASSERT_GT(raw.size(), 44u);
    EXPECT_EQ(raw.substr(0, 4), "BTSI");
    EXPECT_EQ(read_u32_be(raw, 4), 1u);
    EXPECT_EQ(read_u32_be(raw, 8), 3u);

    // Verify SHA-256 footer over all preceding bytes
    std::string payload = raw.substr(0, raw.size() - 32);
    std::string footer = raw.substr(raw.size() - 32);
    EXPECT_EQ(footer, bts::sha256_raw(payload));

    // Verify round-trip and sorted order by path
    auto parsed = bts::read_index(repo_);
    ASSERT_EQ(parsed.size(), 3u);
    EXPECT_EQ(parsed[0], e2);
    EXPECT_EQ(parsed[1], e3);
    EXPECT_EQ(parsed[2], e1);
}

// ─── POSIX separator normalization (PO-1) ────────────────────────────────────

TEST_F(IndexTest, NormalizesSeparatorsToPosix) {
    bts::IndexEntry e{100644, 100ULL, 0U, 5ULL,
                      bts::sha256("hello"), "dir\\sub\\file.txt"};
    ASSERT_TRUE(bts::write_index(repo_, {e}));

    auto parsed = bts::read_index(repo_);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].path, "dir/sub/file.txt");
    EXPECT_EQ(parsed[0].path.find('\\'), std::string::npos);
}

// ─── Integrity checks: checksum, magic, truncated, version (REQ-ERR-3, MNT-3)

TEST_F(IndexTest, CorruptedChecksumThrowsIntegrityException) {
    bts::IndexEntry e{100644, 1700000000ULL, 0U, 3ULL,
                      bts::sha256("abc"), "file.txt"};
    ASSERT_TRUE(bts::write_index(repo_, {e}));

    fs::path idx_path = repo_ / ".bts" / "index";
    std::ifstream in(idx_path, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    in.close();

    // Flip last byte of SHA-256 footer
    raw.back() ^= 0xFF;
    {
        std::ofstream out(idx_path, std::ios::binary | std::ios::trunc);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }

    EXPECT_THROW(bts::read_index(repo_), bts::IndexIntegrityException);
}

TEST_F(IndexTest, InvalidMagicAndTruncatedThrow) {
    fs::path idx_path = repo_ / ".bts" / "index";
    {
        std::ofstream out(idx_path, std::ios::binary | std::ios::trunc);
        std::string bad(50, 'x');
        out.write(bad.data(), static_cast<std::streamsize>(bad.size()));
    }
    EXPECT_THROW(bts::read_index(repo_), bts::IndexIntegrityException);

    {
        std::ofstream out(idx_path, std::ios::binary | std::ios::trunc);
        out << "BTSIshort";
    }
    EXPECT_THROW(bts::read_index(repo_), bts::IndexIntegrityException);
}

TEST_F(IndexTest, UnsupportedVersionThrows) {
    bts::IndexEntry e{100644, 1700000000ULL, 0U, 3ULL,
                      bts::sha256("abc"), "file.txt"};
    ASSERT_TRUE(bts::write_index(repo_, {e}));

    fs::path idx_path = repo_ / ".bts" / "index";
    std::ifstream in(idx_path, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    in.close();

    // Change version from 1 to 2 and recompute footer checksum
    raw[7] = 2;
    std::string payload = raw.substr(0, raw.size() - 32);
    std::string new_footer = bts::sha256_raw(payload);
    raw.replace(raw.size() - 32, 32, new_footer);

    {
        std::ofstream out(idx_path, std::ios::binary | std::ios::trunc);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }

    EXPECT_THROW(bts::read_index(repo_), bts::IndexIntegrityException);
}

// ─── Lock file protection (RR-3) ─────────────────────────────────────────────

TEST_F(IndexTest, ExistingLockFileBlocksWrite) {
    bts::IndexEntry initial{100644, 1ULL, 0U, 1ULL, bts::sha256("a"), "a.txt"};
    ASSERT_TRUE(bts::write_index(repo_, {initial}));

    {
        std::ofstream lock(repo_ / ".bts" / "index.lock");
        lock << "locked";
    }

    bts::IndexEntry updated{100644, 2ULL, 0U, 2ULL, bts::sha256("bb"), "b.txt"};
    testing::internal::CaptureStderr();
    bool ok = bts::write_index(repo_, {updated});
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("index.lock"), std::string::npos);

    auto read_back = bts::read_index(repo_);
    ASSERT_EQ(read_back.size(), 1u);
    EXPECT_EQ(read_back[0], initial);
}

