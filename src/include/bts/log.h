#ifndef BTS_LOG_H
#define BTS_LOG_H

#include <filesystem>

namespace bts {

namespace fs = std::filesystem;

/// Handle `bts log [--oneline]`.
/// Returns exit code (0 success, 1 failure on unborn branch).
int cmd_log(const fs::path& repo, bool oneline);

}  // namespace bts
#endif  // BTS_LOG_H
