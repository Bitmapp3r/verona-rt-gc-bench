#!/usr/bin/env python3
"""
GC Benchmark Visualizer - runs a test and visualizes the auto-generated CSV files

Usage:
  python benchmark_visualizer.py <test_name> [args...]
  python benchmark_visualizer.py <test_name> --sys [args...]

Examples:
  python benchmark_visualizer.py gol
  python benchmark_visualizer.py reproduction --seed 42
  python benchmark_visualizer.py bag --sys
"""

import os
import subprocess
import sys
from pathlib import Path

import matplotlib.pyplot as plt

# Default test directory (relative to script location)
SCRIPT_DIR = Path(__file__).parent.parent
if os.name == "nt":
    BUILD_DIR = SCRIPT_DIR / "build"
else:
    BUILD_DIR = SCRIPT_DIR / "build_ninja"
TEST_DIR = BUILD_DIR / "test" / "benchmarks"  # / "test" / "benchmarks"


# Helper to get test library extension for platform
def get_test_lib_extension():
    if os.name == "nt":
        return ".dll"
    else:
        return ".so"


def get_benchmarker_main():
    """Return the path to benchmarker_main executable in the build/src/benchmarker directory."""
    exe_name = "benchmarker.exe" if os.name == "nt" else "benchmarker"
    exe_path = SCRIPT_DIR / "build_ninja" / "src" / "benchmarker" / exe_name
    if not exe_path.exists():
        print(f"Error: benchmarker_main not found in {exe_path.parent}")
        sys.exit(1)
    return exe_path


def parse_csv(filepath):
    """Parse a benchmark CSV file."""
    results = {"name": Path(filepath).stem, "runs": [], "p50": 0, "p99": 0, "jitter": 0}

    with open(filepath, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("run,"):
                continue
            if line.startswith("#"):
                parts = dict(p.split("=") for p in line[1:].split(",") if "=" in p)
                results["p50"] = int(parts.get("p50_ns", 0))
                results["p99"] = int(parts.get("p99_ns", 0))
                results["jitter"] = float(parts.get("jitter", 0))
            else:
                parts = line.split(",")
                if len(parts) >= 7:
                    results["runs"].append(
                        (
                            int(parts[0]),
                            int(parts[1]),
                            int(parts[2]),
                            int(parts[3]),
                            int(parts[4]),
                            int(parts[5]),
                            int(parts[6]),
                        )
                    )
    return results


def plot(all_results, output_path):
    """Generate a 2x3 plot comparing all region types."""
    if not all_results:
        print("No data to plot")
        return

    fig, axes = plt.subplots(2, 3, figsize=(14, 8))

    # Colors for different region types
    colors = ["#3498db", "#e74c3c", "#2ecc71", "#9b59b6", "#f39c12"]

    num_types = len(all_results)
    width = 0.8 / num_types  # Bar width based on number of types

    # Get run numbers from first result (assume all have same runs)
    first_result = list(all_results.values())[0]
    runs = [r[0] for r in first_result["runs"]]
    x = range(len(runs))

    # Plot each metric with all region types
    for idx, (name, results) in enumerate(all_results.items()):
        offset = (idx - num_types / 2 + 0.5) * width
        color = colors[idx % len(colors)]

        avg_gc_us = [
            r[1] / r[2] / 1e3 if r[2] > 0 else 0 for r in results["runs"]
        ]  # avg per call in µs
        max_us = [r[3] / 1e3 for r in results["runs"]]
        avg_mem_kb = [r[4] / 1024 for r in results["runs"]]
        peak_mem_kb = [r[5] / 1024 for r in results["runs"]]

        # Row 0: Avg GC Time, Max GC Time
        axes[0, 0].bar(
            [i + offset for i in x], avg_gc_us, width, label=name, color=color
        )
        axes[0, 1].bar([i + offset for i in x], max_us, width, label=name, color=color)

        # Row 1: Avg Memory, Peak Memory
        axes[1, 0].bar(
            [i + offset for i in x], avg_mem_kb, width, label=name, color=color
        )
        axes[1, 1].bar(
            [i + offset for i in x], peak_mem_kb, width, label=name, color=color
        )

    # Configure axes - Row 0
    axes[0, 0].set_xlabel("Run")
    axes[0, 0].set_ylabel("Avg GC Time (µs)")
    axes[0, 0].set_title("Average GC Time per Run")
    axes[0, 0].set_xticks(x)
    axes[0, 0].set_xticklabels(runs)
    axes[0, 0].set_yscale("log")
    axes[0, 0].legend()

    axes[0, 1].set_xlabel("Run")
    axes[0, 1].set_ylabel("Max GC Time (µs)")
    axes[0, 1].set_title("Max GC Time per Run")
    axes[0, 1].set_xticks(x)
    axes[0, 1].set_xticklabels(runs)
    axes[0, 1].set_yscale("log")
    axes[0, 1].legend()

    # Remove the third graph in row 0 (was GC Calls)
    axes[0, 2].axis("off")

    # Configure axes - Row 1
    axes[1, 0].set_xlabel("Run")
    axes[1, 0].set_ylabel("Avg Memory (KB)")
    axes[1, 0].set_title("Average Memory per Run")
    axes[1, 0].set_xticks(x)
    axes[1, 0].set_xticklabels(runs)
    axes[1, 0].legend()

    axes[1, 1].set_xlabel("Run")
    axes[1, 1].set_ylabel("Peak Memory (KB)")
    axes[1, 1].set_title("Peak Memory per Run")
    axes[1, 1].set_xticks(x)
    axes[1, 1].set_xticklabels(runs)
    axes[1, 1].legend()

    # Latency percentiles: P50 and P99 on x-axis, region types as bars
    names = list(all_results.keys())
    p50_values = [r["p50"] / 1e3 for r in all_results.values()]
    p99_values = [r["p99"] / 1e3 for r in all_results.values()]

    x_lat = range(2)  # P50, P99
    width_lat = 0.8 / num_types

    for idx, (name, results) in enumerate(all_results.items()):
        offset = (idx - num_types / 2 + 0.5) * width_lat
        color = colors[idx % len(colors)]
        values = [results["p50"] / 1e3, results["p99"] / 1e3]
        axes[1, 2].bar(
            [i + offset for i in x_lat], values, width_lat, label=name, color=color
        )

    axes[1, 2].set_ylabel("Latency (µs)")
    axes[1, 2].set_title("Latency Percentiles")
    axes[1, 2].set_xticks(x_lat)
    axes[1, 2].set_xticklabels(["P50", "P99"])
    axes[1, 2].set_yscale("log")
    axes[1, 2].legend()

    # Add jitter info as text
    jitter_text = "\n".join(
        [f"{name}: {r['jitter'] * 100:.1f}%" for name, r in all_results.items()]
    )
    axes[1, 2].text(
        0.98,
        0.98,
        f"Jitter:\n{jitter_text}",
        transform=axes[1, 2].transAxes,
        ha="right",
        va="top",
        fontsize=9,
        bbox=dict(boxstyle="round", facecolor="wheat", alpha=0.5),
    )

    fig.suptitle("GC Benchmark Comparison", fontsize=14, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    print(f"Saved: {output_path}")
    plt.show()


if __name__ == "__main__":
    use_sys = "--sys" in sys.argv
    if use_sys:
        sys.argv.remove("--sys")

    # Argument parsing: python benchmark_visualizer.py [runs] [warmup_runs] <test_name|csv_file> [args...]
    args = sys.argv[1:]
    runs = "1"
    warmup_runs = "1"
    test_name = None
    extra_args = []

    # Parse runs and warmup_runs if present (must be integers)
    if len(args) >= 3 and args[0].isdigit() and args[1].isdigit():
        runs = args[0]
        warmup_runs = args[1]
        test_name = args[2]
        extra_args = args[3:]
    elif len(args) >= 2 and args[0].isdigit():
        runs = args[0]
        warmup_runs = "0"
        test_name = args[1]
        extra_args = args[2:]
    elif len(args) >= 1:
        test_name = args[0]
        extra_args = args[1:]
    else:
        print(
            "Usage: python benchmark_visualizer.py [runs] [warmup_runs] <test_name|csv_file> [args...]"
        )
        print(
            "       python benchmark_visualizer.py [runs] [warmup_runs] <test_name> --sys [args...]"
        )
        sys.exit(1)

    # If the argument is a CSV file, skip running the test and just plot
    csv_path = Path(test_name)
    if csv_path.suffix == ".csv" and csv_path.exists():
        csv_files = [csv_path]
        TEST_DIR_USED = csv_path.parent
    else:
        exe = get_benchmarker_main()
        # Delete any existing CSV files first
        for old_csv in TEST_DIR.glob("*.csv"):
            old_csv.unlink()

        # List available test libraries for info (optional, can be used for validation or listing)
        test_lib_ext = get_test_lib_extension()
        test_libs = list(TEST_DIR.glob(f"benchmarks-con-*{test_lib_ext}"))

        # Construct the test library filename
        test_lib_name = f"libbenchmarks-con-{test_name}{test_lib_ext}"
        test_lib_path = TEST_DIR / test_lib_name
        if not test_lib_path.exists():
            print(f"Error: Test library not found: {test_lib_path}")
            print("Available test libraries:")
            for tlib in test_libs:
                print(f"  {tlib.name}")
            sys.exit(1)

        # Run benchmarker.exe with the test library as argument
        cmd = [
            str(exe),
            f"--runs",
            runs,
            f"--warmup_runs",
            warmup_runs,
            str(test_lib_path),
        ] + extra_args
        print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True)
        print(result.stdout)
        if result.stderr:
            print(result.stderr)

        # Find all CSV files created in the CSVs directory
        CSV_DIR = BUILD_DIR / "CSVs"
        csv_files = list(CSV_DIR.glob("*.csv"))
        TEST_DIR_USED = CSV_DIR
        if not csv_files:
            print(
                "No CSV files found. Make sure the test uses GCBenchmark::print_summary()"
            )
            print("Available test libraries:")
            for tlib in test_libs:
                print(f"  {tlib.name}")
            sys.exit(1)

    print(f"\nFound {len(csv_files)} CSV file(s)")

    # Parse all CSVs and combine into one plot
    all_results = {}
    label_counts = {}
    for csv_file in sorted(csv_files):
        results = parse_csv(csv_file)
        stem = Path(csv_file).stem.lower()

        # Detect region type from filename
        if "trace" in stem:
            base = "trace"
        elif "arena" in stem:
            base = "arena"
        elif "rc" in stem:
            base = "rc"
        else:
            base = Path(csv_file).stem

        # Ensure unique keys if multiple files map to same region type
        label_counts[base] = label_counts.get(base, 0) + 1
        name = base if label_counts[base] == 1 else f"{base}_{label_counts[base]}"
        all_results[name] = results

    output_file = TEST_DIR_USED / "benchmark_comparison.png"
    plot(all_results, str(output_file))
