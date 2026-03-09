# Benchmarking Verona GC

This folder contains GC benchmarking tests for the Verona runtime.

## Building with Benchmarking Measurement

To enable GC benchmarking measurement (timing, memory tracking, callbacks), configure your build with:

```
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DENABLE_BENCHMARKING=ON
```

> **Note:** The `ENABLE_BENCHMARKING` flag is only meaningful for tests in the `benchmarks` folder. Enabling it for other tests or executables will only add measurement overhead and does not provide useful results. All benchmarks are built with this flag enabled by default; you do not need to set it unless you want to experiment with it in non-benchmark code.

## Running Benchmarks

Benchmarks are built as shared libraries or executables.

### Using the benchmarker tool

Use the `benchmarker` tool to run a benchmark library:

```
./src/benchmarker/benchmarker --runs 5 --warmup_runs 5 test/benchmarks/<file-name>/con-library/<file-name>
```

### Running benchmark executables

All benchmarks are also built with standalone executables. Run them directly:

```
./test/benchmarks/<file-name>/con-executable/<file-name>
```

## CSV Visualization

To visualize benchmark results from CSV files, run the visualizer from the repository root:

```
python utils/benchmark_visualizer.py --csv <file-name>
```

Example:

```
python utils/benchmark_visualizer.py --csv benchmarks-con-gol_lib
```

## Notes

- Run the `benchmarker` tool from your build directory (e.g., `build_ninja`).
- Run the CSV visualizer from the repository root.
