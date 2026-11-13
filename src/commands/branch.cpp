#include "bts/branch.h"

#include <cstring>
#include <iostream>
#include <string>

#include "bts/refs.h"

namespace bts {

int cmd_branch(const fs::path& repo, int argc, const char* const* argv) {
    // ── No arguments → list branches (REQ-BRN-1) ────────────────────────────
    if (argc == 0) {
        auto branches = list_branches(repo);
        auto cur = current_branch(repo);

        for (const auto& branch : branches) {
            if (cur.has_value() && cur.value() == branch) {
                std::cout << "* " << branch << "\n";
            } else {
                std::cout << "  " << branch << "\n";
            }
        }
        return 0;
    }

    std::string first_arg = argv[0];

    // ── --help ───────────────────────────────────────────────────────────────
    if (first_arg == "--help") {
        std::cout << "usage: bts branch [<name>]\n"
                  << "   or: bts branch -d <name>\n"
                  << "\n"
                  << "List, create, or delete branches.\n";
        return 0;
    }

    // ── Delete branch: -d <name> (REQ-BRN-3) ────────────────────────────────
    if (first_arg == "-d") {
        if (argc != 2) {
            std::cerr << "usage: bts branch -d <name>" << std::endl;
            return 2;
        }
        std::string name = argv[1];

        // Check if name is current branch
        auto cur = current_branch(repo);
        if (cur.has_value() && cur.value() == name) {
            std::cerr << "error: Cannot delete branch '" << name
                      << "' checked out at '"
                      << fs::canonical(repo).string() << "'" << std::endl;
            return 1;
        }

        // Check if branch exists
        auto ref = read_ref(repo, "refs/heads/" + name);
        if (!ref.has_value()) {
            std::cerr << "error: branch '" << name << "' not found."
                      << std::endl;
            return 1;
        }

        // Delete it
        if (!delete_ref(repo, "refs/heads/" + name)) {
            std::cerr << "error: failed to delete branch '" << name << "'"
                      << std::endl;
            return 1;
        }

        std::cout << "Deleted branch " << name << std::endl;
        return 0;
    }

    if (!first_arg.empty() && first_arg[0] == '-') {
        std::cerr << "error: unknown option '" << first_arg
                  << "'\nusage: bts branch [<name>] | -d <name>" << std::endl;
        return 2;
    }

    if (argc > 1) {
        std::cerr << "usage: bts branch [<name>]" << std::endl;
        return 2;
    }

    // ── Create branch: <name> (REQ-BRN-2) ───────────────────────────────────
    std::string name = first_arg;

    // Validate branch name (REQ-BRN-4)
    std::string err = validate_branch_name(name);
    if (!err.empty()) {
        std::cerr << "fatal: " << err << std::endl;
        return 1;
    }

    // resolve_head → need a valid commit to branch from (REQ-BRN-5)
    auto head_hash = resolve_head(repo);
    if (!head_hash.has_value()) {
        std::cerr
            << "fatal: not a valid object name: no commits yet"
            << std::endl;
        return 1;
    }

    // Check if branch already exists
    auto existing = read_ref(repo, "refs/heads/" + name);
    if (existing.has_value()) {
        std::cerr << "fatal: A branch named '" << name << "' already exists."
                  << std::endl;
        return 1;
    }

    // Create the branch ref
    if (!update_branch_ref(repo, name, head_hash.value())) {
        std::cerr << "fatal: failed to create branch '" << name << "'"
                  << std::endl;
        return 1;
    }

    return 0;
}

}  // namespace bts
