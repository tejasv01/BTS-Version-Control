#ifndef BTS_CLI_H
#define BTS_CLI_H

namespace bts {

/// Main CLI dispatcher. Parses argv and routes to the correct command.
/// Returns the exit code.
int cli_main(int argc, char* argv[]);

}  // namespace bts
#endif  // BTS_CLI_H
