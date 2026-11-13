# Test Plan, SQA Verification & Benchmark Report

**Authors:** Bharat, Tejasv Lata, Sarthak  
**Baseline Reference:** [SRS.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/SRS.md) Section 4 & Appendix D (Items 3, 5)

---

## 1. Build & Verification Instructions (`MNT-2`)

### Prerequisites (macOS / Linux)
- C++17 compiler (`clang++` or `g++` 9+)
- CMake 3.16+
- OpenSSL 3 (`OpenSSL::Crypto`), zlib (`ZLIB::ZLIB`), GoogleTest (`GTest::gtest_main`)

### Single-Command Release Build & Full CTest Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure
```

---

## 2. Test Levels & Suite Traceability

BTS applies a four-tier automated test pyramid totaling **38 CTest entries** (**105 individual GoogleTest test cases** across 11 compiled test executables):

| Test Level | Test Target(s) | GoogleTest Cases | Scope & Responsibility |
|---|---|---|---|
| **Unit Testing (`UT`, `BT`)** | `storage_tests` (18 CTest entries), `objects_tests` (11 CTest entries), `test_refs` (17 cases), `test_index` (7 cases), `test_diff` (6 cases), `LogAbbrevTest` (2 cases) | **61** | OpenSSL SHA-256 vectors, zlib compression error handling, object store integrity exceptions, Blob/Tree/Commit round-trips, branch validation, binary index `"BTSI"` round-trip & corruption, Myers $O(ND)$ line diff, `abbreviate_hash` prefix collision widening |
| **Integration Testing (`IT`, `FT`)** | `test_branch` (10 cases), `test_checkout` (9 cases), `test_log` (6 cases), `test_cli` (11 cases), `test_add_status_commit` (6 cases) | **42** | Branch/HEAD lifecycle, overwrite protection for modified & colliding untracked files, ambiguous hash candidate listing, branch precedence, CLI flag & exit-code dispatch (`0`/`1`/`2`/`3`), subdirectory discovery, end-to-end staging/stat-cache/tree-building/commit |
| **System E2E & Fault Injection (`ST`)** | `test_system_e2e` (`FullWorkflowAndIntegrityExitCode3`, `FaultInjection_SigkillPreservesConsistentRepoState`) | **2** | Multi-step CLI workflow (`init` -> `config` -> `add` -> `status` -> `commit` -> `branch` -> `checkout` -> `diff` -> `log` -> corrupt index exit `3`) plus POSIX `fork()` + `SIGKILL` crash-safety verification (`RR-1`) |
| **Performance Benchmarking (`PR`)** | [scripts/benchmark.py](../scripts/benchmark.py) | **2 scenarios** | Separate Release benchmark against live `build/bts` binary with host/CMake auto-detection, warmup + multi-run `min`/`median`/`max` stats (`--runs`, `--json`), and clean-state assertions verifying `PR-1` (`< 3.0 s`) and `PR-2` (`< 200 ms`) |

---

## 3. Requirement-to-Test-Case Traceability (`SRS.md` Section 4)

| Req ID | Requirement Summary | Method | Verified Test Case(s) |
|---|---|---|---|
| `REQ-INIT-1` | Create `.bts/` repository structure | FT | `CliTest.Cli_Init` |
| `REQ-INIT-2` | Refuse re-initialization without modifying files | FT | `CliTest.Cli_InitAlreadyExists` |
| `REQ-OBJ-1`–`REQ-OBJ-3` | `<type> <size>\0` header, SHA-256 ID, zlib level-1 storage | UT/IT | `AddStatusCommitTest.AddRegularExecutableAndSymlink`, `HashTest.*`, `CompressTest.*`, `ObjectStoreTest.HashObjectFormat`, `ObjectStoreTest.WriteAndReadRoundTrip*` |
| `REQ-OBJ-4` | Decompress and verify SHA-256 on object read | UT | `ObjectStoreTest.CorruptObjectThrowsIntegrityException`, `AddStatusCommitTest.CorruptObjectYieldsExitCode3WithHash` |
| `REQ-ADD-1`–`REQ-ADD-3` | Stage blob & index entry, recursive directory staging | FT/UT | `AddStatusCommitTest.AddRegularExecutableAndSymlink`, `IndexTest.BinaryFormatRoundTripAndSorting` |
| `REQ-ADD-4`–`REQ-ADD-5` | Modes `100644`, `100755`, and symlink `120000` (without following) | FT | `AddStatusCommitTest.AddRegularExecutableAndSymlink`, `AddStatusCommitTest.NestedTreesDetachedCommitAndCheckoutRestore` |
| `REQ-ADD-6`–`REQ-ADD-7` | Empty dirs ignored, missing path fails cleanly, `.bts/` excluded | BT/FT | `AddStatusCommitTest.AddEmptyDirAndMissingPathAndLock`, `CliTest.Cli_OperationFailureExits1` |
| `REQ-STAT-1`–`REQ-STAT-3` | Staged/unstaged/untracked, stat-cache (`0` hashes), unborn notice | FT/UT/BT | `AddStatusCommitTest.StatusUnbornAndThreeCategoriesAndStatCache` |
| `REQ-CMT-1`–`REQ-CMT-5` | Reject empty commit, nested trees, commit fields, atomic ref update, detached warning | FT/IT | `AddStatusCommitTest.ConfigValidationAndEmptyCommitRejection`, `AddStatusCommitTest.NestedTreesDetachedCommitAndCheckoutRestore`, `BlobTest.*`, `TreeTest.*`, `CommitTest.*` |
| `REQ-LOG-1`–`REQ-LOG-4` | History traversal, `--oneline`, `abbreviate_hash` collision widening, unborn error, detached log | FT/UT/BT | `LogTest.Log_SingleCommit`, `LogTest.Log_MultipleCommits`, `LogTest.Log_MultipleCommits_Oneline`, `LogTest.Log_UnbornBranch`, `LogTest.Log_Detached`, `LogAbbrevTest.AbbreviateHash_MinimumSevenCharsWhenUnique`, `LogAbbrevTest.AbbreviateHash_LengthensOnPrefixCollision` |
| `REQ-BRN-1`–`REQ-BRN-5` | List (`*`), create, delete, name validation, unborn handling | FT/BT | `BranchTest.*`, `RefsTest.ValidateBranchName_Valid`, `RefsTest.ValidateBranchName_Invalid` |
| `REQ-CHK-1`–`REQ-CHK-4` | Branch checkout, overwrite protection (modified & untracked), prefix resolution + ambiguity + branch precedence | IT/BT | `CheckoutTest.CheckoutBranch_Success`, `CheckoutTest.CheckoutBranch_NotFound`, `CheckoutTest.Checkout_RefusesOverwriteModifiedTrackedFile`, `CheckoutTest.Checkout_RefusesOverwriteUntrackedCollision`, `CheckoutTest.CheckoutDetached_*`, `CheckoutTest.Checkout_BranchPrecedenceOverCommitPrefix` |
| `REQ-DIF-1`–`REQ-DIF-3` | Myers line diff, unified `---`/`+++`/`@@` headers, `Binary files differ` | UT/FT | `DiffTest.MyersIdenticalAndEmptyBoundaries`, `DiffTest.MyersReplacementAndUnifiedHunkHeaders`, `DiffTest.BinaryFilesDifferNotice`, `DiffTest.CmdDiffWorkingTreeVsIndex` |
| `REQ-CFG-1`–`REQ-CFG-3` | Config get/set, missing identity blocks commit, bad key exits `2` | FT | `CliTest.Cli_Config`, `AddStatusCommitTest.ConfigValidationAndEmptyCommitRejection` |
| `REQ-ERR-1`–`REQ-ERR-4` | Subdir discovery / outside-repo error, usage exit `2` on bad flags/args, integrity exit `3`, operational exit `1` | FT/ST | `CliTest.Cli_OutsideRepo`, `CliTest.Cli_FindsRepoFromSubdirectory`, `CliTest.Cli_NoArgs`, `CliTest.Cli_Help`, `CliTest.Cli_UnknownCommand`, `CliTest.Cli_MalformedFlagsAndExtraArgsExit2`, `CliTest.Cli_OperationFailureExits1`, `DiffTest.CorruptIndexReturnsExitCode3`, `SystemE2ETest.FullWorkflowAndIntegrityExitCode3` |
| `RR-1`, `RR-2`, `RR-3` | Atomic crash-safe writes, object read integrity, exclusive `.lock` rejection | ST/FT/UT | `SystemE2ETest.FaultInjection_SigkillPreservesConsistentRepoState`, `RefsTest.WriteHead_LockExistsFails`, `RefsTest.WriteRef_LockExistsFails`, `IndexTest.ExistingLockFileBlocksWrite`, `ObjectStoreTest.AtomicWriteLeavesNoTempFiles` |
| `PO-1`, `MNT-3`, `SR-1` | POSIX `/` path separators, versioned binary index (`v1`), immutable `0444` deduplication | UT/IT/IN | `IndexTest.NormalizesSeparatorsToPosix`, `IndexTest.BinaryFormatRoundTripAndSorting`, `IndexTest.UnsupportedVersionThrows`, `ObjectStoreTest.DeduplicationDoesNotOverwrite` |

---

## 4. Software Quality Assurance (SQA) Verification Summary

| Quality Attribute | Assessment Basis | Verified Evidence |
|---|---|---|
| **Reliability** | Atomic updates (`O_CREAT \| O_EXCL` + `fsync` + `rename`), lock conflict rejection, fail-closed integrity | `SystemE2ETest.FaultInjection_SigkillPreservesConsistentRepoState` terminates child writer with `SIGKILL` mid-commit and confirms `.bts/index`, `HEAD`, commit, and tree objects deserialize cleanly; `test_refs` & `test_index` verify lock rejection |
| **Maintainability** | Modular CMake targets (`bts_lib`, `bts_storage`, `bts_objects`), clean `-Wall -Wextra` compile, versioned `"BTSI"` schema | Documented in [architecture.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/architecture.md) & [maintenance.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/maintenance.md); `IndexTest.UnsupportedVersionThrows` enforces `MNT-3` |
| **Security & Integrity** | OpenSSL `EVP_sha256()` digest checks on all object/index reads, `0444` read-only objects, zero shell delegation | `ObjectStoreTest.CorruptObjectThrowsIntegrityException`, `DiffTest.CorruptIndexReturnsExitCode3`, `AddStatusCommitTest.CorruptObjectYieldsExitCode3WithHash`, `SystemE2ETest.FullWorkflowAndIntegrityExitCode3` |
| **Usability** | Actionable `stderr` messages, strict exit contract (`0..3`), per-command `--help`, checkout data-loss refusal | `CliTest.Cli_MalformedFlagsAndExtraArgsExit2`, `CheckoutTest.Checkout_RefusesOverwrite*`, `CheckoutTest.CheckoutDetached_AmbiguousPrefixListsCandidates` |
| **Performance** | `lstat` cache short-circuiting (`0` re-hashes on clean trees) and zlib level-1 compression | Release 3-trial benchmark: `PR-1` median `0.232 s` (max `0.244 s`, target `< 3.0 s`), `PR-2` median `40.3 ms` (max `40.6 ms`, target `< 200 ms`) |

---

## 5. Observed CTest Execution Results (`Release`)

Executed via `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure` (GoogleTest 1.18.0, OpenSSL 3.5.9, ZLIB 1.2.12):

| CTest Entries | Suite / Group | GoogleTest Cases | Status | Observed Duration |
|---|---|---:|---|---:|
| `#1` | `test_refs` | 17 | Passed | 0.03 sec |
| `#2` | `test_branch` | 10 | Passed | 0.03 sec |
| `#3` | `test_checkout` | 9 | Passed | 0.04 sec |
| `#4` | `test_log` | 8 | Passed | 0.02 sec |
| `#5` | `test_cli` | 11 | Passed | 0.01 sec |
| `#6` | `test_index` | 7 | Passed | 0.01 sec |
| `#7` | `test_diff` | 6 | Passed | 0.01 sec |
| `#8` | `test_add_status_commit` | 6 | Passed | 0.04 sec |
| `#9` | `test_system_e2e` | 2 | Passed | 0.03 sec |
| `#10`–`#27` | `storage_tests` (`HashTest.*`, `CompressTest.*`, `ObjectStoreTest.*`) | 18 | Passed | ~0.01 sec combined |
| `#28`–`#38` | `objects_tests` (`BlobTest.*`, `TreeTest.*`, `CommitTest.*`) | 11 | Passed | ~0.01 sec combined |

**Summary:** `100% tests passed out of 38` (`105` individual GoogleTest test cases passed, Total CTest wall time: `0.33 sec`).

---

## 6. Performance Benchmark Report (`PR-1`, `PR-2`)

> [!NOTE]
> **CTest / CMake Release Note:** `PR-1` and `PR-2` are executed outside CTest via [scripts/benchmark.py](../scripts/benchmark.py) against a Release binary built with `-DCMAKE_BUILD_TYPE=Release`:
> ```bash
> cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
> python3 scripts/benchmark.py build/bts --runs 3
> ```
> The script automatically detects OS, CPU, core count, RAM, and `CMAKE_BUILD_TYPE` from `CMakeCache.txt` (warning on `stderr` if non-Release), verifies `< 50 MB` payload size (`20,483,890` bytes) plus `bts log`/`bts status` post-conditions, warms the filesystem cache, and reports `min`/`median`/`max` across trials (`--no-warmup` and `--json` flags supported), failing if worst-case (`max`) exceeds SRS limits.

### Reference Machine & Build Profile (Auto-Reported)
- **Binary:** `build/bts` (`CMAKE_BUILD_TYPE=Release`)
- **CPU:** Apple M5 Pro (`arm64`, `15` cores)
- **RAM:** 48.0 GB (`51,539,607,552` bytes)
- **OS:** macOS 27.0 (Build `26A428`)
- **Methodology:** `3` measured trials after warmup, zlib compression level `1`, warm filesystem cache

### Benchmark Results (`Release` Build, 3 Measured Trials)

| Requirement | Scenario | SRS Target | Median | Min | Max (Worst Case) | Status |
|---|---|---|---|---|---|---|
| **PR-1** | Stage (`bts add .`) and commit (`bts commit`) 1,000 files (`19.53 MiB`) at zlib level 1 | `< 3.0 s` | **0.232 s** | 0.226 s | **0.244 s** (`12.3x` margin) | Pass |
| **PR-2** | `bts status` on a clean repository of 5,000 files using the `lstat` cache | `< 200 ms` | **40.3 ms** | 39.7 ms | **40.6 ms** (`4.9x` margin) | Pass |
