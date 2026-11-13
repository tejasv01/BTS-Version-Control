// Staging Index & Porcelain Commands (add, status, commit, diff)
// Owner: Tejasv Lata (Staging Index & Diff Engine)

#ifndef BTS_INDEX_H
#define BTS_INDEX_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace bts {

namespace fs = std::filesystem;

/// Thrown when `.bts/index` has invalid magic, unsupported version, corrupt
/// entries, or a SHA-256 footer mismatch (REQ-ERR-3, MNT-3).
class IndexIntegrityException : public std::runtime_error {
public:
    explicit IndexIntegrityException(const std::string& msg)
        : std::runtime_error(msg) {}
};

struct IndexEntry {
    uint32_t    mode;           // 100644, 100755, 120000
    uint64_t    mtime_sec;
    uint32_t    mtime_nsec;
    uint64_t    file_size;
    std::string sha256;         // 64-hex
    std::string path;           // relative, '/' separators

    bool operator==(const IndexEntry& other) const {
        return mode == other.mode &&
               mtime_sec == other.mtime_sec &&
               mtime_nsec == other.mtime_nsec &&
               file_size == other.file_size &&
               sha256 == other.sha256 &&
               path == other.path;
    }
};

/// Reset / inspect the hash call counter used by `cmd_status` (REQ-STAT-2).
void reset_status_hash_count();
std::size_t get_status_hash_count();

/// Read the binary index file. Verifies magic, version, and SHA-256 footer.
/// Throws IndexIntegrityException on corruption or footer mismatch.
/// Returns entries sorted by path in byte order.
std::vector<IndexEntry> read_index(const fs::path& repo);

/// Write the binary index file atomically (exclusive .bts/index.lock + rename).
bool write_index(const fs::path& repo, const std::vector<IndexEntry>& entries);

/// Stage a file or directory. Returns 0 on success, 1 on failure, 3 on integrity error.
int cmd_add(const fs::path& repo, const std::vector<std::string>& paths);

/// Print status (staged / unstaged / untracked). Returns 0 on success, 3 on integrity error.
int cmd_status(const fs::path& repo);

/// Print diff (working tree vs index). Returns 0 on success, 1/3 on error.
int cmd_diff(const fs::path& repo);

/// Build tree objects from current index. Returns root tree hash.
std::string build_trees_from_index(const fs::path& repo);

/// Create a commit from the current index state. Returns 0 on success.
int cmd_commit(const fs::path& repo, const std::string& message);

}  // namespace bts
#endif  // BTS_INDEX_H
