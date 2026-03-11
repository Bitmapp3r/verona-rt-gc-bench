// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "grid_walkers.h"

#include "../../../src/benchmarker/export_macro.h"
#include "../benchmarker/benchmark_main_helper.h"
#include "util/gc_benchmark.h"

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  // Parse command-line arguments
  int gridsize = opt.is<int>("-gridsize", 40);
  int numsteps = opt.is<int>("-steps", 20);
  int numwalkers = opt.is<int>("-walkers", 10);

  DISPATCH_REGION(rt, test, gridsize, numsteps, numwalkers);

  return 0;
}

RUN_BENCHMARK_MAIN()
