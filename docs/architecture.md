# Architecture, Design, and UML Models

This document is the companion architecture and UML specification for **bts** (referenced in [SRS.md](SRS.md) Appendix D, items 1 and 2). It traces module boundaries, design decisions, and behavioral models to the requirement IDs in [SRS.md](SRS.md).

---

## 1. Layered Architecture (`MNT-1`)

`bts` separates CLI argument parsing, command orchestration, core VCS data structures, and low-level cryptographic/compression primitives into distinct layers:

```text
Tier 1: CLI Interface          src/main.cpp, src/commands/cli.cpp
          │
Tier 2: Porcelain Commands     src/commands/branch.cpp, checkout.cpp, log.cpp
          │                    src/core/index.cpp (add, status, commit), diff.cpp
          ▼
Tier 3: Core VCS Engine
          ├── Object Store     src/include/bts/object_store.h, src/core/object_store_stub.cpp
          ├── Staging Index    src/include/bts/index.h, src/core/index.cpp
          ├── Ref Manager      src/include/bts/refs.h, src/core/refs.cpp
          └── Diff Engine      src/include/bts/diff.h, src/core/diff.cpp
          │
Tier 4: Storage & Object Model src/storage/{hash,compress,object_store}.*
                               src/objects/{blob,tree,commit}.*
```

### Module Responsibilities

| Module | Files | Owned `.bts/` State | Key Requirements |
|---|---|---|---|
| **CLI Dispatcher** | `src/main.cpp`, `src/commands/cli.cpp` | None | `REQ-ERR-1`, `REQ-ERR-2`, `REQ-ERR-3`, `REQ-ERR-4` |
| **Object Storage** | `src/include/bts/object_store.h`, `src/core/object_store_stub.cpp`, `src/storage/*`, `src/objects/*` | `.bts/objects/`, `.bts/config` | `REQ-INIT-1..2`, `REQ-OBJ-1..4`, `REQ-CFG-1..3`, `SR-1` |
| **Staging Index** | `src/include/bts/index.h`, `src/core/index.cpp` | `.bts/index`, `.bts/index.lock` | `REQ-ADD-1..7`, `REQ-STAT-1..3`, `REQ-CMT-1..5`, `MNT-3`, `RR-1`, `RR-3` |
| **Diff Engine** | `src/include/bts/diff.h`, `src/core/diff.cpp` | None (reads via Index & Object Store) | `REQ-DIF-1..3` |
| **Ref Manager** | `src/include/bts/refs.h`, `src/core/refs.cpp` | `.bts/HEAD`, `.bts/refs/heads/*` | `REQ-BRN-1..5`, `RR-1`, `RR-3` |
| **Branch / Checkout / Log** | `src/commands/{branch,checkout,log}.cpp` | None (uses Refs, Index, Object Store) | `REQ-BRN-1..5`, `REQ-CHK-1..4`, `REQ-LOG-1..4` |

---

## 2. Architecture Decision Records (ADRs)

### ADR-1: Content-Addressed SHA-256 Loose Object Store with Immutable `0444` Permissions
- **Context:** BTS requires tamper-evident storage for blobs, trees, and commits (`REQ-OBJ-1..4`, `SR-1`) without shelling out to `git`.
- **Options Considered:** (A) SHA-1 loose store (legacy Git default); (B) Packfile-only compressed archive; (C) SHA-256 loose store compressed with zlib level 1 and `0444` permissions (`OpenSSL EVP`).
- **Decision:** **Option C** — SHA-256 via `OpenSSL::Crypto` `EVP_sha256()`, zlib level 1 compression, atomic temp-file write + `fsync` + `0444` `chmod` + `rename`, and object-read re-hashing (`IntegrityException` on mismatch).
- **Consequences:** Eliminates SHA-1 collision concerns (`SR-1`), prevents accidental in-place mutation (`0444`), avoids overwriting existing content-addressed objects, and meets `PR-1` throughput (`< 0.3 s` for 1,000 files). Future packfiles remain additive.

### ADR-2: Versioned Binary Staging Index (`"BTSI"` v1) with Embedded SHA-256 Footer & `lstat` Cache
- **Context:** `bts status` must scan 5,000 clean files in `< 200 ms` (`PR-2`) while detecting index corruption (`REQ-ERR-3`) and supporting future format upgrades (`MNT-3`).
- **Options Considered:** (A) Plain-text CSV/INI index; (B) SQLite database; (C) Custom big-endian binary format `"BTSI"` with `lstat` metadata and a 32-byte trailing SHA-256 digest (`Appendix B`).
- **Decision:** **Option C** — Zero external database dependencies, deterministic sorting by POSIX `/` path (`PO-1`), explicit `uint32_t version = 1` validation (`IndexIntegrityException` on unsupported versions or checksum failure), and `mtime`/`size`/`mode` stat cache (`REQ-STAT-2`).
- **Consequences:** Enables `O(N)` single-syscall `lstat()` short-circuiting (`0` blob reads on clean status) and guaranteed fail-closed corruption detection (`exit 3`).

### ADR-3: POSIX Exclusive Lock Files (`O_CREAT | O_EXCL`) + `fsync` + Atomic `rename()`
- **Context:** Sudden process termination or concurrent writers must never leave a truncated `.bts/index` or `.bts/refs/heads/*` (`RR-1`, `RR-3`).
- **Options Considered:** (A) In-place file truncation/overwrite; (B) `flock()` advisory locking only; (C) `O_CREAT | O_EXCL` `.lock` file creation, write + `fsync`, and atomic `rename()`.
- **Decision:** **Option C** (`write_index` in `src/core/index.cpp`, `write_ref_atomic` in `src/core/refs.cpp`).
- **Consequences:** Guarantees strict single-writer mutual exclusion (`RR-3`) and preserves valid pre-crash state under `SIGKILL` (verified in `SystemE2ETest.FaultInjection_SigkillPreservesConsistentRepoState`). Orphaned locks require documented manual removal after abnormal termination.

### ADR-4: Myers $O(ND)$ Shortest-Edit-Script Line Diff
- **Context:** `bts diff` (`REQ-DIF-1..3`) must produce minimal unified diff hunks without spawning external `diff` binaries.
- **Options Considered:** (A) Quadratic $O(M \times N)$ DP LCS table; (B) Myers $O(ND)$ diagonal frontier search (`src/core/diff.cpp`); (C) Shelling out to `/usr/bin/diff`.
- **Decision:** **Option B** — Pure C++17 Myers linear-trace frontier diff normalizing `Delete`-before-`Insert` replacements and formatting 3-line unified hunks (`---`/`+++`/`@@`), short-circuiting binary files containing `NUL` (`\0`) bytes.
- **Consequences:** Sub-millisecond execution on typical source diffs (`D << N`) with bounded memory overhead compared to full DP matrices.

### ADR-5: Tiered Modularity & Exception-to-Exit-Code Boundary
- **Context:** CLI error handling must cleanly distinguish usage errors (`2`), operational errors (`1`), and repository corruption (`3`) (`REQ-ERR-1..4`), while allowing Bharat, Tejasv, and Sarthak to build independent sub-modules (`MNT-1`, `MNT-2`).
- **Options Considered:** (A) `std::exit()` inside core storage functions; (B) Returning `int` error codes everywhere including mid-loop deserializers; (C) Typed exceptions (`IntegrityException`, `IndexIntegrityException`) thrown by persistence layers and caught uniformly at `cli_main`.
- **Decision:** **Option C** — Core persistence throws typed exceptions on tamper/format corruption, `cli_main` translates them to `stderr` + exit code `3`, command logic returns `1` for operational errors, and `cli_main` returns `2` for syntax/flag violations.
- **Consequences:** Core modules stay library-safe and unit-testable in GoogleTest without terminating the test runner process.

---

## 3. Software Quality Assurance (SQA) Architectural Mapping

| Quality Attribute | SRS Requirements | Architectural Mechanism | Primary Code & Test Verification |
| :--- | :--- | :--- | :--- |
| **Reliability** | `RR-1`, `RR-2`, `RR-3` | `O_CREAT \| O_EXCL` lock files, `fsync` + `rename()`, mandatory SHA-256 verification on object/index read | `src/core/index.cpp`, `src/core/refs.cpp`, `test_refs`, `test_index`, `test_system_e2e` (`SIGKILL` fault injection) |
| **Maintainability** | `MNT-1`, `MNT-2`, `MNT-3` | Four-tier module isolation (CLI -> Commands -> Core -> Storage), big-endian versioned `"BTSI"` header (`v1`), modular CMake targets (`bts_lib`, `bts_storage`, `bts_objects`) | [CMakeLists.txt](../CMakeLists.txt), [maintenance.md](maintenance.md), `IndexTest.UnsupportedVersionThrows` |
| **Security & Integrity** | `SR-1`, `REQ-OBJ-4`, `REQ-ERR-3` | Cryptographic `EVP_sha256()` digest check on every read, immutable `0444` loose object permissions, zero shell-out execution | `src/core/object_store_stub.cpp`, `src/storage/object_store.cpp`, `test_diff`, `test_add_status_commit`, `storage_tests` |
| **Usability** | `USR-1`, `REQ-ERR-2`, `REQ-CHK-2..3` | Subcommand `--help` usage strings, strict unknown-flag rejection (`exit 2`), non-destructive checkout conflict guards, ambiguous hash candidate listing | `src/commands/cli.cpp`, `src/commands/checkout.cpp`, `test_cli`, `test_checkout` |
| **Performance** | `PR-1`, `PR-2`, `REQ-STAT-2` | Zlib level-1 fast compression (`1,000` files in `~0.23 s` median), `lstat` `(mode, mtime, size)` cache eliminating file hashing on clean working trees (`5,000` files in `~40 ms` median) | `src/core/index.cpp`, [scripts/benchmark.py](../scripts/benchmark.py) |

---

## 4. Data Flow & Persistence Layout

```mermaid
flowchart TD
    WT["Working Tree File (lstat mode, size, mtime + read bytes)"] --> BlobFmt["Serialize Blob Payload (type + size + raw bytes)"]
    BlobFmt --> Sha["Compute SHA-256 Digest (OpenSSL EVP_sha256)"]
    Sha --> HexId["64-char Hex Object ID"]
    HexId --> ObjStore["Write Loose Object if New (.bts/objects/xx/62-hex, zlib level 1, chmod 0444)"]
    HexId --> IdxUpdate["Update .bts/index (BTSI v1 header + sorted entries + SHA-256 footer)"]
    IdxUpdate --> TreeBuild["build_trees_from_index and write_commit"]
    TreeBuild --> RefUpdate["Atomic Lock + fsync + rename (.bts/refs/heads/branch or HEAD)"]
```

---

## 5. UML Models (`Appendix D #1`)

### 5.1 Use-Case Diagram

```mermaid
flowchart LR
    User(["User"])
    User --- UC1["UC-1: Initialize repository (REQ-INIT-1..2)"]
    User --- UC2["UC-2: Configure identity (REQ-CFG-1..3)"]
    User --- UC3["UC-3: Stage and commit (REQ-ADD-1..7, REQ-STAT-1..3, REQ-CMT-1..5)"]
    User --- UC4["UC-4: Branch and switch (REQ-BRN-1..5, REQ-CHK-1..4)"]
    User --- UC5["UC-5: Inspect history and changes (REQ-LOG-1..4, REQ-DIF-1..3)"]
    UC3 -. includes .-> UC_Hash["Hash and store objects (REQ-OBJ-1..4)"]
    UC4 -. includes .-> UC_Hash
```

### 5.2 Class Diagram

```mermaid
classDiagram
    class IndexEntry {
        +uint32_t mode
        +uint64_t mtime_sec
        +uint32_t mtime_nsec
        +uint64_t file_size
        +string sha256
        +string path
    }

    class TreeEntry {
        +uint32_t mode
        +string name
        +string hash
    }

    class CommitData {
        +string tree_hash
        +vector~string~ parents
        +string author
        +string timestamp
        +string message
    }

    class DiffEdit {
        +DiffOp op
        +string line
        +size_t old_line_no
        +size_t new_line_no
    }

    class IndexManager {
        +read_index(repo) vector~IndexEntry~
        +write_index(repo, entries) bool
        +cmd_add(repo, paths) int
        +cmd_status(repo) int
        +cmd_commit(repo, msg) int
        +build_trees_from_index(repo) string
    }

    class DiffEngine {
        +is_binary_content(content) bool
        +split_lines(content) vector~string~
        +compute_myers_diff(old_lines, new_lines) vector~DiffEdit~
        +format_unified_diff(path, old_c, new_c) string
        +cmd_diff(repo) int
    }

    class ObjectStore {
        +hash_content(data) string
        +write_object(repo, type, data) string
        +read_object(repo, hash) optional~string~
        +write_tree(repo, entries) string
        +write_commit(repo, tree, parents, author, msg) string
    }

    class RefManager {
        +read_head(repo) string
        +resolve_head(repo) optional~string~
        +write_head(repo, content) bool
        +write_ref_atomic(repo, ref, hash) bool
        +validate_branch_name(name) string
    }

    IndexManager --> IndexEntry : manages
    IndexManager --> ObjectStore : writes objects
    IndexManager --> RefManager : updates HEAD
    DiffEngine --> DiffEdit : produces
    DiffEngine --> IndexManager : reads index
    DiffEngine --> ObjectStore : reads blobs
    ObjectStore --> TreeEntry : serializes
    ObjectStore --> CommitData : parses
```

### 5.3 Sequence Diagram — `bts commit` (`REQ-CMT-1..5`, `REQ-CFG-2`)

```mermaid
sequenceDiagram
    actor User
    participant CLI as cli_main
    participant Idx as IndexManager
    participant Obj as ObjectStore
    participant Ref as RefManager

    User->>CLI: bts commit -m msg
    CLI->>Idx: cmd_commit(repo, msg)
    Idx->>Obj: get_config user.name and user.email
    alt Identity missing
        Idx-->>User: stderr missing config key (exit 1)
    else Identity present
        Idx->>Idx: read_index(repo)
        Idx->>Ref: resolve_head(repo)
        alt Empty index or matches HEAD tree
            Idx-->>User: stderr nothing to commit (exit 1)
        else Changes present
            Idx->>Obj: write_tree recursively (REQ-CMT-2)
            Idx->>Obj: write_commit(tree, parents, author, msg) (REQ-CMT-3)
            alt Symbolic HEAD (branch)
                Idx->>Ref: write_ref_atomic(repo, ref_target, commit_hash) (REQ-CMT-4)
            else Detached HEAD
                Idx->>Ref: write_head(repo, commit_hash) (REQ-CMT-5)
                Idx-->>User: stderr detached HEAD warning
            end
            Idx-->>User: stdout commit summary (exit 0)
        end
    end
```

### 5.4 Sequence Diagram — `bts checkout` (`REQ-CHK-1..4`)

```mermaid
sequenceDiagram
    actor User
    participant CLI as cli_main
    participant Chk as cmd_checkout
    participant Ref as RefManager
    participant Obj as ObjectStore
    participant Idx as IndexManager

    User->>CLI: bts checkout target
    CLI->>Chk: cmd_checkout(repo, target)
    Chk->>Ref: check refs/heads/target
    alt Branch exists
        Chk->>Ref: read_ref for branch target
    else Commit hash or prefix at least 4 chars
        Chk->>Obj: resolve_hash_prefix or read_object
    end
    Chk->>Obj: read_object(commit) and collect_tree_files
    Chk->>Idx: read_index(repo)
    Chk->>Chk: check local edits and untracked collisions (REQ-CHK-2)
    alt Conflict detected
        Chk-->>User: stderr abort without modifying files (exit 1)
    else Safe to switch
        Chk->>Chk: remove obsolete files, write blobs, restore modes
        Chk->>Idx: write_index(repo, new_index)
        Chk->>Ref: write_head(repo, ref or hash)
        Chk-->>User: stdout switched branch or stderr detached notice (exit 0)
    end
```

### 5.5 Sequence Diagram — `bts diff` & Tamper Detection (`REQ-DIF-1..3`, `REQ-ERR-3`)

```mermaid
sequenceDiagram
    actor User
    participant CLI as cli_main
    participant Diff as cmd_diff
    participant Idx as read_index
    participant Obj as read_object

    User->>CLI: bts diff
    CLI->>Diff: cmd_diff(repo)
    Diff->>Idx: read_index(repo)
    alt Corrupt index header or footer
        Idx-->>CLI: throw IndexIntegrityException
        CLI-->>User: stderr corruption message (exit 3)
    else Index valid
        loop Each tracked IndexEntry
            Diff->>Obj: read_object(repo, entry.sha256)
            alt Corrupt object digest or zlib stream
                Obj-->>CLI: throw IntegrityException
                CLI-->>User: stderr hash and error (exit 3)
            else Blob valid
                Diff->>Diff: read working tree or symlink target
                alt Binary NUL byte present
                    Diff-->>User: stdout Binary files differ
                else Text file modified
                    Diff->>Diff: compute_myers_diff (context 3)
                    Diff-->>User: stdout unified diff hunks
                end
            end
        end
    end
```

### 5.6 State Diagram — `HEAD` Lifecycle (`REQ-INIT-1`, `REQ-CMT-4..5`, `REQ-CHK-1`, `REQ-CHK-3`)

```mermaid
stateDiagram-v2
    [*] --> UnbornBranch : bts init sets HEAD to main branch
    UnbornBranch --> AttachedBranch : bts commit creates main ref
    AttachedBranch --> AttachedBranch : bts commit or bts checkout branch
    AttachedBranch --> DetachedHEAD : bts checkout commit-hash
    DetachedHEAD --> DetachedHEAD : bts commit updates HEAD and warns
    DetachedHEAD --> AttachedBranch : bts checkout branch
```

---

## 6. Data Flow Diagrams (DFD: Level-0, Level-1, and Level-2)

### 6.1 DFD Level-0 (Context Diagram)

```mermaid
flowchart LR
    Dev["Developer (External Entity)"] -->|"CLI commands and arguments"| Sys(("0.0 BTS Version Control System"))
    Sys -->|"Status, diffs, logs, exit codes 0..3"| Dev
    WT["Working Directory (External Entity)"] -->|"File bytes, symlinks, lstat metadata"| Sys
    Sys -->|"Restored files, modes, symlinks"| WT
```

### 6.2 DFD Level-1 (Core Subsystems and Data Stores)

```mermaid
flowchart TD
    Dev["Developer"] -->|"Commands and flags"| P1(("1.0 Init and Config"))
    Dev -->|"add / status"| P2(("2.0 Staging and Status"))
    Dev -->|"commit -m"| P3(("3.0 Tree and Commit Builder"))
    Dev -->|"branch / checkout / log"| P4(("4.0 Branch, Checkout and Log"))
    Dev -->|"diff"| P5(("5.0 Myers Diff Engine"))

    WT["Working Directory"] <-->|"Read bytes, lstat, restore files"| P2
    WT <-->|"Write target files and modes"| P4
    WT -->|"Read working file bytes"| P5

    P1 <-->|"Read and write key-value pairs"| D4[("D4: .bts/config")]
    P1 -->|"Create initial HEAD and dirs"| D3[("D3: .bts/refs and HEAD")]

    P2 <-->|"Write blobs and read HEAD tree"| D1[("D1: .bts/objects")]
    P2 <-->|"Read and atomic write BTSI index"| D2[("D2: .bts/index")]

    P3 -->|"Write tree and commit objects"| D1
    P3 -->|"Read staged entries"| D2
    P3 <-->|"Read HEAD and atomic update ref"| D3

    P4 <-->|"Read commits, trees, blobs"| D1
    P4 -->|"Rebuild index on checkout"| D2
    P4 <-->|"Read and update branch refs / HEAD"| D3

    P5 -->|"Read staged blobs"| D1
    P5 -->|"Read staged hashes"| D2
```

### 6.3 DFD Level-2 (Expansion of 2.0 Staging & 3.0 Commit Pipeline)

```mermaid
flowchart TD
    Dev["Developer"] -->|"bts add path..."| P21(("2.1 Path Resolver and lstat"))
    WT["Working Directory"] -->|"lstat mode, size, mtime"| P21
    WT -->|"Raw file or symlink bytes"| P22(("2.2 Blob Frame, SHA-256 and Zlib"))

    P21 -->|"Validated repo paths and modes"| P22
    P22 -->|"SHA-256 object ID and metadata"| P23(("2.3 Binary Index Serializer"))
    P22 -->|"Atomic 0444 compressed blob"| D1[("D1: .bts/objects")]

    D2[("D2: .bts/index")] <-->|"Read old and atomic rename new BTSI v1"| P23

    Dev -->|"bts commit -m msg"| P31(("3.1 Recursive Tree Builder"))
    D2 -->|"Sorted IndexEntry list"| P31
    D4[("D4: .bts/config")] -->|"user.name and user.email"| P32(("3.2 Commit and Atomic Ref Writer"))
    P31 -->|"Write sorted tree objects"| D1
    P31 -->|"Root tree SHA-256"| P32
    D3[("D3: .bts/refs and HEAD")] -->|"Current parent commit hash"| P32
    P32 -->|"Write commit object"| D1
    P32 -->|"Atomic lock + rename new tip hash"| D3
    P32 -->|"Branch and short hash summary"| Dev
```
