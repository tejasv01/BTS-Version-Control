#include "commit.hpp"
#include "../storage/hash.hpp"
#include "../storage/object_store.hpp"

#include <sstream>

namespace bts {

namespace {

std::string clean_email(std::string email) {
    while (!email.empty() && email.front() == '<') {
        email.erase(0, 1);
    }
    while (!email.empty() && email.back() == '>') {
        email.pop_back();
    }
    return email;
}

std::string trim_spaces(const std::string& str) {
    std::size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    std::size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace

std::string Commit::serialize() const {
    std::string text;
    text.append("tree ");
    text.append(tree_hash);
    text.push_back('\n');

    for (const auto& parent : parent_hashes) {
        text.append("parent ");
        text.append(parent);
        text.push_back('\n');
    }

    std::string email = clean_email(author_email);

    text.append("author ");
    text.append(author_name);
    text.append(" <");
    text.append(email);
    text.append("> ");
    text.append(std::to_string(timestamp));
    text.push_back(' ');
    text.append(timezone.empty() ? "+0000" : timezone);
    text.push_back('\n');

    text.push_back('\n');
    text.append(message);

    return text;
}

Commit Commit::deserialize(const std::string& data) {
    std::size_t blank_pos = data.find("\n\n");
    if (blank_pos == std::string::npos) {
        throw CorruptObjectException("Commit parsing failed: missing blank line separating header and message");
    }

    std::string header_block = data.substr(0, blank_pos);
    std::string message = data.substr(blank_pos + 2);

    std::istringstream stream(header_block);
    std::string line;

    Commit commit;
    commit.message = std::move(message);

    bool has_tree = false;
    bool has_author = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        if (line.rfind("tree ", 0) == 0) {
            commit.tree_hash = trim_spaces(line.substr(5));
            if (!is_valid_sha256_hex(commit.tree_hash)) {
                throw CorruptObjectException("Invalid tree hash in commit header: " + commit.tree_hash);
            }
            has_tree = true;
        } else if (line.rfind("parent ", 0) == 0) {
            std::string parent = trim_spaces(line.substr(7));
            if (!is_valid_sha256_hex(parent)) {
                throw CorruptObjectException("Invalid parent hash in commit header: " + parent);
            }
            commit.parent_hashes.push_back(std::move(parent));
        } else if (line.rfind("author ", 0) == 0) {
            std::string author_line = line.substr(7);

            std::size_t first_angle = author_line.find('<');
            if (first_angle == std::string::npos) {
                throw CorruptObjectException("Commit author header missing email '<': " + line);
            }

            commit.author_name = trim_spaces(author_line.substr(0, first_angle));

            std::size_t last_angle = author_line.find_last_of('>');
            if (last_angle == std::string::npos || last_angle <= first_angle) {
                throw CorruptObjectException("Commit author header missing email '>': " + line);
            }

            std::string raw_email = author_line.substr(first_angle + 1, last_angle - first_angle - 1);
            commit.author_email = clean_email(raw_email);

            std::string time_part = trim_spaces(author_line.substr(last_angle + 1));
            std::size_t space_time = time_part.find(' ');
            if (space_time == std::string::npos) {
                // Issue #3: reject trailing characters in the fallback (no-timezone) epoch field.
                try {
                    std::size_t idx = 0;
                    commit.timestamp = std::stoll(time_part, &idx);
                    if (idx != time_part.size()) {
                        throw std::invalid_argument("trailing characters in epoch");
                    }
                    commit.timezone = "+0000";
                } catch (const std::exception&) {
                    throw CorruptObjectException("Invalid timestamp in commit author header: " + time_part);
                }
            } else {
                std::string epoch_str = time_part.substr(0, space_time);
                std::string tz_str = trim_spaces(time_part.substr(space_time + 1));
                // Issue #4: reject trailing characters in the timezone-bearing epoch field.
                try {
                    std::size_t idx = 0;
                    commit.timestamp = std::stoll(epoch_str, &idx);
                    if (idx != epoch_str.size()) {
                        throw std::invalid_argument("trailing characters in epoch");
                    }
                } catch (const std::exception&) {
                    throw CorruptObjectException("Invalid timestamp in commit author header: " + epoch_str);
                }
                commit.timezone = tz_str;
            }

            has_author = true;
        }
    }

    if (!has_tree) {
        throw CorruptObjectException("Commit object missing required 'tree' header");
    }
    if (!has_author) {
        throw CorruptObjectException("Commit object missing required 'author' header");
    }

    return commit;
}

} // namespace bts
