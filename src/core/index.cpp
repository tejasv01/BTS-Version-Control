// Binary Staging Index & Porcelain Commands (add, status, commit)
// Owner: Tejasv Lata (Staging Index & Diff Engine)

#include "bts/index.h"
#include "bts/object_store.h"
#include "bts/refs.h"
#include "../storage/hash.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace bts {

namespace {

constexpr char kIndexMagic[4] = {'B', 'T', 'S', 'I'};
constexpr uint32_t kIndexVersion = 1;
constexpr std::size_t kIndexHeaderSize = 12;   // 4 magic + 4 version + 4 count
constexpr std::size_t kIndexFooterSize = 32;   // SHA-256 checksum
constexpr std::size_t kMinEntryFixedSize = 58; // 4+8+4+8+32+2

std::size_t g_status_hash_count = 0;

void append_u16_be(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}

void append_u32_be(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}

void append_u64_be(std::string& out, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((v >> shift) & 0xFF));
    }
}

uint16_t read_u16_be(const std::string& data, std::size_t offset) {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(static_cast<unsigned char>(data[offset])) << 8) |
        static_cast<uint16_t>(static_cast<unsigned char>(data[offset + 1])));
}

uint32_t read_u32_be(const std::string& data, std::size_t offset) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(data[offset])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 3]));
}

uint64_t read_u64_be(const std::string& data, std::size_t offset) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(static_cast<unsigned char>(data[offset + i]));
    }
    return v;
}

bool write_all_fd(int fd, const char* data, std::size_t len) {
    std::size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

struct TreeFileInfo {
    uint32_t    mode;
    std::string hash;
};

void collect_tree_entries(const fs::path& repo,
                          const std::string& tree_hash,
                          const std::string& prefix,
                          std::map<std::string, TreeFileInfo>& out) {
    auto raw = read_object(repo, tree_hash);
    if (!raw) return;

    auto entries = parse_tree(*raw);
    for (const auto& e : entries) {
        std::string full_path = prefix.empty() ? e.name : prefix + "/" + e.name;
        if (e.mode == 40000 || e.mode == 040000) {
            collect_tree_entries(repo, e.hash, full_path, out);
        } else {
            out[full_path] = TreeFileInfo{e.mode, e.hash};
        }
    }
}

struct WorkingStatInfo {
    bool     exists = false;
    bool     is_symlink = false;
    bool     is_regular = false;
    bool     is_directory = false;
    uint32_t mode = 0;
    uint64_t mtime_sec = 0;
    uint32_t mtime_nsec = 0;
    uint64_t file_size = 0;
};

WorkingStatInfo inspect_working_path(const fs::path& fp) {
    WorkingStatInfo info;
    struct stat st {};
    if (::lstat(fp.c_str(), &st) != 0) {
        return info;
    }

    info.exists = true;
    info.is_symlink = S_ISLNK(st.st_mode);
    info.is_regular = S_ISREG(st.st_mode);
    info.is_directory = S_ISDIR(st.st_mode);

    if (info.is_symlink) {
        info.mode = 120000;
    } else if (info.is_regular) {
        info.mode = (st.st_mode & S_IXUSR) ? 100755 : 100644;
    }

    info.mtime_sec = static_cast<uint64_t>(st.st_mtime);
#if defined(__APPLE__)
    info.mtime_nsec = static_cast<uint32_t>(st.st_mtimespec.tv_nsec);
#else
    info.mtime_nsec = static_cast<uint32_t>(st.st_mtim.tv_nsec);
#endif
    info.file_size = static_cast<uint64_t>(st.st_size);
    return info;
}

bool read_file_or_symlink_content(const fs::path& fp,
                                  const WorkingStatInfo& st_info,
                                  std::string& out_content) {
    if (st_info.is_symlink) {
        std::error_code ec;
        fs::path target = fs::read_symlink(fp, ec);
        if (ec) return false;
        out_content = target.generic_string();
        return true;
    }
    if (st_info.is_regular) {
        std::ifstream in(fp, std::ios::binary);
        if (!in.is_open()) return false;
        out_content.assign((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        return true;
    }
    return false;
}

bool is_inside_bts_dir(const std::string& rel_str) {
    return rel_str == ".bts" || rel_str.rfind(".bts/", 0) == 0;
}

bool path_starts_with_dotdot(const std::string& rel_str) {
    return rel_str == ".." || rel_str.rfind("../", 0) == 0;
}

std::string normalize_posix_rel(const fs::path& p) {
    std::string s = p.generic_string();
    while (s.size() >= 2 && s.rfind("./", 0) == 0) {
        s.erase(0, 2);
    }
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

// Resolve a user-supplied path argument into a clean path rooted at `repo`
// without dereferencing a final symlink component.
bool resolve_repo_target(const fs::path& repo,
                         const std::string& raw_arg,
                         fs::path& out_abs_path,
                         std::string& out_rel_str) {
    std::error_code ec;
    fs::path repo_norm = fs::absolute(repo, ec).lexically_normal();
    fs::path repo_canon = fs::weakly_canonical(repo_norm, ec);
    if (ec) repo_canon = repo_norm;

    fs::path cwd = fs::current_path(ec);
    fs::path cwd_canon = ec ? repo_canon : fs::weakly_canonical(cwd, ec);
    if (ec) cwd_canon = repo_canon;

    fs::path base_dir = repo_norm;
    fs::path cwd_rel = cwd_canon.lexically_relative(repo_canon);
    std::string cwd_rel_str = cwd_rel.generic_string();
    if (!cwd_rel_str.empty() && !path_starts_with_dotdot(cwd_rel_str)) {
        base_dir = (cwd_rel_str == ".") ? repo_norm : (repo_norm / cwd_rel);
    }

    fs::path arg_path(raw_arg);
    fs::path candidate = arg_path.is_absolute()
                             ? arg_path.lexically_normal()
                             : (base_dir / arg_path).lexically_normal();

    fs::path rel = candidate.lexically_relative(repo_norm);
    std::string rel_str = normalize_posix_rel(rel);

    // If an absolute path pointed through a symlinked parent (e.g. /private/tmp vs /tmp),
    // try comparing its parent canonical path against repo_canon.
    if (arg_path.is_absolute() && path_starts_with_dotdot(rel_str)) {
        fs::path parent_canon = fs::weakly_canonical(candidate.parent_path(), ec);
        if (!ec) {
            fs::path alt_candidate = (parent_canon / candidate.filename()).lexically_normal();
            fs::path alt_rel = alt_candidate.lexically_relative(repo_canon);
            std::string alt_rel_str = normalize_posix_rel(alt_rel);
            if (!path_starts_with_dotdot(alt_rel_str)) {
                candidate = alt_candidate;
                rel_str = alt_rel_str;
            }
        }
    }

    if (path_starts_with_dotdot(rel_str)) {
        return false;
    }

    out_abs_path = candidate;
    out_rel_str = rel_str;
    return true;
}

bool stage_single_file(const fs::path& repo,
                       const fs::path& abs_path,
                       const std::string& rel_str,
                       const WorkingStatInfo& st_info,
                       std::map<std::string, IndexEntry>& index_map) {
    if (is_inside_bts_dir(rel_str) || rel_str.empty() || rel_str == ".") {
        return true;
    }

    std::string content;
    if (!read_file_or_symlink_content(abs_path, st_info, content)) {
        std::cerr << "error: unable to read '" << rel_str << "'\n";
        return false;
    }

    std::string hash = write_object(repo, ObjectType::Blob, content);

    IndexEntry ie{};
    ie.mode = st_info.mode;
    ie.mtime_sec = st_info.mtime_sec;
    ie.mtime_nsec = st_info.mtime_nsec;
    ie.file_size = static_cast<uint64_t>(content.size());
    ie.sha256 = hash;
    ie.path = rel_str;

    index_map[rel_str] = std::move(ie);
    return true;
}

std::string build_trees_from_entries(const fs::path& repo,
                                     const std::vector<IndexEntry>& entries) {
    struct DirNode {
        std::map<std::string, DirNode> subdirs;
        std::vector<TreeEntry> files;
    };

    DirNode root;
    for (const auto& e : entries) {
        fs::path p(e.path);
        DirNode* current = &root;
        std::vector<std::string> components;
        for (const auto& part : p.parent_path()) {
            std::string s = part.generic_string();
            if (s == "." || s.empty()) continue;
            components.push_back(s);
        }
        for (const auto& comp : components) {
            current = &current->subdirs[comp];
        }
        TreeEntry te;
        te.mode = e.mode;
        te.name = p.filename().generic_string();
        te.hash = e.sha256;
        current->files.push_back(std::move(te));
    }

    std::function<std::string(DirNode&)> write_tree_node;
    write_tree_node = [&](DirNode& node) -> std::string {
        std::vector<TreeEntry> all_entries = node.files;
        for (auto& [name, subdir] : node.subdirs) {
            TreeEntry te;
            te.mode = 40000;
            te.name = name;
            te.hash = write_tree_node(subdir);
            all_entries.push_back(std::move(te));
        }
        return write_tree(repo, all_entries);
    };

    return write_tree_node(root);
}

std::string first_line(const std::string& s) {
    auto pos = s.find('\n');
    if (pos == std::string::npos) return s;
    return s.substr(0, pos);
}

}  // namespace

void reset_status_hash_count() {
    g_status_hash_count = 0;
}

std::size_t get_status_hash_count() {
    return g_status_hash_count;
}

// ─── read_index ──────────────────────────────────────────────────────────────

std::vector<IndexEntry> read_index(const fs::path& repo) {
    std::vector<IndexEntry> entries;
    fs::path idx_path = bts_dir(repo) / "index";

    std::error_code ec;
    if (!fs::exists(idx_path, ec)) {
        return entries;
    }

    std::ifstream in(idx_path, std::ios::binary);
    if (!in.is_open()) {
        throw IndexIntegrityException(
            "fatal: unable to open index file '.bts/index'");
    }

    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (data.empty()) {
        return entries;
    }

    if (data.size() < kIndexHeaderSize + kIndexFooterSize) {
        throw IndexIntegrityException(
            "fatal: index file '.bts/index' is corrupt (truncated header/footer)");
    }

    if (std::memcmp(data.data(), kIndexMagic, sizeof(kIndexMagic)) != 0) {
        throw IndexIntegrityException(
            "fatal: index file '.bts/index' has invalid magic signature");
    }

    uint32_t version = read_u32_be(data, 4);
    if (version != kIndexVersion) {
        throw IndexIntegrityException(
            "fatal: unsupported index version " + std::to_string(version) +
            " in '.bts/index'");
    }

    const std::size_t payload_end = data.size() - kIndexFooterSize;
    std::string expected_checksum = sha256_raw(data.data(), payload_end);
    if (std::memcmp(expected_checksum.data(),
                    data.data() + payload_end,
                    kIndexFooterSize) != 0) {
        throw IndexIntegrityException(
            "fatal: index file '.bts/index' checksum mismatch (corrupted index)");
    }

    uint32_t entry_count = read_u32_be(data, 8);
    entries.reserve(entry_count);

    std::size_t offset = kIndexHeaderSize;
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (offset + kMinEntryFixedSize > payload_end) {
            throw IndexIntegrityException(
                "fatal: index file '.bts/index' is corrupt (truncated entry)");
        }

        IndexEntry e{};
        e.mode = read_u32_be(data, offset);
        e.mtime_sec = read_u64_be(data, offset + 4);
        e.mtime_nsec = read_u32_be(data, offset + 12);
        e.file_size = read_u64_be(data, offset + 16);

        std::string raw_sha = data.substr(offset + 24, 32);
        e.sha256 = raw_to_hex(raw_sha);

        uint16_t path_len = read_u16_be(data, offset + 56);
        offset += kMinEntryFixedSize;

        if (offset + path_len > payload_end) {
            throw IndexIntegrityException(
                "fatal: index file '.bts/index' is corrupt (truncated entry path)");
        }

        e.path = data.substr(offset, path_len);
        offset += path_len;

        entries.push_back(std::move(e));
    }

    if (offset != payload_end) {
        throw IndexIntegrityException(
            "fatal: index file '.bts/index' is corrupt (trailing bytes)");
    }

    std::sort(entries.begin(), entries.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.path < b.path;
              });

    return entries;
}

// ─── write_index ─────────────────────────────────────────────────────────────

bool write_index(const fs::path& repo,
                 const std::vector<IndexEntry>& entries) {
    fs::path idx_path = bts_dir(repo) / "index";
    fs::path lock_path = bts_dir(repo) / "index.lock";

    // Sort a local copy by path in byte order (Appendix B) and enforce POSIX '/'
    std::vector<IndexEntry> sorted = entries;
    for (auto& e : sorted) {
        std::replace(e.path.begin(), e.path.end(), '\\', '/');
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.path < b.path;
              });

    std::string buf;
    buf.reserve(kIndexHeaderSize + sorted.size() * 80 + kIndexFooterSize);
    buf.append(kIndexMagic, sizeof(kIndexMagic));
    append_u32_be(buf, kIndexVersion);
    append_u32_be(buf, static_cast<uint32_t>(sorted.size()));

    try {
        for (const auto& e : sorted) {
            if (e.path.size() > 0xFFFFu) {
                std::cerr << "error: path too long for index entry: " << e.path << "\n";
                return false;
            }
            std::string raw_hash = hex_to_raw(e.sha256);

            append_u32_be(buf, e.mode);
            append_u64_be(buf, e.mtime_sec);
            append_u32_be(buf, e.mtime_nsec);
            append_u64_be(buf, e.file_size);
            buf.append(raw_hash);
            append_u16_be(buf, static_cast<uint16_t>(e.path.size()));
            buf.append(e.path);
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: failed to serialize index: " << ex.what() << "\n";
        return false;
    }

    std::string footer = sha256_raw(buf);
    buf.append(footer);

    // Exclusive lock creation (RR-3)
    int fd = ::open(lock_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        std::cerr << "fatal: Unable to create '.bts/index.lock': "
                  << std::strerror(errno) << "\n";
        return false;
    }

    bool ok = write_all_fd(fd, buf.data(), buf.size());
    if (ok && ::fsync(fd) != 0) {
        ok = false;
    }
    ::close(fd);

    if (!ok) {
        std::error_code rm_ec;
        fs::remove(lock_path, rm_ec);
        std::cerr << "fatal: failed to write '.bts/index.lock'\n";
        return false;
    }

    std::error_code ec;
    fs::rename(lock_path, idx_path, ec);
    if (ec) {
        std::error_code rm_ec;
        fs::remove(lock_path, rm_ec);
        std::cerr << "fatal: failed to rename '.bts/index.lock': "
                  << ec.message() << "\n";
        return false;
    }

    return true;
}

// ─── cmd_add ─────────────────────────────────────────────────────────────────

int cmd_add(const fs::path& repo, const std::vector<std::string>& paths) {
    try {
        if (fs::exists(bts_dir(repo) / "index.lock")) {
            std::cerr << "fatal: Unable to create '.bts/index.lock': File exists.\n";
            return 1;
        }

        auto existing = read_index(repo);
        std::map<std::string, IndexEntry> index_map;
        for (auto& e : existing) {
            index_map[e.path] = std::move(e);
        }

        struct ResolvedArg {
            fs::path abs_path;
            std::string rel_str;
            WorkingStatInfo stat;
        };
        std::vector<ResolvedArg> resolved;
        resolved.reserve(paths.size());

        // Validate every supplied path before staging or modifying anything (REQ-ADD-6).
        for (const auto& p : paths) {
            fs::path abs_path;
            std::string rel_str;
            if (!resolve_repo_target(repo, p, abs_path, rel_str)) {
                std::cerr << "error: pathspec '" << p
                          << "' is outside repository\n";
                return 1;
            }

            if (is_inside_bts_dir(rel_str)) {
                continue;
            }

            WorkingStatInfo st_info = inspect_working_path(abs_path);
            if (!st_info.exists) {
                std::cerr << "error: pathspec '" << p
                          << "' did not match any files\n";
                return 1;
            }

            resolved.push_back(ResolvedArg{abs_path, rel_str, st_info});
        }

        for (const auto& item : resolved) {
            if (item.stat.is_directory) {
                std::error_code ec;
                fs::recursive_directory_iterator it(
                    item.abs_path,
                    fs::directory_options::skip_permission_denied,
                    ec);
                fs::recursive_directory_iterator end;
                for (; !ec && it != end; it.increment(ec)) {
                    const fs::path& entry_path = it->path();
                    if (entry_path.filename() == ".bts") {
                        it.disable_recursion_pending();
                        continue;
                    }

                    WorkingStatInfo child_st = inspect_working_path(entry_path);
                    if (!child_st.exists || child_st.is_directory) {
                        continue;
                    }
                    if (!child_st.is_regular && !child_st.is_symlink) {
                        continue;
                    }

                    std::string child_rel;
                    if (item.rel_str == "." || item.rel_str.empty()) {
                        child_rel = normalize_posix_rel(
                            entry_path.lexically_relative(item.abs_path));
                    } else {
                        std::string sub_rel = normalize_posix_rel(
                            entry_path.lexically_relative(item.abs_path));
                        child_rel = item.rel_str + "/" + sub_rel;
                    }

                    if (!stage_single_file(repo, entry_path, child_rel, child_st, index_map)) {
                        return 1;
                    }
                }
            } else if (item.stat.is_regular || item.stat.is_symlink) {
                if (!stage_single_file(repo, item.abs_path, item.rel_str, item.stat, index_map)) {
                    return 1;
                }
            }
        }

        std::vector<IndexEntry> new_entries;
        new_entries.reserve(index_map.size());
        for (auto& [path, entry] : index_map) {
            new_entries.push_back(std::move(entry));
        }

        if (!write_index(repo, new_entries)) {
            return 1;
        }
        return 0;
    } catch (const IndexIntegrityException& e) {
        std::cerr << e.what() << "\n";
        return 3;
    } catch (const IntegrityException& e) {
        std::cerr << e.what() << "\n";
        return 3;
    }
}

// ─── cmd_status ──────────────────────────────────────────────────────────────

int cmd_status(const fs::path& repo) {
    try {
        auto index = read_index(repo);

        auto branch = current_branch(repo);
        if (branch.has_value()) {
            std::cout << "On branch " << *branch << "\n";
        } else {
            std::cout << "HEAD detached at " << read_head(repo).substr(0, 7) << "\n";
        }

        // Build map of HEAD tree entries
        std::map<std::string, TreeFileInfo> head_files;
        auto head_commit = resolve_head(repo);
        if (!head_commit.has_value()) {
            std::cout << "no commits yet\n\n";
        } else {
            auto raw = read_object(repo, *head_commit);
            if (raw.has_value()) {
                auto cd = parse_commit(*raw);
                collect_tree_entries(repo, cd.tree_hash, "", head_files);
            }
        }

        // 1. Staged changes (index vs HEAD)
        std::vector<std::string> staged_new, staged_mod, staged_del;
        std::set<std::string> index_paths;
        for (const auto& e : index) {
            index_paths.insert(e.path);
            auto it = head_files.find(e.path);
            if (it == head_files.end()) {
                staged_new.push_back(e.path);
            } else if (it->second.hash != e.sha256 || it->second.mode != e.mode) {
                staged_mod.push_back(e.path);
            }
        }
        for (const auto& [path, info] : head_files) {
            if (index_paths.find(path) == index_paths.end()) {
                staged_del.push_back(path);
            }
        }

        if (!staged_new.empty() || !staged_mod.empty() || !staged_del.empty()) {
            std::cout << "Changes to be committed:\n";
            for (const auto& f : staged_new) std::cout << "  new file:   " << f << "\n";
            for (const auto& f : staged_mod) std::cout << "  modified:   " << f << "\n";
            for (const auto& f : staged_del) std::cout << "  deleted:    " << f << "\n";
            std::cout << "\n";
        }

        // 2. Unstaged changes (working tree vs index) with stat-cache shortcut (REQ-STAT-2)
        std::vector<std::string> unstaged_mod, unstaged_del;
        for (const auto& e : index) {
            fs::path fp = repo / e.path;
            WorkingStatInfo wt_st = inspect_working_path(fp);
            if (!wt_st.exists || wt_st.is_directory) {
                unstaged_del.push_back(e.path);
                continue;
            }
            if (!wt_st.is_regular && !wt_st.is_symlink) {
                unstaged_mod.push_back(e.path);
                continue;
            }

            // Stat-cache hit: matching mtime, size, and mode -> unchanged without hashing.
            if (wt_st.mode == e.mode &&
                wt_st.mtime_sec == e.mtime_sec &&
                wt_st.mtime_nsec == e.mtime_nsec &&
                wt_st.file_size == e.file_size) {
                continue;
            }

            if (wt_st.mode != e.mode || wt_st.file_size != e.file_size) {
                unstaged_mod.push_back(e.path);
                continue;
            }

            std::string content;
            if (!read_file_or_symlink_content(fp, wt_st, content)) {
                unstaged_mod.push_back(e.path);
                continue;
            }

            ++g_status_hash_count;
            std::string header = "blob " + std::to_string(content.size());
            std::string full;
            full.reserve(header.size() + 1 + content.size());
            full.append(header);
            full.push_back('\0');
            full.append(content);
            std::string wt_hash = hash_content(full);

            if (wt_hash != e.sha256) {
                unstaged_mod.push_back(e.path);
            }
        }

        if (!unstaged_mod.empty() || !unstaged_del.empty()) {
            std::cout << "Changes not staged for commit:\n";
            for (const auto& f : unstaged_mod) std::cout << "  modified:   " << f << "\n";
            for (const auto& f : unstaged_del) std::cout << "  deleted:    " << f << "\n";
            std::cout << "\n";
        }

        // 3. Untracked files
        std::vector<std::string> untracked;
        std::error_code ec;
        fs::path repo_norm = fs::absolute(repo, ec).lexically_normal();
        fs::recursive_directory_iterator it(
            repo_norm,
            fs::directory_options::skip_permission_denied,
            ec);
        fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path& entry_path = it->path();
            if (entry_path.filename() == ".bts") {
                it.disable_recursion_pending();
                continue;
            }
            WorkingStatInfo child_st = inspect_working_path(entry_path);
            if (!child_st.exists || child_st.is_directory) continue;
            if (!child_st.is_regular && !child_st.is_symlink) continue;

            std::string rel_str = normalize_posix_rel(
                entry_path.lexically_relative(repo_norm));
            if (is_inside_bts_dir(rel_str) || rel_str.empty() || rel_str == ".") {
                continue;
            }
            if (index_paths.find(rel_str) == index_paths.end()) {
                untracked.push_back(rel_str);
            }
        }

        if (!untracked.empty()) {
            std::sort(untracked.begin(), untracked.end());
            std::cout << "Untracked files:\n";
            for (const auto& f : untracked) std::cout << "  " << f << "\n";
            std::cout << "\n";
        }

        if (staged_new.empty() && staged_mod.empty() && staged_del.empty() &&
            unstaged_mod.empty() && unstaged_del.empty() && untracked.empty()) {
            std::cout << "nothing to commit, working tree clean\n";
        }

        return 0;
    } catch (const IndexIntegrityException& e) {
        std::cerr << e.what() << "\n";
        return 3;
    } catch (const IntegrityException& e) {
        std::cerr << e.what() << "\n";
        return 3;
    }
}

// ─── build_trees_from_index ──────────────────────────────────────────────────

std::string build_trees_from_index(const fs::path& repo) {
    auto entries = read_index(repo);
    return build_trees_from_entries(repo, entries);
}

// ─── cmd_commit ──────────────────────────────────────────────────────────────

int cmd_commit(const fs::path& repo, const std::string& message) {
    try {
        if (message.empty()) {
            std::cerr << "error: empty commit message\n";
            return 1;
        }

        // Verify author configuration BEFORE creating any objects (REQ-CFG-2)
        auto name = get_config(repo, "user.name");
        auto email = get_config(repo, "user.email");
        if (!name || name->empty()) {
            std::cerr << "error: user.name is not configured. Run 'bts config user.name \"Your Name\"'\n";
            return 1;
        }
        if (!email || email->empty()) {
            std::cerr << "error: user.email is not configured. Run 'bts config user.email \"you@example.com\"'\n";
            return 1;
        }

        // Check lock files before writing any objects (RR-3)
        if (fs::exists(bts_dir(repo) / "index.lock")) {
            std::cerr << "fatal: Unable to create '.bts/index.lock': File exists.\n";
            return 1;
        }
        std::string head_content = read_head(repo);
        bool is_symbolic = (head_content.rfind("ref: ", 0) == 0);
        if (is_symbolic) {
            std::string ref_target = head_content.substr(5);
            if (fs::exists(bts_dir(repo) / (ref_target + ".lock"))) {
                std::cerr << "fatal: Unable to create '" << ref_target << ".lock': File exists.\n";
                return 1;
            }
        } else if (fs::exists(bts_dir(repo) / "HEAD.lock")) {
            std::cerr << "fatal: Unable to create '.bts/HEAD.lock': File exists.\n";
            return 1;
        }

        auto index = read_index(repo);
        auto head_commit = resolve_head(repo);

        // Reject empty commit on unborn branch (REQ-CMT-1)
        if (!head_commit.has_value() && index.empty()) {
            std::cerr << "error: nothing to commit (empty index)\n";
            return 1;
        }

        // Check if index matches HEAD tree before writing objects (REQ-CMT-1)
        if (head_commit.has_value()) {
            auto raw = read_object(repo, *head_commit);
            if (raw.has_value()) {
                auto cd = parse_commit(*raw);
                std::map<std::string, TreeFileInfo> head_files;
                collect_tree_entries(repo, cd.tree_hash, "", head_files);
                if (head_files.size() == index.size()) {
                    bool all_match = true;
                    for (const auto& e : index) {
                        auto it = head_files.find(e.path);
                        if (it == head_files.end() ||
                            it->second.hash != e.sha256 ||
                            it->second.mode != e.mode) {
                            all_match = false;
                            break;
                        }
                    }
                    if (all_match) {
                        std::cerr << "error: nothing to commit (no changes)\n";
                        return 1;
                    }
                }
            }
        }

        std::string author = *name + " <" + *email + ">";
        std::string tree_hash = build_trees_from_entries(repo, index);

        std::vector<std::string> parents;
        if (head_commit.has_value()) {
            parents.push_back(*head_commit);
        }

        std::string commit_hash = write_commit(repo, tree_hash, parents, author, message);

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
            std::cerr << "warning: HEAD is detached; commit "
                      << commit_hash.substr(0, 7)
                      << " is not on any branch\n";
        }

        std::cout << "[" << branch_name
                  << " " << commit_hash.substr(0, 7) << "] "
                  << first_line(message) << "\n";

        return 0;
    } catch (const IndexIntegrityException& e) {
        std::cerr << e.what() << "\n";
        return 3;
    } catch (const IntegrityException& e) {
        std::cerr << e.what() << "\n";
        return 3;
    }
}

}  // namespace bts

