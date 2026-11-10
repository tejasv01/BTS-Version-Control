# BTS SRS: Proposed Additions and Revisions

Drop-in text for the BTS-Version-Control (bts) SRS. Priority key: **E** = Essential, **D** = Desirable, **O** = Optional.

## A. Revision History (new, place after the title page)

| Version | Date | Author(s) | Description |
|---|---|---|---|
| 0.1 | Nov 2026 | Tejasv , Sarthak | Initial draft |
| 0.2 | Nov 2026 | Tejasv Lata, Sarthak | Added priorities, error and configuration requirements, edge cases, full traceability matrix, appendices |

## B. Corrections to Section 1

**1.3 (BTS):** Replace the entry with: *BTS: Project name, derived from the authors' first names (Bharat, Tejasv, Sarthak).*

**1.3 (add):** *Detached HEAD: A state in which HEAD contains a commit hash directly instead of a branch reference. Unborn branch: A branch that HEAD references but whose ref file does not yet exist because no commit has been made.*

**1.5 Overview (replace):** The remainder of this specification is organized into four sections and appendices. Section 2 covers product context, functions, users, constraints, and apportioning of requirements. Section 3 gives specific requirements: interfaces, functional, non-functional, and data requirements. Section 4 defines the verification and traceability matrix. Appendices give the object and index formats.

## C. Additions to Section 2

### 2.1 Product Perspective (append)

**System interfaces:** bts is standalone and has no interface to other software systems. It depends only on the operating system's filesystem API and standard library (SHA-256, zlib).

**Operations:** The tool is invoked interactively or from shell scripts. It runs as a short-lived process per command, holds no state between invocations other than the contents of `.bts/`, and requires no installation beyond placing the executable on `PATH`.

**Site adaptation:** Repository-specific settings (author name, author email) are stored in `.bts/config`.

### 2.4 General Constraints (append)

- **Regulatory:** None. The project is academic and handles no personal data beyond the author name and email entered by the user.
- **Language and toolchain:** bts shall be implemented in C++17 and built with CMake and a GCC or Clang compiler on Linux and macOS. It shall use `std::filesystem` for path handling, zlib for compression, and OpenSSL libcrypto (EVP interface) for SHA-256. Unit tests shall use GoogleTest and run through CTest. The built executable shall not require network access at runtime.
- **Concurrency:** bts is a single-user, single-process tool. Concurrent invocations on the same repository are unsupported, but shall be detected where practical using a lock file (`.bts/index.lock`, see RR-3).

### 2.5 Assumptions and Dependencies (append)

The build environment provides a C++17 compiler, CMake 3.16 or later, and development headers for zlib and OpenSSL. These are build-time and link-time dependencies only; no other runtime services are needed.

### 2.6 Apportioning of Requirements (new)

The following are deferred to future versions and are not part of this specification: merge and merge-conflict resolution, rebase, stash, tags, `.btsignore` patterns, packfiles and delta compression, garbage collection of unreachable objects, staged diff (`bts diff --cached`), file removal and rename commands (`bts rm`, `bts mv`), and remote operations.

## D. Additions and Revisions to Section 3

### 3.1.1 CLI Syntax (replace)

```
bts init
bts config <user.name | user.email> [<value>]
bts add <path ...>
bts status
bts commit -m "<message>"
bts branch [<name>]
bts branch -d <name>
bts checkout <branch-name | commit-hash>
bts log [--oneline]
bts diff
```

### 3.1.3 Communications Interfaces (new)

Not applicable. bts performs no network communication (see Section 1.2, Excluded capabilities).

### 3.1.4 Memory Interfaces (new)

bts shall process objects as streams or bounded buffers where practical, so that peak memory use does not exceed twice the size of the largest single file being hashed. No minimum primary-memory size is mandated beyond this.

### 3.2.0 Use Cases (new, place at the start of 3.2)

**TODO:** Add a short use-case list at the start of 3.2 (initialize, configure identity, stage and commit, branch and switch, inspect history), with each use case mapped to requirement IDs, if the evaluator expects organization by feature.

### 3.2 Revised Requirements (replace the originals)

| ID | Revised text | Pri. | Rationale |
|---|---|---|---|
| REQ-OBJ-4 | Upon reading any object, the system shall decompress it, recompute SHA-256 over the header and payload, and verify that the digest equals the 64-character hash derived from the object's path. On mismatch the system shall abort with an integrity error (see REQ-ERR-3). | E | Detects disk corruption and tampering. |
| REQ-ADD-1 | `bts add <path>` shall read the contents of the file at `<path>`, wrap them in a blob header per REQ-OBJ-1, compute the hash per REQ-OBJ-2, write the blob per REQ-OBJ-3 (skipping the write if the object already exists), and add or update the matching index entry. | E | Original text hashed the path, not the contents. |
| REQ-CHK-1 | `bts checkout <branch-name>` shall set HEAD to `ref: refs/heads/<branch-name>`, replace the index with the contents of the target commit's root tree, and update the working tree to match. | E | Unchanged in intent. |
| REQ-CHK-2 | If the index or working tree contains uncommitted changes that checkout would overwrite, the system shall abort without modifying any file and instruct the user to commit or discard those changes. | E | The word "stash" removed because stash is out of scope. |
| REQ-DIF-1 | `bts diff` shall compute line-based differences using a Myers or LCS algorithm between each tracked working-tree file and its staged index version. Comparison of the index against HEAD (`--cached`) is out of scope (see 2.6). | E | Removes ambiguity about diff scope. |
| PR-1 | On a reference machine (to be stated in the test report: CPU model, core count, RAM, SSD model, OS version, warm filesystem cache), staging and committing 1,000 files totalling under 50 MB shall complete within **TODO** seconds. | D | **TODO:** choose the target after benchmarking on the team's own machine. Reference benchmark (single-core 2.1 GHz Xeon, 1,000 files, 47.5 MB): hashing about 0.05 s; hash, compress, and write about 0.7 s at zlib level 1 and about 2.1 s at the default level. The original 200 ms target is not achievable with compression. |
| PR-2 | On the same reference machine, `bts status` on a clean repository of up to 5,000 files shall complete within **TODO** ms, relying on cached mtime and size per REQ-STAT-2. | D | **TODO:** choose the target after benchmarking. Aligned with the stated reference environment. |
| SR-1 | The system shall never modify or delete an existing object file. Newly written object files shall be created with permission mode 0444. | E | Original wording overstated what file permissions guarantee. |

### 3.2 New Functional Requirements

#### 3.2.9 Configuration

| ID | Requirement | Pri. | Rationale |
|---|---|---|---|
| REQ-CFG-1 | `bts config user.name <value>` and `bts config user.email <value>` shall store the value in `.bts/config`; with no value they shall print the stored value. | E | REQ-CMT-3 needs an author identity source. |
| REQ-CFG-2 | If user.name or user.email is unset, `bts commit` shall fail with a message naming the missing key and shall create no objects. | E | Prevents commits with empty authorship. |

#### 3.2.10 Additional Branching and Detached HEAD Requirements

| ID | Requirement | Pri. | Rationale |
|---|---|---|---|
| REQ-BRN-3 | `bts branch -d <name>` shall delete `.bts/refs/heads/<name>`. It shall refuse to delete the currently checked-out branch and shall report an error if the branch does not exist. | E | Listed in Product Functions (2.2) but previously unspecified. |
| REQ-BRN-4 | Branch names shall be non-empty and shall not contain whitespace, `..`, `~`, `^`, `:`, a backslash, a leading `-` or `/`, a trailing `/` or `.lock`, or control characters. `bts branch <name>` shall fail if the name is invalid or already exists. | E | Prevents ref-path escapes and duplicate overwrites. |
| REQ-BRN-5 | In an unborn-branch state, `bts branch` with no arguments shall list no branches, and `bts branch <name>` shall fail with an error stating that no commits exist yet. | D | Defined behavior for a fresh repository. |
| REQ-CHK-3 | `bts checkout <commit-hash>` shall resolve the hash (the full 64 characters, or an unambiguous prefix of at least **TODO** characters), write the full hash into HEAD (detached state), and update the index and working tree per REQ-CHK-1. It shall print a notice that HEAD is detached. | E | Required by the CLI syntax and the HEAD definition. |
| REQ-CHK-4 | If the checkout target matches neither a branch nor an existing commit, checkout shall fail without modifying any file. | E | Error handling. |

#### 3.2.11 Additional Staging, History, and Diff Requirements

| ID | Requirement | Pri. | Rationale |
|---|---|---|---|
| REQ-ADD-4 | The index and tree entries shall record file mode as either regular (100644) or executable (100755), based on the owner execute bit. | D | Needed to restore the executable bit on checkout. |
| REQ-ADD-5 | Symbolic links shall be stored as blobs whose content is the link target, with mode 120000, and shall not be followed. | O | Avoids infinite traversal and keeps behavior defined. |
| REQ-ADD-6 | Empty directories shall not be tracked. A path that does not exist shall cause `bts add` to fail with an error without altering the index. | E | Trees cannot represent empty directories under this design. |
| REQ-ADD-7 | The directory `.bts/` shall never be staged or traversed. | E | Prevents recursive self-inclusion. |
| REQ-LOG-3 | Abbreviated hashes shall be 7 characters by default. If a 7-character prefix is ambiguous among reachable commits, it shall be lengthened until unique. | D | Handles prefix collisions. **TODO:** decide the minimum prefix length for checkout and keep it consistent with this requirement. |
| REQ-LOG-4 | In an unborn-branch state, `bts log` shall print an error that no commits exist and exit with a non-zero status. | D | Defined behavior. |
| REQ-DIF-3 | If either version of a file contains a NUL byte, `bts diff` shall print `Binary files differ` instead of a line diff. | D | Avoids unreadable output. |

#### 3.2.12 Error Handling

| ID | Requirement | Pri. | Rationale |
|---|---|---|---|
| REQ-ERR-1 | Any command other than `init` run outside a repository shall exit non-zero with the message `not a bts repository`. The tool shall search parent directories for `.bts/` before reporting this error. | E | Standard behavior. |
| REQ-ERR-2 | An unknown subcommand or malformed arguments shall print a usage summary to standard error and exit with status 2. | E | Predictable scripting behavior. |
| REQ-ERR-3 | Integrity errors (REQ-OBJ-4) shall exit with a distinct non-zero status (3) and name the offending object hash. | E | Distinguishes corruption from user error. |

### 3.3 New Non-Functional Requirements

#### 3.3.2 Reliability (append)

| ID | Requirement | Pri. |
|---|---|---|
| RR-3 | Before mutating the index or any ref, the system shall create a lock file (`.bts/index.lock` or `<ref>.lock`) using exclusive creation. If it already exists, the system shall abort with an error and leave existing data untouched. The lock is removed on completion. | D |

#### 3.3.5 Maintainability (new)

| ID | Requirement | Pri. |
|---|---|---|
| MNT-1 | The codebase shall be organized into separate modules for object storage, index, refs, and command handling, with no module reading `.bts/` files owned by another. | D |
| MNT-2 | Each module shall have unit tests, and the automated test suite shall be runnable with a single documented command. | E |
| MNT-3 | The on-disk formats of objects and the index shall be versioned (see Appendices A and B) so that future versions can detect an unsupported format. | O |

#### 3.3.6 Usability (new)

| ID | Requirement | Pri. |
|---|---|---|
| USR-1 | Every error message shall go to standard error, state what failed, and, where possible, suggest a corrective action. | D |

## E. Appendices (new)

### Appendix A: Object Format

```
blob   : "blob <n>\0" + <n bytes of file content>
tree   : "tree <n>\0" + repeated entries: "<mode> <name>\0" + <32-byte raw SHA-256>
commit : "commit <n>\0" + text:
         tree <64-hex>
         parent <64-hex>        (zero or more lines)
         author <name> <<email>> <epoch> <tz>

         <message>
```

Tree entries shall be sorted by name in byte order. The stored file is `zlib(header + payload)`; the object ID is `SHA-256(header + payload)`.

### Appendix B: Index File Layout

```
Header : magic "BTSI" | version (uint32) | entry count (uint32)
Entry  : mode (uint32) | mtime (uint64 seconds, uint32 nanoseconds)
         | file_size (uint64) | sha256 (32 bytes)
         | path length (uint16) | path (UTF-8, '/' separators)
Footer : SHA-256 checksum of all preceding bytes
```

Entries shall be sorted by path in byte order. A footer mismatch is treated as an integrity error (REQ-ERR-3).

## F. Complete Verification and Traceability Matrix (replace Section 4)

Method key: **UT** unit test, **FT** functional test, **IT** integration test, **BT** boundary test, **ST** stress or fault injection, **PT** performance test, **IN** inspection.

| Req ID | Requirement summary | Pri. | Method | Pass criteria |
|---|---|---|---|---|
| REQ-INIT-1 | Create empty repository structure | E | FT | `objects/`, `refs/heads/`, `index`, and HEAD (`ref: refs/heads/main`) exist. |
| REQ-INIT-2 | Refuse re-initialization | E | FT | Second `init` exits non-zero, prints already-initialized error, and file checksums in `.bts/` are unchanged. |
| REQ-OBJ-1 | Object header format | E | UT | Serialized bytes begin with `<type> <size>\0`. |
| REQ-OBJ-2 | SHA-256 object ID | E | UT | Known input yields the expected 64-hex digest. |
| REQ-OBJ-3 | zlib storage at `objects/xx/yyyy` | E | IT | File is at the correct path; decompression returns the original bytes; stored size is smaller for compressible input. |
| REQ-OBJ-4 | Integrity check on read | E | UT | A tampered object triggers an integrity error naming its hash. |
| REQ-ADD-1 | Stage file as blob and index entry | E | FT | Blob exists in `objects/`; index entry has the correct hash. |
| REQ-ADD-2 | Index entry fields | E | UT | Entry round-trips mode, mtime, size, hash, and path. |
| REQ-ADD-3 | Recursive directory staging | E | FT | Every nested file is in the index. |
| REQ-ADD-4 | Regular and executable modes | D | FT | A `chmod +x` file is recorded as 100755 and restored on checkout. |
| REQ-ADD-5 | Symlink handling | O | FT | A symlink is stored as mode 120000 with the target as content. |
| REQ-ADD-6 | Empty directory and missing path | E | BT | Empty directory stages nothing; missing path exits non-zero with the index unchanged. |
| REQ-ADD-7 | Exclude `.bts/` | E | FT | `bts add .` leaves no `.bts/` paths in the index. |
| REQ-STAT-1 | Three-category status | E | FT | Staged, unstaged, and untracked files appear under the correct heading. |
| REQ-STAT-2 | Stat-cache shortcut | E | UT | With unchanged mtime and size, no file is re-hashed (hash call counter is 0). |
| REQ-CMT-1 | Reject empty commit | E | FT | Commit with index equal to HEAD exits non-zero and creates no commit. |
| REQ-CMT-2 | Build nested trees | E | IT | Root tree hash is reproducible and each subdirectory has its own tree object. |
| REQ-CMT-3 | Commit object fields | E | IT | Parsed commit has the correct tree, parent (none for the root commit), author, timestamp, and message. |
| REQ-CMT-4 | Atomic ref update | E | IT | Branch ref holds the new hash after the commit; no `.tmp` file remains. |
| REQ-LOG-1 | Traverse history to root | E | FT | Output lists commits newest-first, ending at the root. |
| REQ-LOG-2 | `--oneline` format | E | FT | Each line is a 7-character hash plus the first message line. |
| REQ-LOG-3 | Abbreviation uniqueness | D | UT | With two commits sharing a 7-character prefix, shown prefixes are unique. |
| REQ-LOG-4 | Log on unborn branch | D | BT | Non-zero exit and a no-commits message. |
| REQ-BRN-1 | List branches with `*` | E | FT | Exactly one branch is starred, the current one. |
| REQ-BRN-2 | Create branch at HEAD | E | FT | New ref contains the HEAD commit hash; the working tree is unchanged. |
| REQ-BRN-3 | Delete branch | E | FT | Ref file is removed; deleting the current or a missing branch fails. |
| REQ-BRN-4 | Branch name validation | E | BT | Each invalid name in the rule list is rejected, and duplicates are refused. |
| REQ-BRN-5 | Branch ops on unborn branch | D | BT | List is empty; create fails with a clear error. |
| REQ-CHK-1 | Checkout branch | E | IT | HEAD, index, and working tree match the target commit. |
| REQ-CHK-2 | Overwrite protection | E | BT | Aborts with non-zero exit when unstaged edits collide with target files; no files are changed. |
| REQ-CHK-3 | Detached checkout | E | IT | HEAD contains the full commit hash and a detached notice is printed; **TODO:** add prefix-length cases once the minimum is decided. |
| REQ-CHK-4 | Unknown checkout target | E | BT | Non-zero exit and no files modified. |
| REQ-DIF-1 | Line diff of working tree vs index | E | UT | Output matches reference diff results on a set of test inputs. |
| REQ-DIF-2 | Unified diff format | E | FT | Output has `---`/`+++` headers and correct `@@ -s,l +s,l @@` chunk headers. |
| REQ-DIF-3 | Binary file handling | D | FT | NUL-containing file prints `Binary files differ`. |
| REQ-CFG-1 | Set and get config | E | FT | Value persists in `.bts/config` and is printed back. |
| REQ-CFG-2 | Commit requires identity | E | FT | Commit fails naming the missing key; the object count is unchanged. |
| REQ-ERR-1 | Outside-repository error | E | FT | Exit non-zero with `not a bts repository`; works from a subdirectory of a repo. |
| REQ-ERR-2 | Usage error | E | FT | Exit status 2 and a usage message on stderr. |
| REQ-ERR-3 | Integrity error status | E | FT | Corrupted object yields exit status 3 and the offending hash. |
| PR-1 | Stage and commit 1,000 files | D | PT | **TODO:** limit pending. Completes within the stated limit on the documented reference machine. |
| PR-2 | Status on 5,000 files | D | PT | Completes within the stated limit on a clean repository. |
| RR-1 | Atomic writes | E | ST | Killing the process with SIGKILL mid-commit leaves the prior refs and index readable and valid. |
| RR-2 | Hash mismatch is fatal | E | UT | Corrupted object prevents further processing. |
| RR-3 | Lock files | D | FT | A pre-existing lock file makes the command abort without data changes. |
| PO-1 | POSIX separators | E | UT | All stored paths use `/` only. |
| PO-2 | Cross-platform readability | E | IT | A repository created on Linux passes `log` and `status` on macOS, and vice versa. |
| SR-1 | Immutable object files | E | IT | New objects have mode 0444, and no code path rewrites an existing object (verified by test and code inspection). |
| MNT-1 | Modular code structure | D | IN | Review confirms the module boundaries. |
| MNT-2 | Unit tests and one-command run | E | IN | Single documented command runs the suite and passes. |
| MNT-3 | Versioned formats | O | IN | Object and index formats carry version fields. |
| USR-1 | Actionable errors on stderr | D | IN | Sampled error messages go to stderr and state a cause. |

## G. Open Items and Decisions

| # | Item | Status |
|---|---|---|
| 1 | Performance targets (PR-1, PR-2) | **TODO.** Benchmark on your own machine, then set the limits. Reference numbers are in the PR-1 rationale. |
| 2 | Implementation language | **Done.** C++17, CMake, zlib, OpenSSL libcrypto, GoogleTest (see 2.4 and 2.5). |
| 3 | Hash abbreviation floor (REQ-CHK-3, REQ-LOG-3) | **TODO.** Choose a minimum prefix length and keep both requirements consistent. |
| 4 | Section 3 ordering by feature or use case | **TODO.** See 3.2.0. |
