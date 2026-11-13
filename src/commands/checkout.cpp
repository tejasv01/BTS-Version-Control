#include "bts/checkout.h"

#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace bts {

namespace {

struct TargetFile {
    std::string path;
    uint32_t    mode;
    std::string hash;
};

// ─── Helper: recursively flatten a tree into TargetFile entries ──────────────

std::vector<TargetFile>
collect_tree_files(const fs::path& repo, const std::string& tree_hash,
                   const std::string& prefix) {
    std::vector<TargetFile> result;

    auto raw = read_object(repo, tree_hash);
    if (!raw.has_value()) return result;

    auto entries = parse_tree(raw.value());
    for (const auto& entry : entries) {
        std::string full_path =
            prefix.empty() ? entry.name : prefix + "/" + entry.name;

        if (entry.mode == 40000 || entry.mode == 040000) {
            // Subdirectory — recurse
            auto sub = collect_tree_files(repo, entry.hash, full_path);
            result.insert(result.end(), sub.begin(), sub.end());
        } else {
            result.push_back(TargetFile{full_path, entry.mode, entry.hash});
        }
    }

    return result;
}

}  // namespace

// ─── cmd_checkout ────────────────────────────────────────────────────────────

int cmd_checkout(const fs::path& repo, const std::string& target) {
    bool is_branch = false;
    std::string commit_hash;

    // ── Resolution order: branch preferred over commit prefix ────────────
    fs::path branch_ref_path = repo / ".bts" / "refs" / "heads" / target;
    if (fs::exists(branch_ref_path) && fs::is_regular_file(branch_ref_path)) {
        // Branch checkout
        is_branch = true;
        auto ref = read_ref(repo, "refs/heads/" + target);
        if (!ref.has_value()) {
            std::cerr << "error: branch '" << target
                      << "' exists but has no commit" << std::endl;
            return 1;
        }
        commit_hash = ref.value();
    } else {
        // Check if it's a commit hash or prefix
        if (target.size() == 64) {
            // Full hash — use directly; validate it exists
            auto obj = read_object(repo, target);
            if (!obj.has_value()) {
                std::cerr << "error: pathspec '" << target
                          << "' did not match any branch or commit"
                          << std::endl;
                return 1;
            }
            commit_hash = target;
        } else if (target.size() >= 4) {
            // Prefix resolution
            auto matches = resolve_hash_prefix(repo, target);
            if (matches.empty()) {
                std::cerr << "error: pathspec '" << target
                          << "' did not match any branch or commit"
                          << std::endl;
                return 1;
            }
            if (matches.size() > 1) {
                std::cerr << "error: short hash '" << target
                          << "' is ambiguous; candidates:" << std::endl;
                for (const auto& m : matches) {
                    std::cerr << "  " << m << std::endl;
                }
                return 1;
            }
            commit_hash = matches[0];
        } else if (target.size() < 4 && !target.empty()) {
            // Check if all chars are hex digits — if so, prefix too short
            bool all_hex = true;
            for (char c : target) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    all_hex = false;
                    break;
                }
            }
            if (all_hex) {
                std::cerr
                    << "fatal: hash prefix must be at least 4 characters"
                    << std::endl;
                return 1;
            }
            std::cerr << "error: pathspec '" << target
                      << "' did not match any branch or commit" << std::endl;
            return 1;
        } else {
            std::cerr << "error: pathspec '" << target
                      << "' did not match any branch or commit" << std::endl;
            return 1;
        }
    }

    // ── Check locks before mutating workspace (RR-3) ────────────────────
    if (fs::exists(bts_dir(repo) / "index.lock") ||
        fs::exists(bts_dir(repo) / "HEAD.lock")) {
        std::cerr << "fatal: lock file exists; aborting checkout" << std::endl;
        return 1;
    }

    // ── Read target commit ──────────────────────────────────────────────
    auto commit_raw = read_object(repo, commit_hash);
    if (!commit_raw.has_value()) {
        std::cerr << "fatal: unable to read commit object '" << commit_hash
                  << "'" << std::endl;
        return 1;
    }
    CommitData commit_data = parse_commit(commit_raw.value());

    // ── Collect target tree files ───────────────────────────────────────
    auto target_files =
        collect_tree_files(repo, commit_data.tree_hash, "");

    // Build a map of target: path → TargetFile
    std::map<std::string, TargetFile> target_map;
    for (const auto& tf : target_files) {
        target_map[tf.path] = tf;
    }

    // ── Read current index ──────────────────────────────────────────────
    auto current_index = read_index(repo);

    // Build current index map: path → IndexEntry
    std::map<std::string, IndexEntry> index_map;
    for (const auto& ie : current_index) {
        index_map[ie.path] = ie;
    }

    // ── Safety check (REQ-CHK-2) ────────────────────────────────────────
    // For each file in target tree that differs from current index,
    // check if working tree file has local modifications
    for (const auto& [path, tf] : target_map) {
        auto it = index_map.find(path);
        if (it != index_map.end() && it->second.sha256 == tf.hash &&
            it->second.mode == tf.mode) {
            continue;  // same content and mode, no conflict
        }

        // File differs or is new — check working tree (without following symlinks)
        fs::path wt_path = repo / path;
        struct stat wt_st {};
        if (::lstat(wt_path.c_str(), &wt_st) == 0) {
            std::string wt_content;
            if (S_ISLNK(wt_st.st_mode)) {
                std::error_code ec;
                wt_content = fs::read_symlink(wt_path, ec).generic_string();
            } else {
                std::ifstream ifs(wt_path, std::ios::binary);
                if (ifs.is_open()) {
                    std::ostringstream ss;
                    ss << ifs.rdbuf();
                    wt_content = ss.str();
                }
            }
            std::string header = std::string("blob ") + std::to_string(wt_content.size());
            header.push_back('\0');
            std::string wt_hash = hash_content(header + wt_content);

            // If index has this file and working tree differs from index
            if (it != index_map.end() && wt_hash != it->second.sha256) {
                std::cerr
                    << "error: Your local changes would be overwritten "
                       "by checkout.\nPlease commit or discard your "
                       "changes before switching."
                    << std::endl;
                return 1;
            }
        }
    }

    // Check for untracked files that would be overwritten
    for (const auto& [path, tf] : target_map) {
        if (index_map.find(path) != index_map.end()) {
            continue;  // tracked file, already handled
        }
        fs::path wt_path = repo / path;
        struct stat wt_st {};
        if (::lstat(wt_path.c_str(), &wt_st) == 0) {
            std::cerr
                << "error: Your local changes would be overwritten "
                   "by checkout.\nPlease commit or discard your "
                   "changes before switching."
                << std::endl;
            return 1;
        }
    }

    // ── Perform checkout ────────────────────────────────────────────────

    // Remove files that are in current index but NOT in target tree
    for (const auto& ie : current_index) {
        if (target_map.find(ie.path) == target_map.end()) {
            fs::path wt_path = repo / ie.path;
            std::error_code ec;
            fs::remove(wt_path, ec);

            // Remove empty parent directories
            fs::path parent = wt_path.parent_path();
            while (parent != repo) {
                if (fs::is_directory(parent, ec) && fs::is_empty(parent, ec)) {
                    fs::remove(parent, ec);
                    parent = parent.parent_path();
                } else {
                    break;
                }
            }
        }
    }

    // Write / overwrite files from target tree
    for (const auto& tf : target_files) {
        auto blob_data = read_object(repo, tf.hash);
        if (!blob_data.has_value()) {
            std::cerr << "fatal: unable to read blob '" << tf.hash << "'"
                      << std::endl;
            return 1;
        }

        fs::path wt_path = repo / tf.path;

        // Ensure parent directory exists
        std::error_code ec;
        fs::create_directories(wt_path.parent_path(), ec);

        if (tf.mode == 120000) {
            fs::remove(wt_path, ec);
            fs::create_symlink(blob_data.value(), wt_path, ec);
            if (ec) {
                std::cerr << "fatal: unable to create symlink '" << tf.path
                          << "': " << ec.message() << std::endl;
                return 1;
            }
        } else {
            // Remove existing symlink if replacing with regular file
            struct stat existing_st {};
            if (::lstat(wt_path.c_str(), &existing_st) == 0 && S_ISLNK(existing_st.st_mode)) {
                fs::remove(wt_path, ec);
            }

            std::ofstream ofs(wt_path, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                std::cerr << "fatal: unable to write '" << tf.path << "'" << std::endl;
                return 1;
            }
            ofs.write(blob_data->data(), static_cast<std::streamsize>(blob_data->size()));
            ofs.close();

            if (tf.mode == 100755) {
                fs::permissions(
                    wt_path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
            } else {
                fs::permissions(
                    wt_path,
                    fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace, ec);
            }
        }
    }

    // ── Update index to match target tree ───────────────────────────────
    std::vector<IndexEntry> new_index;
    for (const auto& tf : target_files) {
        IndexEntry ie{};
        ie.mode = tf.mode;
        ie.path = tf.path;
        ie.sha256 = tf.hash;

        fs::path wt_path = repo / tf.path;
        struct stat st {};
        if (::lstat(wt_path.c_str(), &st) == 0) {
            ie.mtime_sec = static_cast<uint64_t>(st.st_mtime);
#if defined(__APPLE__)
            ie.mtime_nsec = static_cast<uint32_t>(st.st_mtimespec.tv_nsec);
#else
            ie.mtime_nsec = static_cast<uint32_t>(st.st_mtim.tv_nsec);
#endif
            ie.file_size = static_cast<uint64_t>(st.st_size);
        }

        new_index.push_back(std::move(ie));
    }

    // Sort index by path
    std::sort(new_index.begin(), new_index.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.path < b.path;
              });

    write_index(repo, new_index);

    // ── Update HEAD ─────────────────────────────────────────────────────
    if (is_branch) {
        write_head(repo, "ref: refs/heads/" + target);
        std::cout << "Switched to branch '" << target << "'" << std::endl;
    } else {
        write_head(repo, commit_hash);
        std::cerr << "Note: switching to '" << commit_hash << "'.\n"
                  << "You are in 'detached HEAD' state." << std::endl;
    }

    return 0;
}

}  // namespace bts
