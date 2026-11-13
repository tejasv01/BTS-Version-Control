# Software Maintenance & Post-Deployment Evolution Plan

**Project:** BTS Version Control System  
**Authors:** Bharat, Tejasv Lata, Sarthak  
**Baseline:** [SRS.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/SRS.md) (`MNT-1`, `MNT-2`, `MNT-3`, `PO-1`, `PO-2`, `RR-1..3`, `SR-1`)

---

## 1. Maintenance Strategy Overview (ISO/IEC 14764)

BTS is structured around strict separation between CLI parsing, command orchestration, domain logic (index, diff, refs), and cryptographic/compressed persistence so post-deployment maintenance remains localized and testable without cross-module regressions. This plan organizes ongoing lifecycle operations across the four ISO/IEC 14764 maintenance categories: **Corrective**, **Adaptive**, **Perfective**, and **Preventive**.

---

## 2. Corrective Maintenance (Field Diagnostics, Recovery & Defect Fixes)

### 2.1 Deterministic Exit Code Triage Playbook

All CLI failures funnel through `bts::cli_main` in [cli.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/commands/cli.cpp):

| Exit Code | Meaning | Typical Trigger | Diagnostic & Recovery Action |
| :---: | :--- | :--- | :--- |
| **`0`** | Success | Normal execution or `--help` on valid subcommand | None required. |
| **`1`** | Operational / Domain Failure | Outside repo, missing file on `add`, unborn `log`, missing `user.name`, dirty overwrite block on `checkout`, active `.lock` file | Inspect `stderr` message; satisfy precondition (e.g., `bts config user.name`, commit local changes, or follow Section 2.2 lock recovery). |
| **`2`** | CLI Syntax / Usage Violation | No command, unknown subcommand, unknown flag, extra positional arguments (`REQ-ERR-2`) | Correct command syntax per `bts <cmd> --help`. |
| **`3`** | Cryptographic / Integrity Corruption | Object SHA-256 mismatch (`IntegrityException`), corrupt zlib stream, or `.bts/index` trailer mismatch / bad `"BTSI"` header (`IndexIntegrityException`) | Treat `.bts/` as compromised or hardware-corrupted (Section 2.3). Do **not** ignore or bypass verification (`SR-1`). |

### 2.2 Crash & Stale Lock Recovery Procedure (`RR-1`, `RR-3`)

Atomic mutations (`write_index` in [index.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/index.cpp), `write_ref` in [refs.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/refs.cpp)) acquire an exclusive POSIX `O_CREAT | O_EXCL` lock file (`index.lock`, `HEAD.lock`, or `refs/heads/<name>.lock`), write to it, invoke `fsync`, and atomically `rename()` over the live file.

If the operating system terminates a `bts` process abruptly (`SIGKILL` / power loss) while a lock is held:
1. Confirm no active `bts` process is running against the repository (`ps aux | grep bts`).
2. Inspect `.bts/index` or `.bts/refs/heads/<branch>` — because `rename()` never occurs unless `fsync()` succeeded, the pre-crash target file remains intact and valid (verified by `SystemE2ETest.FaultInjection_SigkillPreservesConsistentRepoState`).
3. Remove the orphaned lock file (`rm .bts/index.lock` or `rm .bts/HEAD.lock`).
4. Run `bts status` to verify the staging index deserializes cleanly (exit `0`, not `3`).

### 2.3 Integrity Incident Diagnosis (`Exit Code 3`)

When exit code `3` occurs (`REQ-ERR-3`, `RR-2`):
1. Identify the corrupted artifact from `stderr` (either `"index"` header/checksum mismatch from `read_index` or object hash `<64-hex>` mismatch from `read_object`).
2. Because loose objects are stored with read-only permissions (`0444`), accidental mutation indicates filesystem corruption, external editing, or malicious tampering.
3. Restore the affected object from a verified backup or re-stage unaffected working tree blobs (`bts add <file>` only writes missing objects — if an existing object is corrupt on disk, remove the corrupt loose file after auditing working-tree integrity and re-hash).

### 2.4 Regression-First Bug Fix Protocol

Every corrective patch follows three mandatory steps:
1. Write a failing GoogleTest case reproducing the bug (in `tests/test_<module>.cpp` or [test_system_e2e.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/tests/test_system_e2e.cpp)).
2. Implement the minimal fix within the owning module.
3. Verify zero compiler warnings (`-Wall -Wextra`) and 100% pass across all CTest suites before release.

---

## 3. Adaptive Maintenance (Platform, Dependency & Schema Evolution)

### 3.1 Operating System & Toolchain Portability (`PO-1`, `PO-2`)

- **Current Target Platforms:** macOS (`arm64` / Clang C++17) and Linux (GCC 9+ / Clang C++17).
- **Platform-Coupled Touchpoints:**
  - `::lstat`, `S_ISLNK`, `::chmod(..., 0444 / 0755)`, `O_CREAT | O_EXCL`, `::fsync` in [index.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/index.cpp), [checkout.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/commands/checkout.cpp), [refs.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/refs.cpp), and [object_store_stub.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/object_store_stub.cpp).
  - `fork()`/`kill(SIGKILL)` fault-injection test in [test_system_e2e.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/tests/test_system_e2e.cpp) (guarded by `#ifndef _WIN32`).
- **Windows / Future POSIX Adaptation Strategy:** Abstract `lstat`/`fsync`/`O_CREAT|O_EXCL` atomic-replace primitives behind a single `src/include/bts/platform.h` helper rather than scattering `#ifdef _WIN32` conditionals across command handlers.
- **Third-Party Libraries:** [CMakeLists.txt](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/CMakeLists.txt) pins C++17 (`CMAKE_CXX_STANDARD 17`) and discovers `OpenSSL::Crypto` (`EVP_sha256()`), `ZLIB::ZLIB`, and `GTest` via standard `find_package`, with Homebrew OpenSSL prefix support on Apple platforms.

### 3.2 Forward-Compatible Binary Index Schema Evolution (`MNT-3`)

The `.bts/index` file header (`Appendix B` of [SRS.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/SRS.md)) begins with 4-byte magic `"BTSI"`, 4-byte big-endian `version` (`uint32_t`, currently `1`), and 4-byte `entry_count`.

To extend `IndexEntry` in future releases (for example, adding nanosecond `mtime_nsec`, `dev`/`ino` fields, or assume-unchanged flags):
1. Keep the `v1` reader path intact in `read_index()` so existing repositories remain readable without manual migration.
2. Increment the version constant to `2` for newly written indexes and dispatch on `version` after header validation.
3. Preserve the trailing 32-byte raw SHA-256 footer over `[0 .. payload_end)` so corruption detection (`IndexIntegrityException`) covers all future versions uniformly.

---

## 4. Perfective Maintenance (Refactoring & Performance Optimization)

### 4.1 Consolidating Dual Object-Store Layers (Tracked Technical Debt)

During parallel sprint integration, two compatible storage interfaces evolved in the repository:
- The **class-based storage/object API** (`bts::storage::ObjectStore` in [src/storage/object_store.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/storage/object_store.cpp) and `bts::objects::{Blob, Tree, Commit}` in [src/objects/](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/objects)), verified by `storage_tests` and `objects_tests`.
- The **free-function core wrapper** (`bts::write_object`, `bts::read_object`, `bts::write_tree`, `bts::write_commit` in [src/core/object_store_stub.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/object_store_stub.cpp)) used by `index.cpp` and `checkout.cpp`. Both share the exact underlying `src/storage/hash.cpp` and `src/storage/compress.cpp` primitives and identical Appendix A on-disk layouts.

**Planned Perfective Consolidation:**
1. Delegate `write_object` / `read_object` in `object_store_stub.cpp` (renamed `object_store_facade.cpp`) directly to `bts::storage::ObjectStore`, eliminating duplicate zlib header formatting code (~80 LOC).
2. Delegate `write_tree` / `parse_tree` / `write_commit` / `parse_commit` to `bts::objects::Tree` and `bts::objects::Commit`.
3. Re-run all 38 CTest entries (`test_*`, `storage_tests`, `objects_tests`) and [benchmark.py](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/scripts/benchmark.py) to confirm behavioral equivalence and unchanged latency.

### 4.2 Scalability Enhancements

- **Parallel Hashing on `bts add`:** Partition large working-tree file batches across worker threads (`std::thread::hardware_concurrency`) for `SHA-256` + zlib compression, while writing to `.bts/objects/` atomically and sorting index entries identically on the main thread (`PR-1`).
- **Packfile Storage (Post-v0.3):** Add a fallback lookup in `read_object()` from loose `.bts/objects/<2>/<62>` to immutable `.bts/objects/pack/*.pack` + binary fan-out `.idx` once repository object counts exceed ~10,000 objects.
- **Performance Regression Guard:** Run `python3 scripts/benchmark.py build/bts` against Release builds (`-DCMAKE_BUILD_TYPE=Release`) after any storage, index, or tree-builder modification to verify `PR-1 < 3.0 s` and `PR-2 < 200 ms`.

---

## 5. Preventive Maintenance & Modular Extension Playbook (`MNT-2`)

BTS’s layered architecture ensures new subcommands or flags can be added cleanly in 4 deterministic steps without modifying storage internals.

### Case Study A: Adding `bts diff --cached` (Index vs HEAD)
1. **CLI Dispatch ([cli.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/commands/cli.cpp)):** Accept `--cached` on the `diff` branch of `cli_main` (still rejecting unknown flags with exit `2`) and forward `bool cached` to `cmd_diff(repo, cached)`.
2. **Core Diff ([diff.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/diff.cpp)):** Reuse `collect_tree_files` / `parse_commit` + `read_index` to compare `HEAD` tree entries against staged `IndexEntry` hashes using the existing `diff_contents()` Myers engine.
3. **Tests:** Add cases to [test_diff.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/tests/test_diff.cpp) and [test_cli.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/tests/test_cli.cpp) covering staged additions, modifications, and unborn `HEAD`.
4. **Docs:** Append new requirement row and verification entry in [SRS.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/SRS.md) and [test-plan-and-report.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/test-plan-and-report.md).

### Case Study B: Adding Lightweight Tags (`bts tag <name>`)
1. **Storage/Ref Layer ([refs.cpp](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/src/core/refs.cpp)):** Reuse `validate_branch_name()`, `write_ref(repo, "refs/tags/" + name, hash)`, and `read_ref()` atomic locking (`RR-1`).
2. **Command Layer:** Add `src/commands/tag.cpp` + `src/include/bts/tag.h`, register in `bts_lib` in [CMakeLists.txt](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/CMakeLists.txt), and dispatch from `cli_main`.
3. **Resolution Priority:** Extend `cmd_checkout()` to resolve `refs/tags/<target>` alongside `refs/heads/<target>` and commit hashes without modifying index or object serialization.

### Release Maintenance Checklist
Before tagging any post-deployment release:
- [ ] Configure clean Release build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- [ ] Run full automated test suite (`ctest --test-dir build --output-on-failure`) — 100% pass expected
- [ ] Run Release performance validation (`python3 scripts/benchmark.py build/bts`) — confirm `PR-1 < 3.0 s` and `PR-2 < 200 ms`
- [ ] Audit `git diff` against [SRS.md](file:///Users/tejasv/Code/SE/Project/BTS-Version-Control/docs/SRS.md) Appendices A–C for on-disk format stability (`MNT-3`)
