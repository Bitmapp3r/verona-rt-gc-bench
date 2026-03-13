# Verona Runtime - GC Benchmarking Suite

This is a fork of the [Project Verona Runtime](https://github.com/microsoft/verona-rt) focused on garbage collection research and benchmarking.

## Project Focus

This fork extends the original Verona runtime with:

1. **Comprehensive GC Benchmarking Suite**: A collection of benchmark tests designed to evaluate garbage collection performance across different region types (Trace, Arena, RC). You can get graphs by using the [visualiser](utils/benchmark_visualizer.py), showing the following metrics per region type across runs:
  - GC Calls
  - Avg GC Time (ns)
  - Max GC pause time (ns)
  - Wall clock time (ns) (WIP)
  - Avg/Peak Memory (bytes)
  - Peak Objects
  - P50 & P99 (ns)
  - Jitter
  - Avg/Peak Memory (bytes)
2. **Concurrent GC Implementation**: WIP
3. **Semi-space GC**: WIP

## Benchmarks

The benchmarking suite includes tests for various memory management scenarios:

- **Game of Life** (`gol`): Grid-based simulation with generational garbage
- **Arbitrary Nodes**: Graph traversal with edge removal
- **Partially Connected Nodes**: Graph mutation with pointer churn
- **Grid Walkers**: Multi-walker grid traversal with edge destruction
- **Pointer Churn**: Continuous graph mutation patterns
- **Reproduction**: Population simulation with generational cycles

These are all found in tests/benchmarks.

### Running Benchmarks 

#### Linux

```bash
./test/benchmarks/<test_name>/con-executable/benchmarks-con-<test_name>
```

Example:
```bash
./test/benchmarks/gol/con-executable/benchmarks-con-gol
```


#### Windows

```bash
.\test\benchmarks\<test_name>\con-executable\Release\benchmarks-con-<test_name>.exe
```

Example:
```bash
.\test\benchmarks\gol\con-executable\Release\benchmarks-con-gol.exe
```

Further documentation on the benchmarker can be found at [test/benchmarks/benchmarking.md](test/benchmarks/benchmarking.md)


### Running Benchmarks with Visualiser

```bash
cd build
python ../utils/benchmark_visualizer.py <test_name> [args...]
```

Example:
```bash
cd build
python ../utils/benchmark_visualizer.py gol
```

The visualizer automatically runs the benchmark and generates comparison plots showing GC performance across different region types.

Further information can be found in [utils/python_visualizer.md](utils/python_visualizer.md).

---

## Original Project Verona Runtime

Our notes on our findings on Verona whilst implementing this project can be found at [docs/verona_implementation_notes.md](docs/verona_implementation_notes.md).

This repository is based on the runtime for the [Verona](https://github.com/microsoft/verona) research project.
See the main [Verona](https://github.com/microsoft/verona) repo for more information.

## [Building](docs/building.md)

## [Contributing](CONTRIBUTING.md)

## [Internal Design](docs/internal)
