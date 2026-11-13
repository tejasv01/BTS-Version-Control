#!/usr/bin/env python3
"""Benchmark PR-1 and PR-2 performance targets from docs/SRS.md."""

import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any, Dict, List, Optional, Tuple


def _read_sysctl(key: str) -> Optional[str]:
    try:
        out = subprocess.check_output(
            ["sysctl", "-n", key], text=True, stderr=subprocess.DEVNULL
        ).strip()
        return out or None
    except Exception:
        return None


def _detect_cmake_build_type(bts_bin: str) -> str:
    """Inspect CMakeCache.txt alongside (or above) `bts_bin` if present."""
    cur = os.path.dirname(bts_bin)
    for _ in range(3):
        cache = os.path.join(cur, "CMakeCache.txt")
        if os.path.isfile(cache):
            try:
                with open(cache, "r", encoding="utf-8", errors="replace") as f:
                    for line in f:
                        if line.startswith("CMAKE_BUILD_TYPE:"):
                            val = line.split("=", 1)[1].strip()
                            return val if val else "Unspecified (Unoptimized)"
            except OSError:
                break
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    return "Unknown (CMakeCache.txt not found)"


def collect_machine_info(bts_bin: str) -> Dict[str, Any]:
    """Gather reference machine specs and CMake build configuration."""
    system = platform.system()
    os_desc = f"{system} {platform.release()}"
    if system == "Darwin":
        ver = platform.mac_ver()[0]
        build = _read_sysctl("kern.osversion")
        if ver:
            os_desc = f"macOS {ver}" + (f" (Build {build})" if build else "")
    elif system == "Linux" and os.path.isfile("/etc/os-release"):
        try:
            with open("/etc/os-release", "r", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("PRETTY_NAME="):
                        os_desc = line.split("=", 1)[1].strip().strip('"')
                        break
        except OSError:
            pass

    cpu = None
    if system == "Darwin":
        cpu = _read_sysctl("machdep.cpu.brand_string")
    elif system == "Linux" and os.path.isfile("/proc/cpuinfo"):
        try:
            with open("/proc/cpuinfo", "r", encoding="utf-8") as f:
                for line in f:
                    if line.lower().startswith("model name"):
                        cpu = line.split(":", 1)[1].strip()
                        break
        except OSError:
            pass
    if not cpu:
        cpu = platform.processor() or platform.machine()

    ram_bytes: Optional[int] = None
    if system == "Darwin":
        raw_mem = _read_sysctl("hw.memsize")
        if raw_mem and raw_mem.isdigit():
            ram_bytes = int(raw_mem)
    elif hasattr(os, "sysconf"):
        try:
            pages = os.sysconf("SC_PHYS_PAGES")
            page_size = os.sysconf("SC_PAGE_SIZE")
            if isinstance(pages, int) and isinstance(page_size, int):
                ram_bytes = pages * page_size
        except (ValueError, OSError):
            pass

    ram_gb = f"{ram_bytes / (1024 ** 3):.1f} GB" if ram_bytes else "Unknown"

    return {
        "os": os_desc,
        "arch": platform.machine(),
        "cpu": cpu,
        "cores": os.cpu_count() or 0,
        "ram_bytes": ram_bytes,
        "ram_human": ram_gb,
        "build_type": _detect_cmake_build_type(bts_bin),
        "binary": bts_bin,
    }


def _init_repo(bts_bin: str, cwd: str) -> None:
    subprocess.run([bts_bin, "init"], cwd=cwd, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([bts_bin, "config", "user.name", "Bench"], cwd=cwd, check=True)
    subprocess.run(
        [bts_bin, "config", "user.email", "bench@example.com"], cwd=cwd, check=True
    )


def run_pr1_once(bts_bin: str) -> Tuple[float, int]:
    """PR-1: Stage and commit 1,000 files (~20 MB < 50 MB) at zlib level 1."""
    with tempfile.TemporaryDirectory() as d:
        _init_repo(bts_bin, d)

        payload = b"0123456789abcdef" * 1280  # 20,480 bytes base per file
        total_bytes = 0
        for i in range(1000):
            sub = os.path.join(d, f"dir_{i // 100}")
            os.makedirs(sub, exist_ok=True)
            data = payload + f"{i}\n".encode()
            total_bytes += len(data)
            with open(os.path.join(sub, f"f_{i}.txt"), "wb") as f:
                f.write(data)

        if total_bytes >= 50 * 1024 * 1024:
            raise AssertionError(
                f"PR-1 fixture exceeded 50 MB SRS bound: {total_bytes} bytes"
            )

        t0 = time.perf_counter()
        subprocess.run([bts_bin, "add", "."], cwd=d, check=True)
        subprocess.run(
            [bts_bin, "commit", "-m", "PR-1 bench"],
            cwd=d,
            check=True,
            stdout=subprocess.DEVNULL,
        )
        elapsed_sec = time.perf_counter() - t0

        # Verify repository state reflects a clean, recorded commit.
        log_out = subprocess.check_output(
            [bts_bin, "log", "--oneline"], cwd=d, text=True
        )
        if "PR-1 bench" not in log_out:
            raise AssertionError("PR-1 post-condition failed: commit missing in log")

        st_out = subprocess.check_output([bts_bin, "status"], cwd=d, text=True)
        if "nothing to commit, working tree clean" not in st_out:
            raise AssertionError(
                f"PR-1 post-condition failed: repo not clean after commit:\n{st_out}"
            )

        return elapsed_sec, total_bytes


def run_pr2_trials(bts_bin: str, runs: int, warmup: bool) -> List[float]:
    """PR-2: Measure `bts status` (ms) across `runs` trials on 5,000 clean files."""
    with tempfile.TemporaryDirectory() as d:
        _init_repo(bts_bin, d)

        for i in range(5000):
            sub = os.path.join(d, f"d_{i // 100}")
            os.makedirs(sub, exist_ok=True)
            with open(os.path.join(sub, f"f_{i}.txt"), "wb") as f:
                f.write(f"file {i}\n".encode())

        subprocess.run([bts_bin, "add", "."], cwd=d, check=True)
        subprocess.run(
            [bts_bin, "commit", "-m", "PR-2 bench"],
            cwd=d,
            check=True,
            stdout=subprocess.DEVNULL,
        )

        if warmup:
            res = subprocess.run(
                [bts_bin, "status"], cwd=d, check=True, capture_output=True, text=True
            )
            if "nothing to commit, working tree clean" not in res.stdout:
                raise AssertionError(
                    f"PR-2 precondition failed: expected clean tree:\n{res.stdout}"
                )

        samples_ms: List[float] = []
        for _ in range(runs):
            t0 = time.perf_counter()
            res = subprocess.run(
                [bts_bin, "status"],
                cwd=d,
                check=True,
                capture_output=True,
                text=True,
            )
            elapsed_ms = (time.perf_counter() - t0) * 1000.0
            if "nothing to commit, working tree clean" not in res.stdout:
                raise AssertionError(
                    f"PR-2 post-condition failed: expected clean status:\n{res.stdout}"
                )
            samples_ms.append(elapsed_ms)

        return samples_ms


def _summarize(values: List[float]) -> Dict[str, Any]:
    return {
        "samples": values,
        "min": min(values),
        "median": statistics.median(values),
        "max": max(values),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark PR-1 and PR-2 performance requirements from docs/SRS.md."
    )
    parser.add_argument(
        "binary",
        nargs="?",
        default=os.path.join("build", "bts"),
        help="Path to compiled bts executable (default: build/bts)",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=3,
        help="Number of measured trials per benchmark (default: 3)",
    )
    parser.add_argument(
        "--no-warmup",
        action="store_true",
        help="Skip initial unmeasured warmup runs",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON summary to stdout",
    )
    args = parser.parse_args()

    if args.runs < 1:
        print("error: --runs must be >= 1", file=sys.stderr)
        return 2

    bts_bin = os.path.abspath(args.binary)
    if not os.path.isfile(bts_bin):
        print(f"error: bts binary not found at {bts_bin}", file=sys.stderr)
        return 1

    warmup = not args.no_warmup
    info = collect_machine_info(bts_bin)

    if warmup:
        run_pr1_once(bts_bin)

    pr1_samples: List[float] = []
    pr1_bytes = 0
    for _ in range(args.runs):
        sec, total_bytes = run_pr1_once(bts_bin)
        pr1_samples.append(sec)
        pr1_bytes = total_bytes

    pr2_samples_ms = run_pr2_trials(bts_bin, args.runs, warmup=warmup)

    pr1_stats = _summarize(pr1_samples)
    pr2_stats = _summarize(pr2_samples_ms)
    pr1_pass = pr1_stats["max"] < 3.0
    pr2_pass = pr2_stats["max"] < 200.0
    all_pass = pr1_pass and pr2_pass

    if args.json:
        report = {
            "machine": info,
            "runs": args.runs,
            "warmup": warmup,
            "pr1": {
                "files": 1000,
                "total_bytes": pr1_bytes,
                "target_sec": 3.0,
                "pass": pr1_pass,
                **pr1_stats,
            },
            "pr2": {
                "files": 5000,
                "target_ms": 200.0,
                "pass": pr2_pass,
                **pr2_stats,
            },
            "pass": all_pass,
        }
        print(json.dumps(report, indent=2))
        return 0 if all_pass else 1

    print("Reference Machine & Build Configuration")
    print(f"  Binary:     {info['binary']}")
    print(f"  Build Type: {info['build_type']}")
    print(f"  CPU:        {info['cpu']} ({info['arch']}, {info['cores']} cores)")
    print(f"  RAM:        {info['ram_human']} ({info['ram_bytes']} bytes)")
    print(f"  OS:         {info['os']}")
    print(f"  Trials:     {args.runs} measured (warmup={'on' if warmup else 'off'})")
    if "Release" not in info["build_type"] and "RelWithDebInfo" not in info["build_type"]:
        print(
            "  WARNING:    Non-Release CMake build detected; pass "
            "-DCMAKE_BUILD_TYPE=Release for accurate PR-1/PR-2 benchmarking.",
            file=sys.stderr,
        )

    pr1_mb = pr1_bytes / (1024 * 1024)
    print(
        f"\nPR-1 (1,000 files, {pr1_mb:.2f} MiB add+commit, target < 3.0 s): "
        f"median={pr1_stats['median']:.3f} s | "
        f"min={pr1_stats['min']:.3f} s | max={pr1_stats['max']:.3f} s "
        f"[{'PASS' if pr1_pass else 'FAIL'}]"
    )
    print(
        f"  Samples (s): {', '.join(f'{v:.3f}' for v in pr1_samples)}"
    )
    print(
        f"PR-2 (5,000 clean files status, target < 200 ms):              "
        f"median={pr2_stats['median']:.1f} ms | "
        f"min={pr2_stats['min']:.1f} ms | max={pr2_stats['max']:.1f} ms "
        f"[{'PASS' if pr2_pass else 'FAIL'}]"
    )
    print(
        f"  Samples (ms): {', '.join(f'{v:.1f}' for v in pr2_samples_ms)}"
    )

    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
