#ifndef BTS_CHECKOUT_H
#define BTS_CHECKOUT_H

#include <filesystem>
#include <string>

namespace bts {

namespace fs = std::filesystem;

/// Handle `bts checkout <target>`.
/// target is a branch name or commit hash (full or prefix >= 4 chars).
/// Returns exit code (0 success, 1 failure).
int cmd_checkout(const fs::path& repo, const std::string& target);

}  // namespace bts
#endif  // BTS_CHECKOUT_H
