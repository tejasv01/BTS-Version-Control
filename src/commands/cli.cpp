#include "bts/cli.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "bts/branch.h"
#include "bts/checkout.h"
#include "bts/index.h"
#include "bts/log.h"
#include "bts/object_store.h"
#include "bts/refs.h"

namespace bts {

namespace {

void print_usage() {
    std::cerr
        << "usage: bts <command> [<args>]\n"
        << "\n"
        << "Available commands:\n"
        << "   init       Initialize a new repository\n"
        << "   config     Get or set repository configuration\n"
        << "   add        Stage file contents\n"
        << "   status     Show working tree status\n"
        << "   commit     Record changes to the repository\n"
        << "   log        Show commit history\n"
        << "   branch     List, create, or delete branches\n"
        << "   checkout   Switch branches or restore files\n"
        << "   diff       Show changes between working tree and index\n";
}

}  // anonymous namespace

int cli_main(int argc, char* argv[]) {
    // No arguments or --help
    if (argc < 2) {
        print_usage();
        return 2;
    }

    std::string command = argv[1];

    if (command == "--help" || command == "-h") {
        print_usage();
        return 2;
    }

    // ── Check if command is known (REQ-ERR-2) ───────────────────────────
    static const std::vector<std::string> valid_commands = {
        "init", "config", "add", "status", "commit", "log", "branch", "checkout", "diff"
    };
    if (std::find(valid_commands.begin(), valid_commands.end(), command) == valid_commands.end()) {
        std::cerr << "bts: '" << command
                  << "' is not a bts command. See 'bts --help'." << std::endl;
        return 2;
    }

    // ── init: does NOT require find_repo_root ────────────────────────────
    if (command == "init") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts init [<directory>]" << std::endl;
            return 0;
        }
        fs::path target = fs::current_path();
        if (argc >= 3) {
            target = argv[2];
        }
        return cmd_init(target);
    }

    // ── All other commands require a repo ────────────────────────────────
    auto repo_opt = find_repo_root(fs::current_path());
    if (!repo_opt.has_value()) {
        std::cerr << "fatal: not a bts repository (or any of the parent "
                     "directories)"
                  << std::endl;
        return 1;
    }
    fs::path repo = repo_opt.value();

    // ── config ───────────────────────────────────────────────────────────
    if (command == "config") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts config <key> [<value>]" << std::endl;
            return 0;
        }
        if (argc < 3) {
            std::cerr << "usage: bts config <key> [<value>]" << std::endl;
            return 2;
        }
        std::string key = argv[2];
        if (argc >= 4) {
            // Set mode
            std::string value = argv[3];
            return cmd_config(repo, key, value, true);
        } else {
            // Get mode
            return cmd_config(repo, key, "", false);
        }
    }

    // ── add ──────────────────────────────────────────────────────────────
    if (command == "add") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts add <path>..." << std::endl;
            return 0;
        }
        if (argc < 3) {
            std::cerr << "usage: bts add <path>..." << std::endl;
            return 2;
        }
        std::vector<std::string> paths;
        for (int i = 2; i < argc; ++i) {
            paths.emplace_back(argv[i]);
        }
        return cmd_add(repo, paths);
    }

    // ── status ───────────────────────────────────────────────────────────
    if (command == "status") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts status" << std::endl;
            return 0;
        }
        return cmd_status(repo);
    }

    // ── commit ───────────────────────────────────────────────────────────
    if (command == "commit") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts commit -m <message>" << std::endl;
            return 0;
        }
        // Parse -m flag
        std::string message;
        bool found_m = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "-m" && i + 1 < argc) {
                message = argv[i + 1];
                found_m = true;
                break;
            }
        }
        if (!found_m) {
            std::cerr << "error: must provide commit message with -m"
                      << std::endl;
            return 2;
        }
        return cmd_commit(repo, message);
    }

    // ── log ──────────────────────────────────────────────────────────────
    if (command == "log") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts log [--oneline]" << std::endl;
            return 0;
        }
        bool oneline = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--oneline") {
                oneline = true;
            }
        }
        return cmd_log(repo, oneline);
    }

    // ── branch ───────────────────────────────────────────────────────────
    if (command == "branch") {
        // Pass remaining args (after "branch") to cmd_branch
        int branch_argc = argc - 2;
        const char* const* branch_argv =
            (branch_argc > 0) ? &argv[2] : nullptr;
        return cmd_branch(repo, branch_argc, branch_argv);
    }

    // ── checkout ─────────────────────────────────────────────────────────
    if (command == "checkout") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts checkout <branch|commit>" << std::endl;
            return 0;
        }
        if (argc < 3) {
            std::cerr << "usage: bts checkout <branch|commit>" << std::endl;
            return 2;
        }
        return cmd_checkout(repo, argv[2]);
    }

    // ── diff ─────────────────────────────────────────────────────────────
    if (command == "diff") {
        if (argc >= 3 && std::string(argv[2]) == "--help") {
            std::cout << "usage: bts diff" << std::endl;
            return 0;
        }
        return cmd_diff(repo);
    }

    // ── Unknown command ──────────────────────────────────────────────────
    std::cerr << "bts: '" << command
              << "' is not a bts command. See 'bts --help'." << std::endl;
    return 2;
}

}  // namespace bts
