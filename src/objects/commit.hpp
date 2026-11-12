#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bts {

/**
 * @brief Represents a Commit object recording snapshot metadata (Appendix A).
 */
struct Commit {
    std::string tree_hash;                  // 64-hex root tree hash
    std::vector<std::string> parent_hashes; // 64-hex parent commit hashes (zero or more)
    std::string author_name;                // author name
    std::string author_email;               // author email
    std::int64_t timestamp = 0;             // Unix epoch timestamp (seconds)
    std::string timezone;                   // timezone offset string, e.g. "+0000", "+0530", "-0800"
    std::string message;                    // commit message

    bool operator==(const Commit& other) const {
        return tree_hash == other.tree_hash &&
               parent_hashes == other.parent_hashes &&
               author_name == other.author_name &&
               author_email == other.author_email &&
               timestamp == other.timestamp &&
               timezone == other.timezone &&
               message == other.message;
    }

    /**
     * @brief Serializes the Commit payload per Appendix A specification:
     * tree <64-hex>
     * parent <64-hex> (zero or more lines)
     * author <name> <<email>> <epoch> <tz>
     * 
     * <message>
     * @return Serialized payload string.
     */
    std::string serialize() const;

    /**
     * @brief Deserializes a Commit payload from raw string.
     * @param data Payload string.
     * @return Parsed Commit structure.
     * @throws CorruptObjectException if the payload is malformed.
     */
    static Commit deserialize(const std::string& data);
};

} // namespace bts
