#include "bts/log.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "bts/object_store.h"
#include "bts/refs.h"

namespace bts {

/// Find the shortest unique prefix for `hash` among `all_hashes`,
/// with a minimum length of 7.
std::string abbreviate_hash(const std::string& hash,
                            const std::vector<std::string>& all_hashes) {
    constexpr size_t min_len = 7;

    for (size_t len = min_len; len <= hash.size(); ++len) {
        std::string prefix = hash.substr(0, len);
        bool unique = true;
        for (const auto& h : all_hashes) {
            if (h != hash && h.substr(0, len) == prefix) {
                unique = false;
                break;
            }
        }
        if (unique) return prefix;
    }
    return hash;  // full hash if never unique (shouldn't happen)
}

namespace {

/// Get the first line of a multi-line string.
std::string first_line(const std::string& s) {
    auto pos = s.find('\n');
    if (pos == std::string::npos) return s;
    return s.substr(0, pos);
}

struct LogEntry {
    std::string hash;
    CommitData  data;
};

}  // anonymous namespace

int cmd_log(const fs::path& repo, bool oneline) {
    // Resolve HEAD to starting commit
    auto head = resolve_head(repo);
    if (!head.has_value()) {
        std::cerr
            << "fatal: your current branch does not have any commits yet"
            << std::endl;
        return 1;
    }

    // Walk the commit DAG and collect all entries
    std::vector<LogEntry> entries;
    std::string current = head.value();

    while (!current.empty()) {
        auto raw = read_object(repo, current);
        if (!raw.has_value()) {
            std::cerr << "fatal: unable to read commit object '" << current
                      << "'" << std::endl;
            return 1;
        }

        LogEntry entry;
        entry.hash = current;
        entry.data = parse_commit(raw.value());
        entries.push_back(std::move(entry));

        // Follow first parent
        if (entries.back().data.parents.empty()) {
            break;
        }
        current = entries.back().data.parents[0];
    }

    // ── Print ────────────────────────────────────────────────────────────
    if (oneline) {
        // Collect all hashes for abbreviation
        std::vector<std::string> all_hashes;
        all_hashes.reserve(entries.size());
        for (const auto& e : entries) {
            all_hashes.push_back(e.hash);
        }

        for (const auto& entry : entries) {
            std::string abbrev = abbreviate_hash(entry.hash, all_hashes);
            std::cout << abbrev << " " << first_line(entry.data.message)
                      << "\n";
        }
    } else {
        // Default format (REQ-LOG-1)
        for (const auto& entry : entries) {
            std::cout << "commit " << entry.hash << "\n";
            std::cout << "Author: " << entry.data.author << "\n";
            std::cout << "Date:   " << entry.data.timestamp << "\n";
            std::cout << "\n";
            // Indent message by 4 spaces (each line)
            std::string msg = entry.data.message;
            std::istringstream iss(msg);
            std::string line;
            while (std::getline(iss, line)) {
                std::cout << "    " << line << "\n";
            }
            std::cout << "\n";
        }
    }

    return 0;
}

}  // namespace bts
