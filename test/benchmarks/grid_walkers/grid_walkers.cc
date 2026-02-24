// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "grid_walkers.h"
#include "benchmark_main.h"
#include "region/region_base.h"

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

extern "C" EXPORT int run_benchmark(RegionType rt, int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Default values
  int gridsize = 40;
  int numsteps = 20;
  int numwalkers = 10;

  // Parse command line arguments
  if (argc >= 2)
  {
    gridsize = std::atoi(argv[1]);
  }
  if (argc >= 3)
  {
    numsteps = std::atoi(argv[2]);
  }
  if (argc >= 4)
  {
    numwalkers = std::atoi(argv[3]);
  }

  run_test_with_region(rt, [&]<RegionType R>() {
    return test_walker<R>(gridsize, numsteps, numwalkers);
  });
  return 0;
}
