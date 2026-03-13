# Python Visualizer Guide

This document explains how to use the Python script to automate running benchmarks and generating graphs. Graphs are automatically saved in the corresponding `CSVs` folder.

## Run and Generate Graphs

```
python3 utils/benchmark_visualiser.py <runs> <warmup_runs> <test_name> <benchmarking_flags> <extra_parameters>
```

### Benchmarking Flags
- `--sys`: Runs the systematic (`sys`) version of the tests instead of the default concurrent (`con`).
- `--run_all`: Runs the workload with all different Garbage Collection (GC) types (trace, rc, arena, semispace). The visualizer ensures consistent seeds across all GC types when this flag is used.

## Generate Graphs from Existing CSVs
If you already have the data in your `CSVs` folder and just want the graphs:

```
python3 utils/benchmark_visualiser.py --csv <folder_name>
```