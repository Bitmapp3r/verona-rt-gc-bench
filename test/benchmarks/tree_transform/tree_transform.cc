// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "tree_transform.h"

#include <debug/harness.h>
#include <test/opt.h>
#include "../../../src/benchmarker/export_macro.h"
#include "../benchmarker/benchmark_main_helper.h"

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, tree_transform::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  // Parse command-line arguments
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed);
  int depth = opt.is<int>("-d", 10);
  int transforms = opt.is<int>("-t", 5);

  DISPATCH_REGION(rt, test, depth, transforms);
  
  return 0;
}

int main(int argc, char** argv)
{
  run_benchmark(argc, argv);
  return 0;
}
