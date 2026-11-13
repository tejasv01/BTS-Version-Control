#ifndef BTS_BRANCH_H
#define BTS_BRANCH_H

#include <filesystem>
#include <string>

namespace bts {

namespace fs = std::filesystem;

/// Handle `bts branch` commands.
/// argc/argv style: "branch" (list), "branch <name>" (create),
/// "branch -d <name>" (delete).
/// Returns exit code (0 success, 1 failure).
int cmd_branch(const fs::path& repo, int argc, const char* const* argv);

}  // namespace bts
#endif  // BTS_BRANCH_H
