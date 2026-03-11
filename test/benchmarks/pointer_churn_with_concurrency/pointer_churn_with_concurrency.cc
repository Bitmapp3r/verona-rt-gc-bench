// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include "pointer_churn_with_concurrency.h"

#include "../../../src/benchmarker/export_macro.h"
#include "../benchmarker/benchmark_main_helper.h"

#include <debug/harness.h>
#include <test/opt.h>

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, pointer_churn_with_concurrency::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  // Parse command-line arguments
  // Fixed seed in test for reproducibility
  size_t seed = opt.is<size_t>("--seed", 12345);
  size_t num_nodes = opt.is<size_t>("-n", 500);
  size_t num_mutations = opt.is<size_t>("-m", 1000);
  size_t num_regions = opt.is<size_t>("--regions", 10);
  size_t iterations = opt.is<size_t>("--iterations", 50);

  DISPATCH_REGION(
    rt, test, num_nodes, num_mutations, seed, num_regions, iterations);

  return 0;
}

RUN_BENCHMARK_MAIN()