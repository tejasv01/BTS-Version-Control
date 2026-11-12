#include <gtest/gtest.h>
#include "../../src/storage/object_store.hpp"
#include "../../src/storage/hash.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace bts {
namespace {

class ObjectStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("bts_test_store_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(test_dir_ / ".bts" / "objects");
    }

    void TearDown() override {
        std::error_code ec;
        // Restore write permissions if needed before deleting test directory
        for (auto& entry : std::filesystem::recursive_directory_iterator(test_dir_, ec)) {
            std::filesystem::permissions(entry.path(), std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec);
        }
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
};

TEST_F(ObjectStoreTest, HashObjectFormat) {
    std::string payload = "Hello world\n";
    // Header should be "blob 12\0Hello world\n"
    std::string expected_header = "blob " + std::to_string(payload.size());
    std::string expected_raw = expected_header + '\0' + payload;
    std::string expected_hash = sha256(expected_raw);

    EXPECT_EQ(hash_object(ObjectType::Blob, payload), expected_hash);
}

TEST_F(ObjectStoreTest, WriteAndReadRoundTripBlob) {
    std::string content = "This is a test blob file content.";
    std::string hash = write_object(test_dir_.string(), ObjectType::Blob, content);

    EXPECT_TRUE(is_valid_sha256_hex(hash));
    EXPECT_TRUE(object_exists(test_dir_.string(), hash));

    // Verify path layout: .bts/objects/xx/yyyy...
    std::filesystem::path expected_path = test_dir_ / ".bts" / "objects" / hash.substr(0, 2) / hash.substr(2);
    EXPECT_TRUE(std::filesystem::exists(expected_path));

    Object obj = read_object(test_dir_.string(), hash);
    EXPECT_EQ(obj.type, ObjectType::Blob);
    EXPECT_EQ(obj.data, content);
}

TEST_F(ObjectStoreTest, WriteAndReadRoundTripTreeAndCommit) {
    std::string tree_payload = "100644 file.txt\0" + std::string(32, '\x01');
    std::string tree_hash = write_object(test_dir_.string(), ObjectType::Tree, tree_payload);

    Object tree_obj = read_object(test_dir_.string(), tree_hash);
    EXPECT_EQ(tree_obj.type, ObjectType::Tree);
    EXPECT_EQ(tree_obj.data, tree_payload);

    std::string commit_payload = "tree " + tree_hash + "\nauthor Bharat <b@example.com> 123 +0000\n\ninit\n";
    std::string commit_hash = write_object(test_dir_.string(), ObjectType::Commit, commit_payload);

    Object commit_obj = read_object(test_dir_.string(), commit_hash);
    EXPECT_EQ(commit_obj.type, ObjectType::Commit);
    EXPECT_EQ(commit_obj.data, commit_payload);
}

TEST_F(ObjectStoreTest, DeduplicationDoesNotOverwrite) {
    std::string data = "unique immutable content";
    std::string hash1 = write_object(test_dir_.string(), ObjectType::Blob, data);
    std::filesystem::path obj_path = get_object_path(test_dir_.string(), hash1);
    auto write_time1 = std::filesystem::last_write_time(obj_path);

    // Write same object again: should skip write and return identical hash (SR-1, REQ-ADD-1)
    std::string hash2 = write_object(test_dir_.string(), ObjectType::Blob, data);
    EXPECT_EQ(hash1, hash2);
    auto write_time2 = std::filesystem::last_write_time(obj_path);
    EXPECT_EQ(write_time1, write_time2);
}

TEST_F(ObjectStoreTest, MissingObjectThrowsNotFound) {
    std::string non_existent = "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";
    EXPECT_FALSE(object_exists(test_dir_.string(), non_existent));
    EXPECT_THROW({
        try {
            read_object(test_dir_.string(), non_existent);
        } catch (const ObjectNotFoundException& e) {
            EXPECT_EQ(e.hash(), non_existent);
            throw;
        }
    }, ObjectNotFoundException);
}

TEST_F(ObjectStoreTest, CorruptObjectThrowsIntegrityException) {
    std::string content = "Sensitive data to protect against corruption";
    std::string hash = write_object(test_dir_.string(), ObjectType::Blob, content);
    std::filesystem::path obj_path = get_object_path(test_dir_.string(), hash);

    // Make file writable temporarily to simulate disk corruption or tampering
    std::error_code ec;
    std::filesystem::permissions(obj_path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec);

    // Tamper with file contents by writing corrupted bytes
    {
        std::fstream file(obj_path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file.seekp(5, std::ios::beg);
        char corrupt_byte = '\x55';
        file.write(&corrupt_byte, 1);
        file.flush();
    }

    // Reading tampered object must throw IntegrityException or CorruptObjectException
    EXPECT_THROW({
        read_object(test_dir_.string(), hash);
    }, StorageException);
}

TEST_F(ObjectStoreTest, AtomicWriteLeavesNoTempFiles) {
    std::string content = "Atomic write verification";
    std::string hash = write_object(test_dir_.string(), ObjectType::Blob, content);

    std::filesystem::path dir = test_dir_ / ".bts" / "objects" / hash.substr(0, 2);
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        std::string filename = entry.path().filename().string();
        EXPECT_FALSE(filename.rfind(".tmp_", 0) == 0) << "Found leftover temporary file: " << filename;
    }
}

} // namespace
} // namespace bts
