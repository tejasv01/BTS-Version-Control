#ifndef BTS_LOG_H
#define BTS_LOG_H

#include <filesystem>
#include <string>
#include <vector>

namespace bts {

namespace fs = std::filesystem;

/// Find the shortest unique prefix (min length 7) for `hash` among `all_hashes`.
std::string abbreviate_hash(const std::string& hash,
                            const std::vector<std::string>& all_hashes);

/// Handle `bts log [--oneline]`.
/// Returns exit code (0 success, 1 failure on unborn branch).
int cmd_log(const fs::path& repo, bool oneline);

}  // namespace bts
#endif  // BTS_LOG_H
