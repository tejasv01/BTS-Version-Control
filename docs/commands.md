# Command Reference

Quick reference for building, testing, benchmarking, operating, and troubleshooting **BTS-Version-Control** across **macOS**, **Linux**, and **Windows**.

---

## 1. CMake Build Commands

### macOS & Linux

```bash
# If Homebrew cmake/ctest is not already in PATH (macOS Apple Silicon):
export PATH="/opt/homebrew/bin:$PATH"

# Recommended optimized Release build (used for benchmarks and production runs):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8

# Default unoptimized build:
cmake -S . -B build && cmake --build build
```

### Windows

> [!NOTE]
> The current prototype uses POSIX filesystem primitives (`<unistd.h>`, `<fcntl.h>`, `::lstat`, `::fsync`, `O_CREAT | O_EXCL`) in [index.cpp](../src/core/index.cpp), [checkout.cpp](../src/commands/checkout.cpp), and [refs.cpp](../src/core/refs.cpp). On Windows, build inside **WSL** or **MSYS2 (MinGW64)**; native MSVC requires implementing the Win32 wrapper layer described in [maintenance.md](maintenance.md#L48-L55).

**Windows — WSL (Ubuntu/Debian) or MSYS2 MinGW64:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
```

**Windows — PowerShell (CMake Multi-Config Generator syntax, once Win32 shims are enabled):**

```powershell
cmake -S . -B build
cmake --build build --config Release -j 8
```

---

## 2. Automated Testing (CTest & GoogleTest)

### macOS / Linux / Windows (WSL or MSYS2)

```bash
# Run the full CTest suite (38 CTest entries / 105 GoogleTest cases):
ctest --test-dir build --output-on-failure

# Verbose test execution output:
ctest --test-dir build --verbose

# List individual GoogleTest cases inside a compiled test binary:
./build/test_system_e2e --gtest_list_tests
./build/test_checkout --gtest_list_tests
./build/tests/storage/storage_tests --gtest_list_tests
./build/tests/objects/objects_tests --gtest_list_tests
```

### Windows (PowerShell Multi-Config)

```powershell
ctest --test-dir build -C Release --output-on-failure
.\build\Release\test_system_e2e.exe --gtest_list_tests
```

---

## 3. Performance Benchmarking (`PR-1` & `PR-2`)

Run against a **Release** build using [scripts/benchmark.py](../scripts/benchmark.py):

```bash
# macOS / Linux / WSL / MSYS2:
python3 scripts/benchmark.py build/bts                 # 3 measured trials with warmup (default)
python3 scripts/benchmark.py build/bts --runs 5        # Custom trial count
python3 scripts/benchmark.py build/bts --no-warmup     # Skip initial warmup runs
python3 scripts/benchmark.py build/bts --runs 3 --json # Emit machine-readable JSON
```

```powershell
# Windows PowerShell:
python .\scripts\benchmark.py .\build\bts.exe --runs 3
# Or for multi-config Release output:
python .\scripts\benchmark.py .\build\Release\bts.exe --runs 3
```

---

## 4. `bts` CLI Workflow Commands

### Optional: Run `bts` Globally (Add `build/` to `PATH`)

Run from the repository root so every `cmake --build build` automatically updates your active `bts` command:

```bash
# Current terminal session (macOS / Linux / WSL):
export PATH="$(pwd)/build:$PATH"

# Permanently in zsh (macOS):
echo "export PATH=\"$(pwd)/build:\$PATH\"" >> ~/.zshrc
source ~/.zshrc

# Permanently in bash (Linux / WSL):
echo "export PATH=\"$(pwd)/build:\$PATH\"" >> ~/.bashrc
source ~/.bashrc
```

```powershell
# Current PowerShell session (Windows):
$env:Path = "$PWD\build;$env:Path"

# Permanently for current user (Windows PowerShell):
[Environment]::SetEnvironmentVariable("Path", "$PWD\build;" + [Environment]::GetEnvironmentVariable("Path", "User"), "User")
```

After adding `build/` to `PATH`, you can invoke `bts init`, `bts status`, `bts add .`, `bts commit -m "..."`, etc. directly from any directory.

---

### Subcommand Reference

Replace `./build/bts` with `bts` (if added to `PATH`), or `.\build\bts.exe` / `.\build\Release\bts.exe` on Windows PowerShell:

```bash
# Help
./build/bts --help
./build/bts <command> --help

# Initialize & configure repository
./build/bts init [directory]
./build/bts config user.name "<Name>"
./build/bts config user.email "<email>"
./build/bts config <key>

# Stage, inspect status, diff, and commit
./build/bts add <path>...        # e.g. ./build/bts add .
./build/bts status
./build/bts diff
./build/bts commit -m "<message>"

# History & branches
./build/bts log
./build/bts log --oneline
./build/bts branch
./build/bts branch <name>
./build/bts branch -d <name>
./build/bts checkout <branch|commit-prefix>   # min 4 hex chars for commit prefix
```

---

## 5. Diagnostics & Crash Recovery

### Exit Codes (`REQ-ERR-1..4`)

| Exit Code | Meaning | Shell Check (macOS / Linux / WSL) | PowerShell Check (Windows) |
| :---: | :--- | :--- | :--- |
| `0` | Success | `echo $?` | `$LASTEXITCODE` |
| `1` | Operational failure (missing path, dirty checkout blocked, unborn `log`, active `.lock`) | `echo $?` | `$LASTEXITCODE` |
| `2` | CLI syntax / unknown flag / wrong arity | `echo $?` | `$LASTEXITCODE` |
| `3` | SHA-256 or `"BTSI"` index integrity corruption | `echo $?` | `$LASTEXITCODE` |

### Stale Lock Cleanup After Abrupt Crash (`RR-3`)

Only remove `.lock` files after confirming no `bts` process is currently running:

```bash
# macOS / Linux / WSL:
rm .bts/index.lock
rm .bts/HEAD.lock
```

```powershell
# Windows PowerShell:
Remove-Item .bts\index.lock, .bts\HEAD.lock -ErrorAction SilentlyContinue
```
