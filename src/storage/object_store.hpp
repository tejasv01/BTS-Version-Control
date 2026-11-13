#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace bts {

/**
 * @brief Types of objects supported by BTS.
 */
enum class ObjectType {
    Blob,
    Tree,
    Commit
};

/**
 * @brief Converts an ObjectType enum to its canonical string representation ("blob", "tree", "commit").
 */
std::string object_type_to_string(ObjectType type);

/**
 * @brief Parses an ObjectType from its string representation.
 * @throws std::invalid_argument if the string is not a valid object type.
 */
ObjectType string_to_object_type(const std::string& type_str);

/**
 * @brief In-memory representation of an uncompressed BTS object.
 */
struct Object {
    ObjectType type;
    std::string data; // raw payload bytes (excluding the "<type> <size>\0" header)
};

/**
 * @brief Base exception for storage-related failures.
 */
class StorageException : public std::runtime_error {
public:
    explicit StorageException(const std::string& message) : std::runtime_error(message) {}
};

/**
 * @brief Exception thrown when an object is not found in the object store.
 */
class ObjectNotFoundException : public StorageException {
public:
    explicit ObjectNotFoundException(const std::string& hash)
        : StorageException("Object not found: " + hash), hash_(hash) {}

    const std::string& hash() const noexcept { return hash_; }

private:
    std::string hash_;
};

/**
 * @brief Exception thrown when an object's SHA-256 digest does not match its expected hash (REQ-OBJ-4, REQ-ERR-3).
 */
class IntegrityException : public StorageException {
public:
    IntegrityException(const std::string& expected_hash, const std::string& actual_hash)
        : StorageException("Integrity check failed for object " + expected_hash +
                           " (computed hash: " + actual_hash + ")"),
          expected_hash_(expected_hash), actual_hash_(actual_hash) {}

    const std::string& expected_hash() const noexcept { return expected_hash_; }
    const std::string& actual_hash() const noexcept { return actual_hash_; }

private:
    std::string expected_hash_;
    std::string actual_hash_;
};

/**
 * @brief Exception thrown when an object file is corrupted or cannot be parsed.
 */
class CorruptObjectException : public StorageException {
public:
    explicit CorruptObjectException(const std::string& message) : StorageException(message) {}
};

/**
 * @brief Computes the SHA-256 hash of an object with its standard header "<type> <size>\0" (REQ-OBJ-1, REQ-OBJ-2).
 * @param type The object type (Blob, Tree, Commit).
 * @param data The raw payload of the object.
 * @return 64-character lowercase hexadecimal SHA-256 hash.
 */
std::string hash_object(ObjectType type, const std::string& data);

/**
 * @brief Writes an object to the repository's content-addressable store (REQ-OBJ-3).
 *
 * Implements atomic writes via temporary file creation, filesystem sync (fsync),
 * and atomic rename. Skips writing if the object already exists (SR-1, REQ-ADD-1).
 * Sets file permissions to read-only (mode 0444, SR-1).
 *
 * @param repo_root Path to the repository root directory (containing .bts/).
 * @param type The object type.
 * @param data The uncompressed object payload.
 * @return The 64-character lowercase SHA-256 hash of the written object.
 * @throws StorageException on write failure.
 */
std::string write_object(const std::string& repo_root, ObjectType type, const std::string& data);

/**
 * @brief Reads and verifies an object from the repository's object store (REQ-OBJ-4).
 *
 * Decompresses the stored file, validates that the SHA-256 digest of header+payload
 * matches the hash, and parses the object into type and payload data.
 *
 * @param repo_root Path to the repository root directory.
 * @param hash The 64-character SHA-256 hexadecimal hash of the object.
 * @return The parsed Object.
 * @throws ObjectNotFoundException if the object file does not exist.
 * @throws IntegrityException if the decompressed content hash does not match hash.
 * @throws CorruptObjectException if decompression fails or header is malformed.
 */
Object read_object(const std::string& repo_root, const std::string& hash);

/**
 * @brief Checks whether an object with the given hash exists in the repository.
 * @param repo_root Path to the repository root directory.
 * @param hash 64-character SHA-256 hex string.
 * @return True if the object file exists on disk, false otherwise.
 */
bool object_exists(const std::string& repo_root, const std::string& hash);

/**
 * @brief Resolves the absolute filesystem path for an object in .bts/objects/xx/yyyy...
 * @param repo_root Path to the repository root directory.
 * @param hash 64-character SHA-256 hex string.
 * @return std::filesystem::path pointing to the object file.
 */
std::filesystem::path get_object_path(const std::string& repo_root, const std::string& hash);

} // namespace bts
