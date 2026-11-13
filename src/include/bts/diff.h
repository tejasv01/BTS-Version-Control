// Diff Engine (Myers O(ND) line diff & unified diff formatting)
// Owner: Tejasv Lata (Staging Index & Diff Engine)

#ifndef BTS_DIFF_H
#define BTS_DIFF_H

#include <cstddef>
#include <string>
#include <vector>

namespace bts {

enum class DiffOp { Equal, Delete, Insert };

struct DiffEdit {
    DiffOp      op;
    std::string line;
    std::size_t old_line_no;  // 1-based line number in old file (0 if Insert)
    std::size_t new_line_no;  // 1-based line number in new file (0 if Delete)
};

/// Return true if content contains any NUL byte ('\0') (REQ-DIF-3).
bool is_binary_content(const std::string& content);

/// Split file content into lines by '\n' (preserving empty content as 0 lines).
std::vector<std::string> split_lines(const std::string& content);

/// Compute a shortest edit script using Myers' O(ND) difference algorithm (REQ-DIF-1).
std::vector<DiffEdit> compute_myers_diff(const std::vector<std::string>& old_lines,
                                         const std::vector<std::string>& new_lines);

/// Format unified diff output with --- / +++ and @@ -s,l +s,l @@ headers (REQ-DIF-2, REQ-DIF-3).
std::string format_unified_diff(const std::string& path,
                                const std::string& old_content,
                                const std::string& new_content,
                                std::size_t context_lines = 3);

}  // namespace bts
#endif  // BTS_DIFF_H

