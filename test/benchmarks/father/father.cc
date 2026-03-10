// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "father.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <iostream>
#include <util/gc_benchmark.h>
#include "../benchmarker/benchmark_main_helper.h"
#include "region/region_base.h"
#include "../../../src/benchmarker/export_macro.h"

using namespace verona::rt::api;

#if defined(_WIN32) || defined(_WIN64)
extern "C" BENCHMARK_EXPORT void set_gc_callback(void (*callback)(uint64_t, verona::rt::RegionType, size_t, size_t))
{
  static std::function<void(uint64_t, verona::rt::RegionType, size_t, size_t)> func;
  if (callback)
  {
    func = callback;
    verona::rt::set_gc_callback(&func);
  }
  else
  {
    verona::rt::set_gc_callback(nullptr);
  }
}
#endif

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  std::cout << "Father benchmark - mixed workload test\n\n";

  // Parse benchmark parameters
  size_t iterations = opt.is<size_t>("--iterations", 5);

  // Set up workloads: 1 GOL + 1 grid_walkers
  std::vector<std::pair<std::string, std::vector<std::string>>> workloads;

//   // Grid walkers workload
  // workloads.push_back({
  //   "test\\benchmarks\\grid_walkers\\con-library\\Debug\\benchmarks-con-grid_walkers_lib.dll",
  //   {}
  // });

  workloads.push_back({
    "test\\benchmarks\\pointer_churn\\con-library\\Debug\\benchmarks-con-pointer_churn_lib.dll",
    {"test\\benchmarks\\pointer_churn\\con-library\\Debug\\benchmarks-con-pointer_churn_lib.dll", "--arena", "--m", "1000"}
  });
  //Best Arena, Worst RC

  // GOL workload
  workloads.push_back({
    "test\\benchmarks\\gol\\con-library\\Debug\\benchmarks-con-gol_lib.dll",
    {".\test\benchmarks\gol\con-library\Debug\benchmarks-con-gol_lib.dll", "--arena", "--generations", "10"}
  });
  // Best RC/Arena, Worst Trace

  workloads.push_back({
    "test\\benchmarks\\reproduction\\con-library\\Debug\\benchmarks-con-reproduction_lib.dll",
    {"test\\benchmarks\\reproduction\\con-library\\Debug\\benchmarks-con-reproduction_lib.dll","--arena", "--generations", "300", "--killPercent", "50", "--popSize", "100"}
    //Best Trace, Worst Arena
  });

  size_t num_regions = 1 ;

  SystematicTestHarness harness(argc, argv);

  if (rt == RegionType::Trace)
  {
    harness.run([&]() {
      father::run_test<RegionType::Trace>(workloads, num_regions, iterations);
    });
  }
  else if (rt == RegionType::Rc)
  {
    harness.run([&]() {
      father::run_test<RegionType::Rc>(workloads, num_regions, iterations);
    });
  }
  else if (rt == RegionType::Arena)
  {
    harness.run([&]() {
      father::run_test<RegionType::Arena>(workloads, num_regions, iterations);
    });
  }

  return 0;
}

int main(int argc, char** argv)
{
  run_benchmark(argc, argv);
  return 0;
}
