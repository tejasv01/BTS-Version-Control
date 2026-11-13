#include "bts/refs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace bts {

// ─── Utility ─────────────────────────────────────────────────────────────────

namespace {

/// Trim trailing whitespace and newlines from a string.
std::string trim_right(const std::string& s) {
    auto end = s.find_last_not_of(" \t\r\n");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

/// Read the first line of a file, trimmed.
std::optional<std::string> read_first_line(const fs::path& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return std::nullopt;
    std::string line;
    if (!std::getline(ifs, line)) return std::nullopt;
    return trim_right(line);
}

}  // anonymous namespace

// ─── HEAD operations ─────────────────────────────────────────────────────────

std::string read_head(const fs::path& repo) {
    fs::path head_path = repo / ".bts" / "HEAD";
    auto line = read_first_line(head_path);
    return line.value_or("");
}

bool is_head_detached(const fs::path& repo) {
    std::string head = read_head(repo);
    // HEAD is detached if it does NOT start with "ref: "
    return head.rfind("ref: ", 0) != 0;
}

bool write_head(const fs::path& repo, const std::string& content) {
    fs::path head_path = repo / ".bts" / "HEAD";
    fs::path lock_path = repo / ".bts" / "HEAD.lock";

    // If lock exists, fail
    if (fs::exists(lock_path)) {
        std::cerr << "fatal: Unable to create '.bts/HEAD.lock': File exists."
                  << std::endl;
        return false;
    }

    // Write content + newline to lock file
    {
        std::ofstream ofs(lock_path, std::ios::binary);
        if (!ofs.is_open()) {
            std::cerr << "fatal: Unable to write '.bts/HEAD.lock'" << std::endl;
            return false;
        }
        ofs << content << "\n";
    }

    // Atomic rename
    std::error_code ec;
    fs::rename(lock_path, head_path, ec);
    if (ec) {
        std::cerr << "fatal: Unable to rename HEAD.lock to HEAD: "
                  << ec.message() << std::endl;
        fs::remove(lock_path);
        return false;
    }

    return true;
}

std::optional<std::string> resolve_head(const fs::path& repo) {
    std::string head = read_head(repo);
    if (head.empty()) return std::nullopt;

    const std::string ref_prefix = "ref: ";
    if (head.rfind(ref_prefix, 0) == 0) {
        // Symbolic ref — e.g. "ref: refs/heads/main"
        std::string ref_path = head.substr(ref_prefix.size());
        return read_ref(repo, ref_path);
    }

    // Detached HEAD — hash directly
    return head;
}

std::optional<std::string> current_branch(const fs::path& repo) {
    std::string head = read_head(repo);
    const std::string prefix = "ref: refs/heads/";
    if (head.rfind(prefix, 0) == 0) {
        return head.substr(prefix.size());
    }
    return std::nullopt;  // detached
}

// ─── Ref operations ──────────────────────────────────────────────────────────

std::optional<std::string> read_ref(const fs::path& repo,
                                    const std::string& ref_path) {
    fs::path full_path = repo / ".bts" / ref_path;
    return read_first_line(full_path);
}

bool write_ref_atomic(const fs::path& repo, const std::string& ref_path,
                      const std::string& hash) {
    fs::path target = repo / ".bts" / ref_path;
    fs::path lock   = fs::path(target.string() + ".lock");

    if (fs::exists(lock)) {
        std::cerr << "fatal: Unable to create '" << ref_path
                  << ".lock': File exists." << std::endl;
        return false;
    }

    // Ensure parent directories exist
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        std::cerr << "fatal: Unable to create directory for '" << ref_path
                  << "': " << ec.message() << std::endl;
        return false;
    }

    // Write hash + newline to lock file
    {
        std::ofstream ofs(lock, std::ios::binary);
        if (!ofs.is_open()) {
            std::cerr << "fatal: Unable to write '" << ref_path << ".lock'"
                      << std::endl;
            return false;
        }
        ofs << hash << "\n";
    }

    // Atomic rename
    fs::rename(lock, target, ec);
    if (ec) {
        std::cerr << "fatal: Unable to rename '" << ref_path
                  << ".lock': " << ec.message() << std::endl;
        fs::remove(lock);
        return false;
    }

    return true;
}

bool update_branch_ref(const fs::path& repo, const std::string& branch,
                       const std::string& hash) {
    return write_ref_atomic(repo, "refs/heads/" + branch, hash);
}

bool delete_ref(const fs::path& repo, const std::string& ref_path) {
    fs::path full_path = repo / ".bts" / ref_path;
    if (!fs::exists(full_path)) return false;

    std::error_code ec;
    fs::remove(full_path, ec);
    return !ec;
}

std::vector<std::string> list_branches(const fs::path& repo) {
    std::vector<std::string> branches;
    fs::path heads_dir = repo / ".bts" / "refs" / "heads";

    if (!fs::exists(heads_dir) || !fs::is_directory(heads_dir)) {
        return branches;
    }

    for (const auto& entry : fs::directory_iterator(heads_dir)) {
        if (entry.is_regular_file()) {
            branches.push_back(entry.path().filename().string());
        }
    }

    std::sort(branches.begin(), branches.end());
    return branches;
}

// ─── Branch name validation ─────────────────────────────────────────────────

std::string validate_branch_name(const std::string& name) {
    if (name.empty()) {
        return "Branch name must not be empty";
    }

    // Check for whitespace
    for (char c : name) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            return "Branch name must not contain whitespace";
        }
    }

    // Check for ".."
    if (name.find("..") != std::string::npos) {
        return "Branch name must not contain '..'";
    }

    // Check for forbidden characters: ~ ^ : \.
    for (char c : name) {
        if (c == '~') return "Branch name must not contain '~'";
        if (c == '^') return "Branch name must not contain '^'";
        if (c == ':') return "Branch name must not contain ':'";
        if (c == '\\') return "Branch name must not contain '\\'";
    }

    // Leading characters
    if (name[0] == '-') {
        return "Branch name must not start with '-'";
    }
    if (name[0] == '/') {
        return "Branch name must not start with '/'";
    }

    // Trailing characters
    if (name.back() == '/') {
        return "Branch name must not end with '/'";
    }
    if (name.size() >= 5 &&
        name.substr(name.size() - 5) == ".lock") {
        return "Branch name must not end with '.lock'";
    }

    // Control characters (< 0x20 or 0x7f)
    for (char c : name) {
        auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7f) {
            return "Branch name must not contain control characters";
        }
    }

    return "";  // valid
}

}  // namespace bts
