#include "bts/checkout.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace bts {

// ─── Helper: recursively flatten a tree into (path, blob_hash) pairs ─────────

static std::vector<std::pair<std::string, std::string>>
collect_tree_files(const fs::path& repo, const std::string& tree_hash,
                   const std::string& prefix) {
    std::vector<std::pair<std::string, std::string>> result;

    auto raw = read_object(repo, tree_hash);
    if (!raw.has_value()) return result;

    auto entries = parse_tree(raw.value());
    for (const auto& entry : entries) {
        std::string full_path =
            prefix.empty() ? entry.name : prefix + "/" + entry.name;

        if (entry.mode == 040000) {
            // Subdirectory — recurse
            auto sub = collect_tree_files(repo, entry.hash, full_path);
            result.insert(result.end(), sub.begin(), sub.end());
        } else {
            result.emplace_back(full_path, entry.hash);
        }
    }

    return result;
}

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
                          << "' is ambiguous" << std::endl;
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

    // Build a map of target: path → blob_hash
    std::map<std::string, std::string> target_map;
    for (const auto& [path, hash] : target_files) {
        target_map[path] = hash;
    }

    // ── Read current index ──────────────────────────────────────────────
    auto current_index = read_index(repo);

    // Build current index map: path → sha256
    std::map<std::string, std::string> index_map;
    for (const auto& ie : current_index) {
        index_map[ie.path] = ie.sha256;
    }

    // ── Safety check (REQ-CHK-2) ────────────────────────────────────────
    // For each file in target tree that differs from current index,
    // check if working tree file has local modifications
    for (const auto& [path, target_hash] : target_map) {
        auto it = index_map.find(path);
        if (it != index_map.end() && it->second == target_hash) {
            continue;  // same content, no conflict
        }

        // File differs or is new — check working tree
        fs::path wt_path = repo / path;
        if (fs::exists(wt_path)) {
            // Read working tree file and hash it
            std::ifstream ifs(wt_path, std::ios::binary);
            if (ifs.is_open()) {
                std::ostringstream ss;
                ss << ifs.rdbuf();
                std::string wt_content = ss.str();
                std::string header = std::string("blob ") + std::to_string(wt_content.size());
                header.push_back('\0');
                std::string wt_hash = hash_content(header + wt_content);

                // If index has this file and working tree differs from index
                if (it != index_map.end() && wt_hash != it->second) {
                    std::cerr
                        << "error: Your local changes would be overwritten "
                           "by checkout.\nPlease commit or discard your "
                           "changes before switching."
                        << std::endl;
                    return 1;
                }
            }
        }
    }

    // Check for untracked files that would be overwritten
    for (const auto& [path, target_hash] : target_map) {
        if (index_map.find(path) != index_map.end()) {
            continue;  // tracked file, already handled
        }
        fs::path wt_path = repo / path;
        if (fs::exists(wt_path)) {
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
                if (fs::is_directory(parent) && fs::is_empty(parent)) {
                    fs::remove(parent, ec);
                    parent = parent.parent_path();
                } else {
                    break;
                }
            }
        }
    }

    // Write / overwrite files from target tree
    for (const auto& [path, blob_hash] : target_files) {
        auto blob_data = read_object(repo, blob_hash);
        if (!blob_data.has_value()) {
            std::cerr << "fatal: unable to read blob '" << blob_hash << "'"
                      << std::endl;
            return 1;
        }

        fs::path wt_path = repo / path;

        // Ensure parent directory exists
        std::error_code ec;
        fs::create_directories(wt_path.parent_path(), ec);

        // Write the file
        std::ofstream ofs(wt_path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "fatal: unable to write '" << path << "'" << std::endl;
            return 1;
        }
        ofs << blob_data.value();
    }

    // ── Update index to match target tree ───────────────────────────────
    std::vector<IndexEntry> new_index;
    for (const auto& [path, blob_hash] : target_files) {
        IndexEntry ie;
        ie.mode = 100644;
        ie.path = path;
        ie.sha256 = blob_hash;

        // Get file stats from working tree
        fs::path wt_path = repo / path;
        if (fs::exists(wt_path)) {
            auto ftime = fs::last_write_time(wt_path);
            auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
                ftime - fs::file_time_type::clock::now() +
                std::chrono::system_clock::now());
            ie.mtime_sec = static_cast<uint64_t>(
                sctp.time_since_epoch().count());
            ie.mtime_nsec = 0;
            ie.file_size = fs::file_size(wt_path);
        } else {
            ie.mtime_sec = 0;
            ie.mtime_nsec = 0;
            ie.file_size = 0;
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
        // Check if we're already on this branch
        std::cout << "Switched to branch '" << target << "'" << std::endl;
    } else {
        write_head(repo, commit_hash);
        std::cerr << "Note: switching to '" << commit_hash << "'.\n"
                  << "You are in 'detached HEAD' state." << std::endl;
    }

    return 0;
}

}  // namespace bts
