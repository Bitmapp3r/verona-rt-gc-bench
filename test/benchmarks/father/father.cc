// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "father.h"

#include "../../../src/benchmarker/export_macro.h"
#include "../benchmarker/benchmark_main_helper.h"
#include "region/region_base.h"

#include <cstddef>
#include <debug/harness.h>
#include <iostream>
#include <test/opt.h>
#include <util/gc_benchmark.h>
#include <string>
using namespace verona::rt::api;

MAKE_REGION_WRAPPER(test, father::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  std::cout << "Father benchmark - mixed workload test\n\n";

  // Parse benchmark parameters
  size_t iterations = opt.is<size_t>("--iterations", 5);
  size_t num_regions = opt.is<size_t>("--num-regions", 1);

  std::vector<std::pair<std::string, std::vector<std::string>>> workloads;
  
  std::string pointer_churn_with_concurrencyPath = "test/benchmarks/pointer_churn_with_concurrency/con-library/";
  #ifdef PLATFORM_WINDOWS
  pointer_churn_with_concurrencyPath += "Debug/";
  #endif
  std::string pointerChurnLib = buildLibraryPath(
      pointer_churn_with_concurrencyPath,
      "benchmarks-con-pointer_churn_lib_with_concurrency"
  );
  
  workloads.push_back({
      pointerChurnLib,
      {pointerChurnLib, "-n", "10000", "--regions", "30", "--iterations", "100", "--m", "10000"}
  });
  
  std::string golPath = "test/benchmarks/gol/con-library/";
  #ifdef PLATFORM_WINDOWS
  golPath += "Debug/";
  #endif
  std::string golLib = buildLibraryPath(
      golPath,
      "benchmarks-con-gol_lib"
  );
  
  workloads.push_back({
      golLib,
      {golLib, "--arena", "--generations", "10000"}
  });
  
  DISPATCH_REGION(rt, test, workloads, num_regions, iterations);

  return 0;
}

RUN_BENCHMARK_MAIN()
