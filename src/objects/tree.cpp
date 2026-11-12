#include "tree.hpp"
#include "../storage/hash.hpp"
#include "../storage/object_store.hpp"

#include <algorithm>

namespace bts {

std::string Tree::serialize() const {
    // Sort entries by name in byte order per Appendix A
    std::vector<TreeEntry> sorted_entries = entries;
    std::sort(sorted_entries.begin(), sorted_entries.end(), [](const TreeEntry& a, const TreeEntry& b) {
        return a.name < b.name;
    });

    std::string payload;
    for (const auto& entry : sorted_entries) {
        payload.append(entry.mode);
        payload.push_back(' ');
        payload.append(entry.name);
        payload.push_back('\0');
        payload.append(hex_to_raw(entry.sha256_hex));
    }
    return payload;
}

Tree Tree::deserialize(const std::string& data) {
    Tree tree;
    std::size_t pos = 0;
    const std::size_t total_size = data.size();

    while (pos < total_size) {
        std::size_t space_pos = data.find(' ', pos);
        if (space_pos == std::string::npos) {
            throw CorruptObjectException("Tree parsing failed: missing space after mode at offset " + std::to_string(pos));
        }

        std::string mode = data.substr(pos, space_pos - pos);

        std::size_t null_pos = data.find('\0', space_pos + 1);
        if (null_pos == std::string::npos) {
            throw CorruptObjectException("Tree parsing failed: missing null terminator after name at offset " + std::to_string(space_pos));
        }

        std::string name = data.substr(space_pos + 1, null_pos - space_pos - 1);

        std::size_t raw_sha_offset = null_pos + 1;
        if (raw_sha_offset + 32 > total_size) {
            throw CorruptObjectException("Tree parsing failed: truncated 32-byte raw hash at offset " + std::to_string(raw_sha_offset));
        }

        std::string raw_sha = data.substr(raw_sha_offset, 32);
        std::string sha256_hex = raw_to_hex(raw_sha);

        tree.entries.push_back(TreeEntry{std::move(mode), std::move(name), std::move(sha256_hex)});
        pos = raw_sha_offset + 32;
    }

    return tree;
}

} // namespace bts
