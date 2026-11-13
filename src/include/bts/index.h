// TODO(Tejasv): replace when merged – minimal interface for Staging Index
// Owner: Tejasv Lata (Staging Index & Diff Engine)

#ifndef BTS_INDEX_H
#define BTS_INDEX_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bts {

namespace fs = std::filesystem;

struct IndexEntry {
    uint32_t    mode;           // 100644, 100755, 120000
    uint64_t    mtime_sec;
    uint32_t    mtime_nsec;
    uint64_t    file_size;
    std::string sha256;         // 64-hex
    std::string path;           // relative, '/' separators
};

/// Read the index file. Returns entries sorted by path.
std::vector<IndexEntry> read_index(const fs::path& repo);

/// Write the index file atomically (write .lock, rename).
bool write_index(const fs::path& repo, const std::vector<IndexEntry>& entries);

/// Stage a file or directory. Returns 0 on success.
int cmd_add(const fs::path& repo, const std::vector<std::string>& paths);

/// Print status (staged / unstaged / untracked). Returns 0.
int cmd_status(const fs::path& repo);

/// Print diff (working tree vs index). Returns 0.
int cmd_diff(const fs::path& repo);

/// Build tree objects from current index. Returns root tree hash.
std::string build_trees_from_index(const fs::path& repo);

/// Create a commit from the current index state. Returns 0 on success.
int cmd_commit(const fs::path& repo, const std::string& message);

}  // namespace bts
#endif  // BTS_INDEX_H
