# BTS-Version-Control

[![C++](https://img.shields.io/badge/C++-blue.svg)](https://isocpp.org/)
[![Academic Project](https://img.shields.io/badge/Course-Software_Engineering-success)](#)

**BTS-Version-Control** is a lightweight, minimal, content-addressable version control system built from scratch. Inspired by the internal architecture of Git, `bts` was developed to explore the mechanics of directed acyclic graphs (DAGs), cryptographic hashing, and low-level filesystem management.

This project is submitted as part of the **Software Engineering** course at the **Indian Institute of Information Technology, Allahabad**.

---

## 📂 Repository Structure

```text
bts-version-control/
├── docs/
│   ├── SRS.md                    # IEEE Std 830 requirements specification
│   ├── requirement-analysis.md   # Stakeholder analysis & use cases (UC-1..UC-5)
│   ├── architecture.md           # Layer architecture, ADRs, SQA & UML diagrams
│   ├── test-plan-and-report.md   # Test plan, traceability & benchmark report
│   ├── project-plan.md           # Milestones, RACI matrix & risk register
│   ├── maintenance.md            # ISO/IEC 14764 post-deployment maintenance guide
│   └── commands.md               # Cross-platform build, test & CLI reference
├── scripts/
│   └── benchmark.py              # PR-1 / PR-2 Release benchmark runner
├── src/                          # Source implementation
├── tests/                        # Unit, integration, E2E & fault-injection tests
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## 🚀 Core Features

`bts` implements the foundational mechanics of a local version control system:
* **Content-Addressable Storage:** Cryptographic SHA-256 hashing and zlib compression for all objects.
* **Staging Area (Index):** Binary cache tracking workspace modifications with `stat` metadata optimization.
* **Immutable Snapshots:** Tree and Commit objects forming a chronological history DAG.
* **Branching & Checkout:** Lightweight branch pointers and detached `HEAD` state management.
* **Unified Diffing:** Myers/LCS algorithm for line-by-line file comparison.
* **Crash Resilience:** Atomic POSIX file operations ensuring repository integrity during abrupt terminations.

Supported Commands: `init`, `config`, `add`, `status`, `commit`, `log`, `branch`, `checkout`, and `diff`.

---

## 🏗 System Architecture

The system is structured in a layered architecture separating CLI parsing from low-level filesystem I/O.

```text
Tier 1: CLI Interface (Argument Parsing & Command Routing)
Tier 2: Porcelain Commands (User-facing actions: commit, checkout, status)
Tier 3: Core VCS Engine
        ├── Index Manager (Binary staging serialization)
        ├── Object Model (Blob, Tree, Commit parsers)
        ├── Ref Manager (HEAD resolution, atomic locking)
        └── Diff Engine (Myers LCS Algorithm)
Tier 4: Storage & POSIX I/O (zlib, SHA-256, atomic renames)
```

### Data Model
All tracked data is stored in the hidden `.bts/` directory inside the project root:
* **Blobs**: Raw file data, compressed and hashed.
* **Trees**: Directory structures linking to blobs and sub-trees.
* **Commits**: Metadata snapshots linking to a root tree and parent commits.
* **Refs**: Text files pointing to specific commit hashes (e.g., `.bts/refs/heads/main`).

---

## 👥 Team Responsibilities (BTS)

This project was collaboratively designed and developed by:

* **[Bharat](https://github.com/your-username)** 
  * *Storage Engine & Object Model*: Implementation of SHA-256 hashing, zlib compression, and serialization/deserialization of Blob, Tree, and Commit objects.
* **[Tejasv Lata](https://github.com/your-username)** 
  * *Staging Index & Diff Engine*: Binary index parsing, `stat` cache optimization, `status` command logic, and line-by-line unified diffing.
* **[Sarthak](https://github.com/your-username)** 
  * *Reference Manager & CLI/Porcelain*: Branch creation/deletion, atomic checkout routines, commit DAG traversal (`log`), and main CLI argument dispatching.

*(Note: Roles represent primary ownership; all members contributed to system design, debugging, and testing).*

---

## ⚙️ Quickstart & Build Guide

### Prerequisites
* **C++17** compatible compiler (`g++` or `clang++`)
* **CMake** (v3.16+)
* **OpenSSL** (for SHA-256 hashing)
* **zlib** (for object compression)
* **GoogleTest** (for unit testing)

### Build, Test & Benchmark Instructions

Clone the repository and build a Release binary with CMake:

**macOS / Linux / Windows (WSL or MSYS2 MinGW64):**

```bash
git clone https://github.com/your-org/bts-version-control.git
cd bts-version-control

# Configure & compile (Release build)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8

# Run all 38 CTest targets (105 GoogleTest cases)
ctest --test-dir build --output-on-failure

# Run PR-1 / PR-2 performance benchmarks
python3 scripts/benchmark.py build/bts
```

*(For Windows PowerShell multi-config syntax and full CLI/recovery commands, see [docs/commands.md](docs/commands.md).)*

### Basic Usage

*(Tip: run `export PATH="$(pwd)/build:$PATH"` in your shell or add it to `~/.zshrc` so you can invoke `bts` directly from anywhere; see [docs/commands.md](docs/commands.md#optional-run-bts-globally-add-build-to-path) for Linux/Windows `PATH` setup.)*

```bash
# Initialize a new repository
./build/bts init

# Configure user identity
./build/bts config user.name "Your Name"
./build/bts config user.email "your.email@example.com"

# Create a file, stage, check status, and commit
echo "Hello World" > hello.txt
./build/bts add hello.txt
./build/bts status
./build/bts commit -m "Initial commit"

# Inspect history and branches
./build/bts log --oneline
./build/bts branch
```

---

## 📚 Documentation

Detailed software engineering artifacts and references live in `docs/`:
* [Software Requirements Specification (SRS)](docs/SRS.md)
* [Requirement Analysis & Use Cases](docs/requirement-analysis.md)
* [Architecture, Design Decisions & UML Models](docs/architecture.md)
* [Test Plan, SQA & Benchmark Report](docs/test-plan-and-report.md)
* [Project Plan, RACI & Risk Register](docs/project-plan.md)
* [Software Maintenance Plan](docs/maintenance.md)
* [Command Reference (macOS, Linux, Windows)](docs/commands.md)

---
*Created for the Software Engineering Course (Nov 2026).*