#pragma once

#include <string>
#include <vector>

namespace bts {

/**
 * @brief Represents a single entry inside a Tree object (Appendix A).
 */
struct TreeEntry {
    std::string mode;       // e.g. "100644" (regular), "100755" (executable), "120000" (symlink), "40000" (tree)
    std::string name;       // file or directory name (no path separators)
    std::string sha256_hex; // 64-character lowercase hex digest of target object

    bool operator==(const TreeEntry& other) const {
        return mode == other.mode && name == other.name && sha256_hex == other.sha256_hex;
    }
};

/**
 * @brief Represents a Tree object listing directory entries (Appendix A).
 * Entries are sorted by name in byte order upon serialization.
 */
struct Tree {
    std::vector<TreeEntry> entries;

    /**
     * @brief Serializes the Tree payload into canonical binary format:
     * repeated "<mode> <name>\0" + <32-byte raw SHA-256>, sorted by name in byte order.
     * @return Serialized payload string.
     */
    std::string serialize() const;

    /**
     * @brief Deserializes a Tree payload from raw bytes.
     * @param data Binary payload data.
     * @return Parsed Tree structure.
     * @throws CorruptObjectException if the payload is malformed.
     */
    static Tree deserialize(const std::string& data);
};

} // namespace bts
