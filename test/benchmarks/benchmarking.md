
# Benchmarking Verona GC

This folder contains GC benchmarking tests for the Verona runtime.

## Building for Benchmarking

To enable GC benchmarking measurement (timing, memory tracking, callbacks), configure your build with:

```
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DENABLE_BENCHMARKING=ON
```

> **Note:** The `ENABLE_BENCHMARKING` flag is only meaningful for tests in the `benchmarks` folder. All benchmarks are built with this flag enabled by default; you do not need to set it unless you want to experiment with it in non-benchmark code.

## Benchmarking Setup

Programs designed for benchmarking are located in `test/benchmarks`. When the project is compiled, four directories are generated for each benchmark test:
- `con-executable` (Concurrent executable)
- `con-library` (Concurrent library files)
- `sys-executable` (Systematic executable)
- `sys-library` (Systematic library files)

*Note:* The library files (`.so` on Linux or `.dll` on Windows) are strictly necessary to run a test with benchmarking.

## Running Benchmarks

To execute a test and generate raw statistics:

```
<build_dir>/src/benchmarker/benchmarker --runs <n> --warmup_runs <m> <build_dir>/test/benchmarks/<benchmark_test>/<con-library/sys-library>/<test_file> <extra_parameters>
```

Or run the standalone executables:

```
<build_dir>/test/benchmarks/<benchmark_test>/con-executable/<test_file>
```

## Visualizing Benchmark Results

To visualize benchmark results from CSV files, run the visualizer from the repository root:

```
python utils/benchmark_visualizer.py --csv <folder_name>
```

Example:

```
python utils/benchmark_visualizer.py --csv benchmarks-con-gol_lib
```

Or to run benchmarks and generate graphs automatically:

```
python3 utils/benchmark_visualizer.py <runs> <warmup_runs> <test_name> [benchmarking_flags] [extra_parameters]
```

### Benchmarking Flags
- `--sys`: Runs the systematic (`sys`) version of the tests instead of the default concurrent (`con`).
- `--run_all`: Runs the workload with all different Garbage Collection (GC) types (trace, rc, arena, semispace). The visualizer ensures consistent seeds across all GC types when this flag is used.

## Notes
- Run the `benchmarker` tool from your build directory (e.g., `build_ninja`).
- Run the CSV visualizer from the repository root.
