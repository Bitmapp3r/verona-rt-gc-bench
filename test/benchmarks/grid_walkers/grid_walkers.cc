// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "grid_walkers.h"
#include "util/gc_benchmark.h"

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  test_walker(40, 20, 10);
  return 0;
}

extern "C" int run_benchmark(int argc, char** argv) {
    GCBenchmark trace_benchmark;
    SystematicTestHarness harness(argc, argv);
    size_t runs = 10;
    size_t warmup_runs = 10;
    trace_benchmark.run_benchmark(
      [&]() {
        harness.run(
          [&]() { test_walker(40, 20, 10); }
        );
      },
      runs,
      warmup_runs);

    trace_benchmark.print_summary("Arbitrary Nodes - Using Trace");

    opt::Opt opt(argc, argv);

    return 0;
}
