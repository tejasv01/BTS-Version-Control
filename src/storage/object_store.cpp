#include "object_store.hpp"
#include "compress.hpp"
#include "hash.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace bts {

namespace {

void sync_file(FILE* fp) {
    if (!fp) return;
    fflush(fp);
#if defined(_WIN32)
    int fd = _fileno(fp);
    if (fd >= 0) {
        _commit(fd);
    }
#else
    int fd = fileno(fp);
    if (fd >= 0) {
        fsync(fd);
    }
#endif
}

struct TempFileRemover {
    std::filesystem::path path;
    bool committed = false;

    ~TempFileRemover() {
        if (!committed && !path.empty()) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

} // namespace

std::string object_type_to_string(ObjectType type) {
    switch (type) {
        case ObjectType::Blob:
            return "blob";
        case ObjectType::Tree:
            return "tree";
        case ObjectType::Commit:
            return "commit";
    }
    throw std::invalid_argument("Unknown ObjectType enum");
}

ObjectType string_to_object_type(const std::string& type_str) {
    if (type_str == "blob") {
        return ObjectType::Blob;
    }
    if (type_str == "tree") {
        return ObjectType::Tree;
    }
    if (type_str == "commit") {
        return ObjectType::Commit;
    }
    throw std::invalid_argument("Invalid object type string: '" + type_str + "'");
}

std::filesystem::path get_object_path(const std::string& repo_root, const std::string& hash) {
    if (hash.size() < 2) {
        throw std::invalid_argument("Object hash too short to form path: " + hash);
    }
    std::string dir_name = hash.substr(0, 2);
    std::string file_name = hash.substr(2);
    return std::filesystem::path(repo_root) / ".bts" / "objects" / dir_name / file_name;
}

bool object_exists(const std::string& repo_root, const std::string& hash) {
    if (!is_valid_sha256_hex(hash)) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(get_object_path(repo_root, hash), ec);
}

std::string hash_object(ObjectType type, const std::string& data) {
    std::string header = object_type_to_string(type) + " " + std::to_string(data.size());
    std::string full_content;
    full_content.reserve(header.size() + 1 + data.size());
    full_content.append(header);
    full_content.push_back('\0');
    full_content.append(data);
    return sha256(full_content);
}

std::string write_object(const std::string& repo_root, ObjectType type, const std::string& data) {
    std::string header = object_type_to_string(type) + " " + std::to_string(data.size());
    std::string uncompressed;
    uncompressed.reserve(header.size() + 1 + data.size());
    uncompressed.append(header);
    uncompressed.push_back('\0');
    uncompressed.append(data);

    std::string hash = sha256(uncompressed);
    std::filesystem::path target_path = get_object_path(repo_root, hash);
    std::filesystem::path target_dir = target_path.parent_path();

    // Per SR-1 and REQ-ADD-1: Never modify or overwrite an existing object.
    std::error_code ec;
    if (std::filesystem::exists(target_path, ec)) {
        return hash;
    }

    if (!std::filesystem::create_directories(target_dir, ec) && ec) {
        throw StorageException("Failed to create object directory: " + target_dir.string() + " (" + ec.message() + ")");
    }

    // Atomic write pattern:
    // 1. Write compressed payload to a temporary file in the same directory.
    // 2. Flush and sync (fsync / _commit) to disk.
    // 3. Close the file.
    // 4. Atomically rename the temporary file to the final object path.
    auto now_epoch = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string temp_filename = ".tmp_" + std::to_string(now_epoch) + "_" + hash.substr(2, 8);
    std::filesystem::path temp_path = target_dir / temp_filename;

    TempFileRemover cleaner{temp_path, false};

    std::string compressed = compress_data(uncompressed, 1);

#if defined(_WIN32)
    FILE* fp = nullptr;
    errno_t err = fopen_s(&fp, temp_path.string().c_str(), "wb");
    if (err != 0 || !fp) {
        throw StorageException("Failed to open temporary object file for writing: " + temp_path.string());
    }
#else
    FILE* fp = fopen(temp_path.c_str(), "wb");
    if (!fp) {
        throw StorageException("Failed to open temporary object file for writing: " + temp_path.string());
    }
#endif

    std::size_t written = fwrite(compressed.data(), 1, compressed.size(), fp);
    if (written != compressed.size()) {
        fclose(fp);
        throw StorageException("Failed to complete write to temporary object file: " + temp_path.string());
    }

    sync_file(fp);
    fclose(fp);

    // Set permission mode to 0444 (read-only for owner, group, others) per SR-1
    std::filesystem::permissions(
        temp_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace,
        ec
    );

    // Atomically rename over the target path
    std::filesystem::rename(temp_path, target_path, ec);
    if (ec) {
        // If target was created concurrently by another process or write, check if target exists
        std::error_code check_ec;
        if (std::filesystem::exists(target_path, check_ec)) {
            // Target exists, our write was redundant
            cleaner.committed = false; // let destructor remove temp file
            return hash;
        }
        throw StorageException("Failed to rename temporary object file to destination: " + ec.message());
    }

    // Ensure permissions are set on target_path
    std::filesystem::permissions(
        target_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace,
        ec
    );

    cleaner.committed = true;
    return hash;
}

Object read_object(const std::string& repo_root, const std::string& hash) {
    if (!is_valid_sha256_hex(hash)) {
        throw std::invalid_argument("Invalid SHA-256 object hash: '" + hash + "'");
    }

    std::filesystem::path obj_path = get_object_path(repo_root, hash);
    std::error_code ec;
    if (!std::filesystem::exists(obj_path, ec)) {
        throw ObjectNotFoundException(hash);
    }

    std::ifstream file(obj_path, std::ios::binary);
    if (!file) {
        throw StorageException("Failed to open object file: " + obj_path.string());
    }

    std::string compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string decompressed;
    try {
        decompressed = decompress_data(compressed);
    } catch (const std::exception& e) {
        throw CorruptObjectException("Decompression failed for object " + hash + ": " + e.what());
    }

    // Verify SHA-256 integrity (REQ-OBJ-4, REQ-ERR-3)
    std::string actual_hash = sha256(decompressed);
    if (actual_hash != hash) {
        throw IntegrityException(hash, actual_hash);
    }

    // Parse header: "<type> <size>\0<payload>"
    std::size_t space_pos = decompressed.find(' ');
    if (space_pos == std::string::npos) {
        throw CorruptObjectException("Malformed object header (missing space): " + hash);
    }

    std::string type_str = decompressed.substr(0, space_pos);
    ObjectType type;
    try {
        type = string_to_object_type(type_str);
    } catch (const std::exception&) {
        throw CorruptObjectException("Unknown object type in header: '" + type_str + "' for object " + hash);
    }

    std::size_t null_pos = decompressed.find('\0', space_pos + 1);
    if (null_pos == std::string::npos) {
        throw CorruptObjectException("Malformed object header (missing null terminator): " + hash);
    }

    std::string size_str = decompressed.substr(space_pos + 1, null_pos - space_pos - 1);
    std::size_t declared_size = 0;
    try {
        std::size_t idx = 0;
        declared_size = std::stoull(size_str, &idx);
        if (idx != size_str.size()) {
            throw std::invalid_argument("trailing characters in size");
        }
    } catch (const std::exception&) {
        throw CorruptObjectException("Invalid object size in header: '" + size_str + "' for object " + hash);
    }

    std::string payload = decompressed.substr(null_pos + 1);
    if (payload.size() != declared_size) {
        throw CorruptObjectException("Object payload size mismatch for " + hash +
                                     ": header states " + std::to_string(declared_size) +
                                     " but found " + std::to_string(payload.size()));
    }

    return Object{type, std::move(payload)};
}

} // namespace bts
