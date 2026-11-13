#ifndef BTS_REFS_H
#define BTS_REFS_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bts {

namespace fs = std::filesystem;

// ─── HEAD operations ─────────────────────────────────────────────────────────

/// Read the raw content of .bts/HEAD (trimmed).
/// Returns e.g. "ref: refs/heads/main" or a 64-hex commit hash.
std::string read_head(const fs::path& repo);

/// Returns true if HEAD is in detached state (contains a raw hash, not "ref:").
bool is_head_detached(const fs::path& repo);

/// Write HEAD atomically (via .lock + rename).
/// content is either "ref: refs/heads/<branch>" or a 64-hex hash.
/// Returns true on success, false if lock exists.
bool write_head(const fs::path& repo, const std::string& content);

/// Resolve HEAD to a commit hash.
/// If HEAD is "ref: refs/heads/X", reads .bts/refs/heads/X.
/// If HEAD is detached, returns the hash directly.
/// Returns nullopt if the ref doesn't exist yet (unborn branch).
std::optional<std::string> resolve_head(const fs::path& repo);

/// Get the current branch name, or nullopt if HEAD is detached.
std::optional<std::string> current_branch(const fs::path& repo);

// ─── Ref operations ──────────────────────────────────────────────────────────

/// Read a ref file (e.g. .bts/refs/heads/main). Returns the hash, or nullopt.
std::optional<std::string> read_ref(const fs::path& repo,
                                    const std::string& ref_path);

/// Write a ref atomically using .lock + rename.
/// Returns false if lock file already exists.
bool write_ref_atomic(const fs::path& repo, const std::string& ref_path,
                      const std::string& hash);

/// Update ref: convenience for refs/heads/<branch>.
bool update_branch_ref(const fs::path& repo, const std::string& branch,
                       const std::string& hash);

/// Delete a ref file. Returns false if it doesn't exist.
bool delete_ref(const fs::path& repo, const std::string& ref_path);

/// List all branch names (from .bts/refs/heads/).
std::vector<std::string> list_branches(const fs::path& repo);

// ─── Branch name validation ─────────────────────────────────────────────────

/// Validate a branch name per REQ-BRN-4:
/// - Non-empty
/// - No whitespace, no "..", no "~", "^", ":", "\"
/// - No leading "-" or "/"
/// - No trailing "/" or ".lock"
/// - No control characters (< 0x20 or 0x7f)
/// Returns empty string if valid, or an error message.
std::string validate_branch_name(const std::string& name);

}  // namespace bts
#endif  // BTS_REFS_H
