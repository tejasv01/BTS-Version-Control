// TODO(Bharat): replace when merged – minimal interface for Object Storage
// Owner: Bharat (Storage Engine & Object Model)
//
// This header declares the interface that Sarthak's Reference Manager,
// Branch/Checkout, and Log modules depend on. Bharat will replace the stub
// implementations with full SHA-256 + zlib versions.

#ifndef BTS_OBJECT_STORE_H
#define BTS_OBJECT_STORE_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace bts {

namespace fs = std::filesystem;

// ─── Object types ────────────────────────────────────────────────────────────

enum class ObjectType { Blob, Tree, Commit };

inline std::string object_type_string(ObjectType t) {
    switch (t) {
        case ObjectType::Blob:   return "blob";
        case ObjectType::Tree:   return "tree";
        case ObjectType::Commit: return "commit";
    }
    return "unknown";
}

// ─── Tree entry ──────────────────────────────────────────────────────────────

struct TreeEntry {
    uint32_t    mode;     // 100644, 100755, 040000, 120000
    std::string name;
    std::string hash;     // 64-hex SHA-256
};

// ─── Commit data ─────────────────────────────────────────────────────────────

struct CommitData {
    std::string              tree_hash;
    std::vector<std::string> parents;
    std::string              author;      // "Name <email>"
    std::string              timestamp;   // "epoch tz"
    std::string              message;
};

// ─── Repository path helpers ─────────────────────────────────────────────────

/// Search upward from `start` for a directory containing `.bts/`.
/// Returns the repo root path, or nullopt.
std::optional<fs::path> find_repo_root(const fs::path& start);

/// Return `.bts/` path given the repo root.
fs::path bts_dir(const fs::path& repo_root);

// ─── Init command ────────────────────────────────────────────────────────────

/// Initialise a new repository at `path`.  Returns 0 on success, 1 if already
/// initialised.
int cmd_init(const fs::path& path);

// ─── Config helpers ──────────────────────────────────────────────────────────

/// Read a config value. Returns nullopt if key not found.
std::optional<std::string> get_config(const fs::path& repo,
                                      const std::string& key);

/// Set or get config. Returns 0 on success, 1 key not found (get), 2 bad key.
int cmd_config(const fs::path& repo, const std::string& key,
               const std::string& value, bool set_mode);

// ─── Object read/write ──────────────────────────────────────────────────────

/// Hash content deterministically. Returns 64-hex hash string.
std::string hash_content(const std::string& data);

/// Write an object to the store. Returns the 64-hex hash.
std::string write_object(const fs::path& repo, ObjectType type,
                         const std::string& data);

/// Read raw object data given its 64-hex hash. Returns nullopt if not found.
/// Returns just the payload (after header).
std::optional<std::string> read_object(const fs::path& repo,
                                       const std::string& hash);

/// Read full raw object data (header + payload) for integrity checking.
std::optional<std::string> read_object_raw(const fs::path& repo,
                                           const std::string& hash);

/// Parse a commit object's raw payload text into CommitData.
CommitData parse_commit(const std::string& raw);

/// Parse a tree object's raw payload data into a list of TreeEntry.
std::vector<TreeEntry> parse_tree(const std::string& raw);

/// Build a tree blob from entries and write it. Returns 64-hex hash.
std::string write_tree(const fs::path& repo,
                       const std::vector<TreeEntry>& entries);

/// Build a commit object and write it. Returns 64-hex hash.
std::string write_commit(const fs::path& repo, const std::string& tree_hash,
                         const std::vector<std::string>& parents,
                         const std::string& author,
                         const std::string& message);

/// Find all object hashes matching a given prefix (>= 4 chars).
std::vector<std::string> resolve_hash_prefix(const fs::path& repo,
                                             const std::string& prefix);

}  // namespace bts
#endif  // BTS_OBJECT_STORE_H
