// TODO(Tejasv): replace when merged – stub implementation for Staging Index
// Owner: Tejasv Lata (Staging Index & Diff Engine)
//
// Minimal stub so Sarthak's checkout/branch/log modules compile and link.

#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace bts {

// ─── read_index ──────────────────────────────────────────────────────────────

std::vector<IndexEntry> read_index(const fs::path& repo) {
    std::vector<IndexEntry> entries;
    fs::path idx_path = bts_dir(repo) / "index";

    if (!fs::exists(idx_path)) return entries;

    std::ifstream in(idx_path);
    if (!in.is_open()) return entries;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        IndexEntry e{};
        iss >> e.mode >> e.mtime_sec >> e.mtime_nsec
            >> e.file_size >> e.sha256 >> e.path;

        if (!e.path.empty()) {
            entries.push_back(std::move(e));
        }
    }

    // Sort by path
    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.path < b.path;
              });

    return entries;
}

// ─── write_index ─────────────────────────────────────────────────────────────

bool write_index(const fs::path& repo,
                 const std::vector<IndexEntry>& entries) {
    fs::path idx_path  = bts_dir(repo) / "index";
    fs::path lock_path = bts_dir(repo) / "index.lock";

    // Atomic: write to .lock, then rename
    {
        std::ofstream out(lock_path, std::ios::trunc);
        if (!out.is_open()) return false;

        for (auto& e : entries) {
            out << e.mode << " "
                << e.mtime_sec << " "
                << e.mtime_nsec << " "
                << e.file_size << " "
                << e.sha256 << " "
                << e.path << "\n";
        }
    }

    std::error_code ec;
    fs::rename(lock_path, idx_path, ec);
    return !ec;
}

// ─── Helper: collect tree entries recursively ────────────────────────────────

static void collect_tree_entries(const fs::path& repo,
                                 const std::string& tree_hash,
                                 const std::string& prefix,
                                 std::map<std::string, std::string>& out) {
    auto raw = read_object(repo, tree_hash);
    if (!raw) return;

    auto entries = parse_tree(*raw);
    for (auto& e : entries) {
        std::string full_path = prefix.empty() ? e.name : prefix + "/" + e.name;
        if (e.mode == 40000) {
            collect_tree_entries(repo, e.hash, full_path, out);
        } else {
            out[full_path] = e.hash;
        }
    }
}

// ─── cmd_add ─────────────────────────────────────────────────────────────────

int cmd_add(const fs::path& repo, const std::vector<std::string>& paths) {
    auto entries = read_index(repo);

    for (auto& p : paths) {
        fs::path abs_path = fs::absolute(repo / p);

        // Skip .bts directory
        {
            fs::path bts = bts_dir(repo);
            auto rel = fs::relative(abs_path, bts);
            std::string rel_str = rel.string();
            if (rel_str.find("..") != 0) {
                // path is inside .bts, skip
                continue;
            }
        }

        if (!fs::exists(abs_path)) {
            std::cerr << "error: pathspec '" << p
                      << "' did not match any files\n";
            return 1;
        }

        if (fs::is_directory(abs_path)) {
            // Recursively add files
            for (auto& de : fs::recursive_directory_iterator(abs_path)) {
                if (!de.is_regular_file()) continue;

                fs::path rel = fs::relative(de.path(), repo);
                std::string rel_str = rel.generic_string();

                // Skip .bts/
                if (rel_str.rfind(".bts/", 0) == 0 || rel_str == ".bts")
                    continue;

                // Read file content
                std::ifstream in(de.path(), std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());

                std::string hash = write_object(repo, ObjectType::Blob, content);

                IndexEntry ie{};
                ie.mode      = 100644;
                ie.mtime_sec = static_cast<uint64_t>(std::time(nullptr));
                ie.mtime_nsec = 0;
                ie.file_size = static_cast<uint64_t>(content.size());
                ie.sha256    = hash;
                ie.path      = rel_str;

                // Update or add entry
                auto it = std::find_if(entries.begin(), entries.end(),
                    [&](const IndexEntry& x) { return x.path == rel_str; });
                if (it != entries.end()) {
                    *it = ie;
                } else {
                    entries.push_back(ie);
                }
            }
        } else {
            fs::path rel = fs::relative(abs_path, repo);
            std::string rel_str = rel.generic_string();

            // Read file content
            std::ifstream in(abs_path, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());

            std::string hash = write_object(repo, ObjectType::Blob, content);

            IndexEntry ie{};
            ie.mode      = 100644;
            ie.mtime_sec = static_cast<uint64_t>(std::time(nullptr));
            ie.mtime_nsec = 0;
            ie.file_size = static_cast<uint64_t>(content.size());
            ie.sha256    = hash;
            ie.path      = rel_str;

            auto it = std::find_if(entries.begin(), entries.end(),
                [&](const IndexEntry& x) { return x.path == rel_str; });
            if (it != entries.end()) {
                *it = ie;
            } else {
                entries.push_back(ie);
            }
        }
    }

    // Sort and write
    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.path < b.path;
              });

    write_index(repo, entries);
    return 0;
}

// ─── cmd_status ──────────────────────────────────────────────────────────────

int cmd_status(const fs::path& repo) {
    auto head_info = read_head(repo);
    auto index = read_index(repo);

    // Build set of HEAD tree entries
    std::map<std::string, std::string> head_files;  // path -> hash
    auto head_commit = resolve_head(repo);
    if (head_commit) {
        auto raw = read_object(repo, *head_commit);
        if (raw) {
            auto cd = parse_commit(*raw);
            collect_tree_entries(repo, cd.tree_hash, "", head_files);
        }
    }

    // Staged changes (index vs HEAD)
    std::vector<std::string> staged_new, staged_mod, staged_del;
    std::set<std::string> index_paths;
    for (auto& e : index) {
        index_paths.insert(e.path);
        auto it = head_files.find(e.path);
        if (it == head_files.end()) {
            staged_new.push_back(e.path);
        } else if (it->second != e.sha256) {
            staged_mod.push_back(e.path);
        }
    }
    for (auto& [path, hash] : head_files) {
        if (index_paths.find(path) == index_paths.end()) {
            staged_del.push_back(path);
        }
    }

    // Print staged
    if (!staged_new.empty() || !staged_mod.empty() || !staged_del.empty()) {
        std::cout << "Changes to be committed:\n";
        for (auto& f : staged_new) std::cout << "  new file:   " << f << "\n";
        for (auto& f : staged_mod) std::cout << "  modified:   " << f << "\n";
        for (auto& f : staged_del) std::cout << "  deleted:    " << f << "\n";
        std::cout << "\n";
    }

    // Unstaged (working tree vs index)
    std::vector<std::string> unstaged_mod, unstaged_del;
    for (auto& e : index) {
        fs::path fp = repo / e.path;
        if (!fs::exists(fp)) {
            unstaged_del.push_back(e.path);
        } else if (fs::is_regular_file(fp)) {
            std::ifstream in(fp, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
            std::string h = hash_content(
                "blob " + std::to_string(content.size()) + '\0' + content);
            // Compare stored object hash approach: re-hash via write_object
            // is wasteful, so just compare file size as simple heuristic
            if (static_cast<uint64_t>(content.size()) != e.file_size) {
                unstaged_mod.push_back(e.path);
            }
        }
    }

    if (!unstaged_mod.empty() || !unstaged_del.empty()) {
        std::cout << "Changes not staged for commit:\n";
        for (auto& f : unstaged_mod) std::cout << "  modified:   " << f << "\n";
        for (auto& f : unstaged_del) std::cout << "  deleted:    " << f << "\n";
        std::cout << "\n";
    }

    // Untracked files
    std::vector<std::string> untracked;
    for (auto& de : fs::recursive_directory_iterator(repo)) {
        if (!de.is_regular_file()) continue;
        fs::path rel = fs::relative(de.path(), repo);
        std::string rel_str = rel.generic_string();
        if (rel_str.rfind(".bts/", 0) == 0 || rel_str == ".bts") continue;
        if (index_paths.find(rel_str) == index_paths.end()) {
            untracked.push_back(rel_str);
        }
    }

    if (!untracked.empty()) {
        std::sort(untracked.begin(), untracked.end());
        std::cout << "Untracked files:\n";
        for (auto& f : untracked) std::cout << "  " << f << "\n";
        std::cout << "\n";
    }

    return 0;
}

// ─── cmd_diff ────────────────────────────────────────────────────────────────

int cmd_diff(const fs::path& /*repo*/) {
    std::cout << "diff stub - TODO(Tejasv)\n";
    return 0;
}

// ─── build_trees_from_index ──────────────────────────────────────────────────

std::string build_trees_from_index(const fs::path& repo) {
    auto entries = read_index(repo);

    // Build a tree structure: map directory path -> vector of TreeEntry
    // For simplicity, build nested trees bottom-up
    struct DirNode {
        std::map<std::string, DirNode> subdirs;
        std::vector<TreeEntry> files;
    };

    DirNode root;

    for (auto& e : entries) {
        // Split path into components
        fs::path p(e.path);
        DirNode* current = &root;
        std::vector<std::string> components;
        for (const auto& part : p.parent_path()) {
            std::string s = part.string();
            if (s == "." || s.empty()) continue;
            components.push_back(s);
        }
        for (auto& comp : components) {
            current = &current->subdirs[comp];
        }
        TreeEntry te;
        te.mode = e.mode;
        te.name = p.filename().string();
        te.hash = e.sha256;
        current->files.push_back(te);
    }

    // Recursively write tree objects
    std::function<std::string(DirNode&)> write_tree_node;
    write_tree_node = [&](DirNode& node) -> std::string {
        std::vector<TreeEntry> all_entries;

        // Add file entries
        for (auto& f : node.files) {
            all_entries.push_back(f);
        }

        // Process subdirectories
        for (auto& [name, subdir] : node.subdirs) {
            std::string sub_hash = write_tree_node(subdir);
            TreeEntry te;
            te.mode = 40000;
            te.name = name;
            te.hash = sub_hash;
            all_entries.push_back(te);
        }

        return write_tree(repo, all_entries);
    };

    return write_tree_node(root);
}

// ─── cmd_commit ──────────────────────────────────────────────────────────────

int cmd_commit(const fs::path& repo, const std::string& message) {
    if (message.empty()) {
        std::cerr << "error: empty commit message\n";
        return 1;
    }

    // Check config for user
    auto name = get_config(repo, "user.name");
    auto email = get_config(repo, "user.email");
    if (!name || !email) {
        std::cerr << "error: user.name and user.email must be configured\n";
        return 1;
    }

    std::string author = *name + " <" + *email + ">";

    auto index = read_index(repo);
    if (index.empty()) {
        std::cerr << "error: nothing to commit (empty index)\n";
        return 1;
    }

    // Build tree
    std::string tree_hash = build_trees_from_index(repo);

    // Get current HEAD commit (parent)
    std::vector<std::string> parents;
    auto head_commit = resolve_head(repo);
    if (head_commit) {
        // Check if tree is the same (empty commit)
        auto raw = read_object(repo, *head_commit);
        if (raw) {
            auto cd = parse_commit(*raw);
            if (cd.tree_hash == tree_hash) {
                std::cerr << "error: nothing to commit (no changes)\n";
                return 1;
            }
        }
        parents.push_back(*head_commit);
    }

    // Create commit
    std::string commit_hash = write_commit(repo, tree_hash, parents, author,
                                           message);

    // Update HEAD
    std::string head_content = read_head(repo);
    bool is_symbolic = (head_content.rfind("ref: ", 0) == 0);
    std::string branch_name;
    if (is_symbolic) {
        std::string ref_target = head_content.substr(5);
        if (!write_ref_atomic(repo, ref_target, commit_hash)) {
            std::cerr << "error: failed to update ref " << ref_target << "\n";
            return 1;
        }
        auto slash = ref_target.rfind('/');
        branch_name = (slash != std::string::npos) ? ref_target.substr(slash + 1) : ref_target;
    } else {
        if (!write_head(repo, commit_hash)) {
            std::cerr << "error: failed to update HEAD\n";
            return 1;
        }
        branch_name = "detached";
    }

    std::cout << "[" << branch_name
              << " " << commit_hash.substr(0, 7) << "] "
              << message << "\n";

    return 0;
}

}  // namespace bts
