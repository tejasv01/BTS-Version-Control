#include "bts/diff.h"
#include "bts/index.h"
#include "bts/object_store.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace bts {

bool is_binary_content(const std::string& content) {
    return content.find('\0') != std::string::npos;
}

std::vector<std::string> split_lines(const std::string& content) {
    std::vector<std::string> lines;
    if (content.empty()) {
        return lines;
    }

    std::size_t start = 0;
    while (start < content.size()) {
        std::size_t nl = content.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(content.substr(start));
            break;
        }
        lines.push_back(content.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

std::vector<DiffEdit> compute_myers_diff(const std::vector<std::string>& old_lines,
                                         const std::vector<std::string>& new_lines) {
    const int n = static_cast<int>(old_lines.size());
    const int m = static_cast<int>(new_lines.size());

    if (n == 0 && m == 0) {
        return {};
    }

    const int max_d = n + m;
    const int offset = max_d;
    std::vector<int> v(2 * max_d + 1, 0);
    std::vector<std::vector<int>> trace;
    trace.reserve(static_cast<std::size_t>(max_d) + 1);

    int found_d = -1;
    for (int d = 0; d <= max_d; ++d) {
        trace.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int x = 0;
            if (k == -d || (k != d && v[k - 1 + offset] < v[k + 1 + offset])) {
                x = v[k + 1 + offset];
            } else {
                x = v[k - 1 + offset] + 1;
            }
            int y = x - k;

            while (x < n && y < m && old_lines[x] == new_lines[y]) {
                ++x;
                ++y;
            }

            v[k + offset] = x;

            if (x >= n && y >= m) {
                found_d = d;
                break;
            }
        }
        if (found_d >= 0) {
            break;
        }
    }

    // Backtrack from (n, m) down to (0, 0) using trace.
    std::vector<DiffEdit> reversed;
    int x = n;
    int y = m;

    for (int d = found_d; d >= 0; --d) {
        const std::vector<int>& prev_v = trace[static_cast<std::size_t>(d)];
        const int k = x - y;

        const bool is_insert =
            (k == -d || (k != d && prev_v[k - 1 + offset] < prev_v[k + 1 + offset]));
        const int prev_k = is_insert ? (k + 1) : (k - 1);
        const int prev_x = prev_v[prev_k + offset];
        const int prev_y = prev_x - prev_k;

        while (x > prev_x && y > prev_y) {
            reversed.push_back(DiffEdit{
                DiffOp::Equal,
                old_lines[static_cast<std::size_t>(x - 1)],
                static_cast<std::size_t>(x),
                static_cast<std::size_t>(y)
            });
            --x;
            --y;
        }

        if (d > 0) {
            if (is_insert) {
                reversed.push_back(DiffEdit{
                    DiffOp::Insert,
                    new_lines[static_cast<std::size_t>(y - 1)],
                    0,
                    static_cast<std::size_t>(y)
                });
                --y;
            } else {
                reversed.push_back(DiffEdit{
                    DiffOp::Delete,
                    old_lines[static_cast<std::size_t>(x - 1)],
                    static_cast<std::size_t>(x),
                    0
                });
                --x;
            }
        }
    }

    std::reverse(reversed.begin(), reversed.end());

    // Normalize contiguous change blocks so Delete entries always precede Insert entries.
    std::size_t i = 0;
    while (i < reversed.size()) {
        if (reversed[i].op == DiffOp::Equal) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < reversed.size() && reversed[j].op != DiffOp::Equal) {
            ++j;
        }
        std::stable_partition(reversed.begin() + static_cast<std::ptrdiff_t>(i),
                              reversed.begin() + static_cast<std::ptrdiff_t>(j),
                              [](const DiffEdit& e) { return e.op == DiffOp::Delete; });
        i = j;
    }

    return reversed;
}

std::string format_unified_diff(const std::string& path,
                                const std::string& old_content,
                                const std::string& new_content,
                                std::size_t context_lines) {
    if (old_content == new_content) {
        return "";
    }

    if (is_binary_content(old_content) || is_binary_content(new_content)) {
        std::ostringstream oss;
        oss << "--- a/" << path << "\n"
            << "+++ b/" << path << "\n"
            << "Binary files differ\n";
        return oss.str();
    }

    auto old_lines = split_lines(old_content);
    auto new_lines = split_lines(new_content);
    auto edits = compute_myers_diff(old_lines, new_lines);

    struct Hunk {
        std::size_t start_idx;
        std::size_t end_idx;
    };

    std::vector<Hunk> hunks;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        if (edits[i].op == DiffOp::Equal) {
            continue;
        }
        std::size_t hs = (i > context_lines) ? (i - context_lines) : 0;
        std::size_t he = std::min(edits.size(), i + 1 + context_lines);
        if (!hunks.empty() && hs <= hunks.back().end_idx) {
            hunks.back().end_idx = he;
        } else {
            hunks.push_back(Hunk{hs, he});
        }
    }

    if (hunks.empty()) {
        return "";
    }

    // Track 0-based counts of old and new lines before each edit index.
    std::vector<std::size_t> old_prefix(edits.size() + 1, 0);
    std::vector<std::size_t> new_prefix(edits.size() + 1, 0);
    for (std::size_t i = 0; i < edits.size(); ++i) {
        old_prefix[i + 1] = old_prefix[i] + (edits[i].op != DiffOp::Insert ? 1 : 0);
        new_prefix[i + 1] = new_prefix[i] + (edits[i].op != DiffOp::Delete ? 1 : 0);
    }

    std::ostringstream out;
    out << "--- a/" << path << "\n"
        << "+++ b/" << path << "\n";

    for (const auto& hunk : hunks) {
        const std::size_t old_before = old_prefix[hunk.start_idx];
        const std::size_t new_before = new_prefix[hunk.start_idx];
        const std::size_t old_len = old_prefix[hunk.end_idx] - old_before;
        const std::size_t new_len = new_prefix[hunk.end_idx] - new_before;

        const std::size_t old_start = (old_len == 0) ? old_before : (old_before + 1);
        const std::size_t new_start = (new_len == 0) ? new_before : (new_before + 1);

        out << "@@ -" << old_start << "," << old_len
            << " +" << new_start << "," << new_len << " @@\n";

        for (std::size_t k = hunk.start_idx; k < hunk.end_idx; ++k) {
            const auto& ed = edits[k];
            switch (ed.op) {
                case DiffOp::Equal:
                    out << ' ' << ed.line << '\n';
                    break;
                case DiffOp::Delete:
                    out << '-' << ed.line << '\n';
                    break;
                case DiffOp::Insert:
                    out << '+' << ed.line << '\n';
                    break;
            }
        }
    }

    return out.str();
}

namespace {

bool read_working_tree_content(const fs::path& fp, std::string& out_content, bool& out_exists) {
    struct stat st {};
    if (::lstat(fp.c_str(), &st) != 0) {
        if (errno == ENOENT) {
            out_exists = false;
            out_content.clear();
            return true;
        }
        return false;
    }

    out_exists = true;
    if (S_ISLNK(st.st_mode)) {
        std::error_code ec;
        fs::path target = fs::read_symlink(fp, ec);
        if (ec) return false;
        out_content = target.generic_string();
        return true;
    }

    if (S_ISREG(st.st_mode)) {
        std::ifstream in(fp, std::ios::binary);
        if (!in.is_open()) return false;
        out_content.assign((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        return true;
    }

    // Directories or special files cannot be diffed as regular content
    out_exists = false;
    out_content.clear();
    return true;
}

}  // namespace

int cmd_diff(const fs::path& repo) {
    try {
        auto entries = read_index(repo);

        for (const auto& entry : entries) {
            auto staged_opt = read_object(repo, entry.sha256);
            if (!staged_opt.has_value()) {
                std::cerr << "fatal: missing staged object '" << entry.sha256
                          << "' for '" << entry.path << "'\n";
                return 1;
            }

            std::string wt_content;
            bool wt_exists = false;
            fs::path fp = repo / entry.path;
            if (!read_working_tree_content(fp, wt_content, wt_exists)) {
                std::cerr << "error: failed to read working tree file '" << entry.path << "'\n";
                return 1;
            }

            if (!wt_exists) {
                wt_content.clear();
            }

            if (wt_content != *staged_opt) {
                std::cout << format_unified_diff(entry.path, *staged_opt, wt_content);
            }
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

}  // namespace bts

