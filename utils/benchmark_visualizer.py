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
#CHANGE THE STUFF IN EXEPATH AND CON/SYS
import matplotlib.pyplot as plt
import math

# Default test directory (relative to script location)
if os.name == "nt":
    BUILD_DIR = Path(__file__).parent.parent / "build"
    lib_ext = ".dll"
    exe_name = "benchmarker.exe"
    DEBUG = "Debug"
else:
    BUILD_DIR = Path(__file__).parent.parent / "build_ninja"
    lib_ext = ".so"
    exe_name = "benchmarker"
    DEBUG = ""


# Helper to get test library extension for platform

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

def plot_bp(ax, data, labels, box_colors, xlabel, title):
    bp = ax.boxplot(data, patch_artist=True, vert=False)
    for patch, color in zip(bp['boxes'], box_colors):
        patch.set_facecolor(color)
        patch.set_edgecolor('black')
    ax.set_yticks(range(1, len(labels) + 1))
    ax.set_yticklabels(labels)
    ax.set_ylabel("Region Type")
    ax.set_xlabel(xlabel)
    ax.set_title(title)
    ax.set_xscale("log")
    min_val = min([min(vals) for vals in data if vals]) if data else 1
    max_val = max([max(vals) for vals in data if vals]) if data else 1
    left_lim = min_val * 0.8
    right_lim = max_val * 1.9
    ax.set_xlim(left=left_lim, right=right_lim)

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

    region_color_map = {"trace": colors[0], "arena": colors[1], "rc": colors[2]}
    avg_gc_us_data = [[(r[1] / r[2]) / 1e3 for r in results["runs"]] for results in all_results.values()]
    max_gc_us_data = [[r[3] / 1e3 for r in results["runs"]] for results in all_results.values()]
    avg_mem_kb_data = [[r[4] / 1024 for r in results["runs"]] for results in all_results.values()]
    max_mem_kb_data = [[r[5] / 1024 for r in results["runs"]] for results in all_results.values()]
    labels = [name for name in all_results.keys()]
    box_colors = [region_color_map.get(
        "trace" if "trace" in name else ("arena" if "arena" in name else ("rc" if "rc" in name else None)),
        colors[idx % len(colors)]
    ) for idx, (name, results) in enumerate(all_results.items())]
    print(avg_gc_us_data)
    plot_bp(
        axes[0, 0],
        avg_gc_us_data,
        labels,
        box_colors,
        "Avg GC Time (µs)",
        "Avg GC Time by Region Type (Boxplot)"
    )
    plot_bp(
        axes[0, 1],
        max_gc_us_data,
        labels,
        box_colors,
        "Max GC Time (µs)",
        "Max GC Time by Region Type (Boxplot)"
    )
    plot_bp(
        axes[1, 0],
        avg_mem_kb_data,
        labels,
        box_colors,
        "Avg Memory (KB)",
        "Avg Memory by Region Type (Boxplot)"
    )
    plot_bp(
        axes[1, 1],
        max_mem_kb_data,
        labels,
        box_colors,
        "Max Memory (KB)",
        "Max Memory by Region Type (Boxplot)"
    )

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
    axes[0, 2].axis("off")  # Hide the unused subplot

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
    first_name = list(all_results.values())[0]["name"]
    last_underscore = first_name.rfind("_")
    base_test_name = first_name[:last_underscore] if last_underscore != -1 else first_name
    fig.suptitle(f"GC Benchmark Comparison ({base_test_name})", fontsize=14, fontweight="bold")
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    print(f"Saved: {output_path}")
    plt.show()


if __name__ == "__main__":
    use_sys = "--sys" in sys.argv
    if use_sys:
        sys.argv.remove("--sys")
        TEST_DIR = BUILD_DIR / "test" / "benchmarks" / "sys" / DEBUG
    else:
        TEST_DIR = BUILD_DIR / "test" / "benchmarks" / "con" / DEBUG

    # Argument parsing: python benchmark_visualizer.py --csv <csvfile> | [runs] [warmup_runs] <test_name> [args...]
    args = sys.argv[1:]
    CSV_DIR = Path(__file__).parent.parent / "CSVs"

    # CSV mode: --csv must be the first argument
    if len(args) >= 2 and args[0] == '--csv':
        approx_name = args[1]
        print(f"Checking directory for CSV files: {CSV_DIR} (full path: {CSV_DIR.resolve()})")
        csv_files = list(CSV_DIR.glob(f"*{approx_name}*.csv"))
    
        if not csv_files:
            print(f"No CSV files found containing '{approx_name}' in {CSV_DIR}")
            sys.exit(1)
        # Only plot, skip all test running logic


    else:
        runs = "5"
        warmup_runs = "5"
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
            print("Usage: python benchmark_visualizer.py --csv <csvfile> | [runs] [warmup_runs] <test_name> [args...]" )
            print("       python benchmark_visualizer.py [runs] [warmup_runs] <test_name> --sys [args...]" )
            sys.exit(1)
        # Delete any existing CSV files first
        for old_csv in TEST_DIR.glob("*.csv"):
            old_csv.unlink()

        # List available test libraries for info (optional, can be used for validation or listing)
        test_libs = list(TEST_DIR.glob(f"benchmarks-con-*{lib_ext}"))

        # Construct the test library filename
        test_lib_name = f"benchmarks-con-{test_name}{lib_ext}"
        test_lib_path = TEST_DIR / test_lib_name
        if not test_lib_path.exists():
            print(f"Error: Test library not found: {test_lib_path}")
            print("Available test libraries:")
            for tlib in test_libs:
                print(f"  {tlib.name}")
            sys.exit(1)

        # Run benchmarker.exe with the test library as argument
        exe = BUILD_DIR / "src" / "benchmarker" / DEBUG / exe_name
        if not exe.exists():
            print(f"Error: benchmarker_main not found in {exe.parent}")
            sys.exit(1)
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
        # Search CSV_DIR for all CSV files containing the test name
        print(f"Checking directory for CSV files: {CSV_DIR} (full path: {CSV_DIR.resolve()})")
        csv_files = list(CSV_DIR.glob(f"*{test_name}*.csv"))
        if not csv_files:
            print(f"No CSV files found for test '{test_name}' in {CSV_DIR}")
            sys.exit(1)
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

    output_file = CSV_DIR / "benchmark_comparison.png"
    plot(all_results, str(output_file))
