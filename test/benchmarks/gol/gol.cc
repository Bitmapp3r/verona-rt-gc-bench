// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "gol.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

#include "../benchmarker/benchmark_main_helper.h"
#include "../../../src/benchmarker/export_macro.h"

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, gol::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  size_t seed = opt.is<size_t>("--seed", 42);
  UNUSED(seed);
  int generations = opt.is<int>("--generations", 10);
  int size = opt.is<int>("--size", 8);

  DISPATCH_REGION(rt, test, size, generations);
  return 0;
}

int main(int argc, char** argv)
{
  return run_benchmark(argc, argv);
}
