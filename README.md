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
│   ├── SRS.md                  # This IEEE Std 830 document
│   └── architecture.md         # Layer diagrams & modular specs
├── src/                        # Source implementation
├── tests/                      # Unit & integration test suites
├── Makefile / CMakeLists.txt
├── .gitignore                  # Ignore build artifacts, .bts/ test runs
└── README.md                   # Project overview, team members, quickstart
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

### Build Instructions
Clone the repository and build using CMake:

```bash
git clone https://github.com/your-org/bts-version-control.git
cd bts-version-control

# Create build directory and compile
mkdir build && cd build
cmake ..
make
```

### Basic Usage

After building, you can add the executable to your path or run it directly.

```bash
# Initialize a new repository
./bts init

# Configure user identity
./bts config user.name "Your Name"
./bts config user.email "your.email@example.com"

# Create a file, stage, and commit
echo "Hello World" > hello.txt
./bts add hello.txt
./bts commit -m "Initial commit"

# Check history
./bts log --oneline
```

---

## 📚 Documentation

Detailed software engineering artifacts, including the formal IEEE Std 830-1998 requirements specification, can be found in the `docs/` directory:
* [Software Requirements Specification (SRS)](docs/SRS.md)

---
*Created for the Software Engineering Course (Nov 2026).*