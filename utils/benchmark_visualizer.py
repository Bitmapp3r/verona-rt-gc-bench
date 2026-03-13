#!/usr/bin/env python3
"""
GC Benchmark Visualizer - runs a test and visualizes the auto-generated CSV files

Usage:
    python benchmark_visualizer.py <test_name> [args...]
    python benchmark_visualizer.py <test_name> --sys [args...]
    python benchmark_visualizer.py <test_name> --run_all [args...]
    python benchmark_visualizer.py [runs] [warmup_runs] <test_name> [args...]
    python benchmark_visualizer.py --csv <folder_name>

Examples:
    python benchmark_visualizer.py gol
    python benchmark_visualizer.py reproduction --seed 42
    python benchmark_visualizer.py bag --sys
    python benchmark_visualizer.py bag --run_all
    python benchmark_visualizer.py 10 2 gol --sys --seed 42
    python benchmark_visualizer.py --csv benchmarks-con-gol_lib
"""

import math
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.stats import gaussian_kde

# CHANGE THE STUFF IN EXEPATH AND CON/SYS
import matplotlib.pyplot as plt

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
                    run_time_ns = int(parts[7]) if len(parts) >= 8 else 0
                    results["runs"].append(
                        (
                            int(parts[0]),
                            int(parts[1]),
                            int(parts[2]),
                            int(parts[3]),
                            int(parts[4]),
                            int(parts[5]),
                            int(parts[6]),
                            run_time_ns,
                        )
                    )
    return results


def plot_bp(ax, data, labels, box_colors, xlabel, title):
    bp = ax.boxplot(data, patch_artist=True, vert=False)
    for patch, color in zip(bp["boxes"], box_colors):
        patch.set_facecolor(color)
        patch.set_edgecolor("black")
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


def plot(all_results, output_path, test_name=None):
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
    avg_gc_us_data = [
        [(r[1] / r[2]) / 1e3 for r in results["runs"]]
        for results in all_results.values()
    ]
    max_gc_us_data = [
        [r[3] / 1e3 for r in results["runs"]] for results in all_results.values()
    ]
    avg_mem_kb_data = [
        [r[4] / 1024 for r in results["runs"]] for results in all_results.values()
    ]
    max_mem_kb_data = [
        [r[5] / 1024 for r in results["runs"]] for results in all_results.values()
    ]
    labels = [name for name in all_results.keys()]
    box_colors = [
        region_color_map.get(
            "trace"
            if "trace" in name
            else ("arena" if "arena" in name else ("rc" if "rc" in name else None)),
            colors[idx % len(colors)],
        )
        for idx, (name) in enumerate(all_results.keys())
    ]
    plot_bp(
        axes[0, 0],
        avg_gc_us_data,
        labels,
        box_colors,
        "Avg GC Time (µs)",
        "Avg GC Time by Region Type",
    )
    plot_bp(
        axes[0, 1],
        max_gc_us_data,
        labels,
        box_colors,
        "Max GC Time (µs)",
        "Max GC Time by Region Type",
    )
    plot_bp(
        axes[1, 0],
        avg_mem_kb_data,
        labels,
        box_colors,
        "Avg Memory (KB)",
        "Avg Memory by Region Type",
    )
    plot_bp(
        axes[1, 1],
        max_mem_kb_data,
        labels,
        box_colors,
        "Max Memory (KB)",
        "Max Memory by Region Type",
    )

    # GC latency distribution (bell curve / KDE on log-scale)
    # KDE is fitted on log-transformed data so distributions at different
    # magnitudes (e.g. arena ~0.1µs vs trace ~200µs) are all visible.
    for idx, (name, results) in enumerate(all_results.items()):
        gc_times = [(r[1] / r[2]) / 1e3 for r in results["runs"] if r[2] > 0]
        if len(gc_times) < 2:
            continue
        color = region_color_map.get(
            "trace" if "trace" in name else ("arena" if "arena" in name else ("rc" if "rc" in name else None)),
            colors[idx % len(colors)],
        )
        data = np.array(gc_times)
        data = data[data > 0]  # log requires positive values
        if len(data) < 2:
            continue
        log_data = np.log10(data)
        kde = gaussian_kde(log_data)
        # Evaluate over a padded range in log-space
        lo, hi = log_data.min() - 0.5, log_data.max() + 0.5
        log_range = np.linspace(lo, hi, 300)
        density = kde(log_range)
        # Trim where density < 1% of peak
        threshold = density.max() * 0.01
        above = np.where(density >= threshold)[0]
        log_range = log_range[above[0]:above[-1] + 1]
        density = density[above[0]:above[-1] + 1]
        x_range = 10 ** log_range
        axes[1, 2].plot(x_range, density, label=name, color=color, linewidth=1.5)
        axes[1, 2].fill_between(x_range, density, alpha=0.15, color=color)

    axes[1, 2].set_xscale("log")
    axes[1, 2].set_xlabel("Avg GC Time per Run (µs)")
    axes[1, 2].set_ylabel("Density (log-scale)")
    axes[1, 2].set_title("GC Latency Distribution")
    axes[1, 2].legend(fontsize=8, loc="upper left")

    # P50/P99/jitter info box (upper right, won't overlap legend on upper left)
    info_lines = []
    for name, r in all_results.items():
        p50 = r['p50'] / 1e3
        p99 = r['p99'] / 1e3
        jitter = r['jitter'] * 100
        info_lines.append(f"{name}: P50={p50:.1f}µs  P99={p99:.1f}µs  J={jitter:.1f}%")
    axes[1, 2].text(
        0.98,
        0.98,
        "\n".join(info_lines),
        transform=axes[1, 2].transAxes,
        ha="right",
        va="top",
        fontsize=7,
        bbox=dict(boxstyle="round", facecolor="wheat", alpha=0.5),
    )

    # Run time boxplot in the previously unused subplot
    run_time_ms_data = [
        [r[7] / 1e6 for r in results["runs"] if len(r) > 7 and r[7] > 0]
        for results in all_results.values()
    ]
    if any(run_time_ms_data):
        plot_bp(
            axes[0, 2],
            run_time_ms_data,
            labels,
            box_colors,
            "Run Time (ms)",
            "Total Run Time by Region Type",
        )
    else:
        axes[0, 2].axis("off")

    if test_name:
        display_name = test_name.replace("_", " ").replace("-", " ").title()
    else:
        first_name = list(all_results.values())[0]["name"]
        last_underscore = first_name.rfind("_")
        display_name = (
            first_name[:last_underscore] if last_underscore != -1 else first_name
        ).replace("_", " ").title()
    fig.suptitle(
        f"GC Benchmark: {display_name}", fontsize=14, fontweight="bold"
    )
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    print(f"Saved: {output_path}")
    plt.show()


if __name__ == "__main__":
    use_sys = "--sys" in sys.argv
    if use_sys:
        sys.argv.remove("--sys")

    run_all = "--run_all" in sys.argv
    if run_all:
        sys.argv.remove("--run_all")

    # Argument parsing: python benchmark_visualizer.py --csv <csvfile> | [runs] [warmup_runs] <test_name> [args...]
    args = sys.argv[1:]
    CSV_DIR = Path(__file__).parent.parent / "CSVs"
    test_name = None

    # CSV mode: --csv must be the first argument
    if len(args) >= 2 and args[0] == "--csv":
        folder_name = args[1]
        target_dir = CSV_DIR / folder_name
        print(f"Checking directory for CSV files: {target_dir} (full path: {target_dir.resolve()})")
        if not target_dir.exists() or not target_dir.is_dir():
            print(f"Error: Directory not found: {target_dir}")
            sys.exit(1)
        csv_files = list(target_dir.glob("*.csv"))
        if not csv_files:
            print(f"No CSV files found in directory '{target_dir}'")
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
            print(
                "Usage: python benchmark_visualizer.py --csv <csvfile> | [runs] [warmup_runs] <test_name> [args...]"
            )
            print(
                "       python benchmark_visualizer.py [runs] [warmup_runs] <test_name> --sys [args...]"
            )
            sys.exit(1)

        # Build the test directory path:
        # BUILD_DIR / test / benchmarks / <test_name> / (con-library|sys-library) / DEBUG
        lib_type = "sys-library" if use_sys else "con-library"
        prefix = "sys" if use_sys else "con"
        TEST_DIR = BUILD_DIR / "test" / "benchmarks" / test_name / lib_type / DEBUG

        # Delete any existing CSV files first
        if TEST_DIR.exists():
            for old_csv in TEST_DIR.glob("*.csv"):
                old_csv.unlink()

        # List available benchmarks for info
        benchmarks_dir = BUILD_DIR / "test" / "benchmarks"
        available_benchmarks = [d.name for d in benchmarks_dir.iterdir() if d.is_dir()] if benchmarks_dir.exists() else []

        # Construct the test library filename: benchmarks-con-<name>_lib.dll
        test_lib_name = f"benchmarks-{prefix}-{test_name}_lib{lib_ext}"
        test_lib_path = TEST_DIR / test_lib_name
        if not test_lib_path.exists():
            print(f"Error: Test library not found: {test_lib_path}")
            print("Available benchmarks:")
            for bname in available_benchmarks:
                print(f"  {bname}")
            sys.exit(1)

        # Run benchmarker.exe with the test library as argument
        exe = BUILD_DIR / "src" / "benchmarker" / DEBUG / exe_name
        if not exe.exists():
            print(f"Error: benchmarker_main not found in {exe.parent}")
            sys.exit(1)

        if run_all:
            gc_types = ["trace", "rc", "arena", "semispace"]
        else:
            gc_types = [None]

        for gc_type in gc_types:
            cmd = [
                str(exe),
                f"--runs",
                runs,
                f"--warmup_runs",
                warmup_runs,
                str(test_lib_path),
            ] + extra_args
            if gc_type:
                cmd += ["-g", gc_type]
            print(extra_args)
            print(f"Running: {' '.join(cmd)}")
            result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
            print(result.stdout)
            if result.stderr:
                print(result.stderr)

        # Find all CSV files created in the CSVs directory under a subdirectory named after the test
        csv_folder_name = test_lib_name.replace(lib_ext, "")
        target_dir = CSV_DIR / csv_folder_name
        print(f"Checking directory for CSV files: {target_dir} (full path: {target_dir.resolve()})")
        if not target_dir.exists() or not target_dir.is_dir():
            print(f"Error: Directory not found: {target_dir}")
            sys.exit(1)
        csv_files = list(target_dir.glob("*.csv"))
        if not csv_files:
            print(f"No CSV files found in directory '{target_dir}'")
            print("Available benchmarks:")
            for bname in available_benchmarks:
                print(f"  {bname}")
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

    # Determine test name for the plot title
    plot_test_name = test_name if test_name else None
    output_file = target_dir / "benchmark_comparison.png"
    plot(all_results, str(output_file), test_name=plot_test_name)