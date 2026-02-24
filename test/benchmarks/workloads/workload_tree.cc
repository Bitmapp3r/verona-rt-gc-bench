// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "workload_tree.h"
#include "region/region_base.h"

#include <benchmark_main.h>

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

extern "C" EXPORT int run_benchmark(RegionType rt, int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed);

  run_test_with_region(rt, [&]<RegionType R>() {
    return workload_tree::run_test<R>(10, 10);
  });
  return 0;
}
