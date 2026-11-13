#include "object_store.hpp"
#include "compress.hpp"
#include "hash.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <random>
#include <stdexcept>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace bts {

namespace {

// Returns true on success; throws StorageException on flush or sync failure.
// Issue #8: propagate fflush / fsync / _commit failures instead of silently swallowing them.
void sync_file(FILE* fp, const std::string& path_hint) {
    if (!fp) return;
    if (fflush(fp) != 0) {
        throw StorageException("fflush failed for temporary object file: " + path_hint);
    }
#if defined(_WIN32)
    int fd = _fileno(fp);
    if (fd >= 0 && _commit(fd) != 0) {
        throw StorageException("_commit (fsync) failed for temporary object file: " + path_hint);
    }
#else
    int fd = fileno(fp);
    if (fd >= 0 && fsync(fd) != 0) {
        throw StorageException("fsync failed for temporary object file: " + path_hint);
    }
#endif
}

// Issue #11: fsync the parent directory after rename so the new directory
// entry is durable. POSIX-only; on Windows, MoveFileExW with
// MOVEFILE_WRITE_THROUGH provides equivalent durability.
#if !defined(_WIN32)
void sync_directory(const std::filesystem::path& dir_path) {
    int fd = open(dir_path.c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
#endif

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
    // Issue #9: validate the full 64-char SHA-256 hex format, not just length >= 2.
    if (!is_valid_sha256_hex(hash)) {
        throw std::invalid_argument("Invalid SHA-256 hash for object path: '" + hash + "'");
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
    // 1. Write compressed payload to a uniquely-named temporary file in the same directory.
    // 2. Flush and fsync the file to disk (propagating errors).
    // 3. Close the file, set read-only permissions.
    // 4. Atomically rename (with write-through on Windows) to the final object path.
    // 5. fsync the parent directory (POSIX) so the new directory entry is durable.

    // Issue #1: use PID + cryptographically seeded random 32-bit value for temp filename
    // uniqueness instead of steady_clock alone (which can collide under high concurrency).
#if defined(_WIN32)
    unsigned long pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<uint32_t> dist;
    std::string temp_filename = ".tmp_" + std::to_string(pid) + "_" + std::to_string(dist(rng)) + "_" + hash.substr(2, 8);
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

    // Issue #8: sync_file now propagates errors; must call before fclose.
    sync_file(fp, temp_path.string());
    fclose(fp);

    // Issue #10: check permissions() error before rename; a failure here means
    // the object would be written without the intended immutability guarantee.
    std::filesystem::permissions(
        temp_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace,
        ec
    );
    if (ec) {
        throw StorageException("Failed to set read-only permissions on temporary object file: " + ec.message());
    }

    // Issues #2 & #11: use a write-through rename on Windows (MoveFileExW with
    // MOVEFILE_WRITE_THROUGH) for durability. On POSIX, follow with a directory fsync.
#if defined(_WIN32)
    bool rename_ok = ::MoveFileExW(
        temp_path.wstring().c_str(),
        target_path.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    );
    if (!rename_ok) {
        // If target already exists (concurrent write), that's fine.
        std::error_code check_ec;
        if (!std::filesystem::exists(target_path, check_ec)) {
            throw StorageException("MoveFileExW failed to rename temporary object file (error " +
                                   std::to_string(::GetLastError()) + ")");
        }
        // Target exists; our write was redundant — let TempFileRemover clean up.
        return hash;
    }
#else
    // Atomically rename over the target path.
    std::filesystem::rename(temp_path, target_path, ec);
    if (ec) {
        std::error_code check_ec;
        if (std::filesystem::exists(target_path, check_ec)) {
            // Target exists from a concurrent write; our write was redundant.
            return hash;
        }
        throw StorageException("Failed to rename temporary object file to destination: " + ec.message());
    }
    // Issue #11: fsync the parent directory so the new directory entry survives a crash.
    sync_directory(target_dir);
#endif

    // Ensure final permissions are set on the target path (Windows rename may not preserve them).
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
