// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "reproduction.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>
#include "../benchmarker/benchmark_main_helper.h"
#include "region/region_base.h"
#include "../../../src/benchmarker/export_macro.h"

MAKE_REGION_WRAPPER(test, reproduction::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  size_t seed = opt.is<size_t>("--seed", 42);
  int generations = opt.is<int>("--generations", 101);
  int killPercent = opt.is<int>("--kill-percent", 50);
  int popSize     = opt.is<int>("--pop-size", 100);

  DISPATCH_REGION(rt, test, generations, killPercent, popSize, seed);
  return 0;
}

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  run_benchmark(argc, argv);
  return 0;
}
