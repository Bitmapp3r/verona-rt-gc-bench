// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "arbitrary_nodes.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

#include "../benchmarker/benchmark_main_helper.h"
#include "../../../src/benchmarker/export_macro.h"

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, arbitrary_nodes::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  int size = opt.is<int>("--size", 1010);
  int regions = opt.is<int>("--regions", 100);

  SystematicTestHarness harness(argc, argv);
  harness.run([&]() {
    DISPATCH_REGION(rt, test, size, regions);
  });
  return 0;
}

int main(int argc, char** argv)
{
  return run_benchmark(argc, argv);
}
