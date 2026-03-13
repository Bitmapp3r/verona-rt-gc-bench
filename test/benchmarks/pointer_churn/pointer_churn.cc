// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include "pointer_churn.h"

#include <debug/harness.h>
#include <test/opt.h>
#include "../../../src/benchmarker/export_macro.h"
#include "../benchmarker/benchmark_main_helper.h"

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, pointer_churn::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  // Parse command-line arguments
  // Fixed seed in test for reproducibility
  size_t seed = opt.is<size_t>("--seed", 12345);
  size_t num_nodes = opt.is<size_t>("-n", 12);
  size_t num_mutations = opt.is<size_t>("-m", 1000);

  DISPATCH_REGION(rt, test, num_nodes, num_mutations, seed);

  return 0;
}

int main(int argc, char** argv)
{
  run_benchmark(argc, argv);
  return 0;
}
