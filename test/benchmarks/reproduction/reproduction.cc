// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "reproduction.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>
#include "../benchmarker/benchmark_main_helper.h"
#include "region/region_base.h"

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

void run_test_RC(int a, int b, size_t c) ;

extern "C" EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  size_t seed =
    opt.is<size_t>("--seed", 42);
  
  RegionType rt = stringToRegionType(argv[0]);

  SystematicTestHarness harness(argc, argv);
  harness.run(run_test_RC, 20, 50, seed);
  //size_t cores = 6;
  //Scheduler& sched = Scheduler::get();
  //sched.init(cores);
  ///sched.set_fair(false);
  
  //reproduction::run_test<decltype(rt)>(101, 50, 1000,seed);
  //run_test_RC(101, 50, seed);
  //sched.run();
  //  test();
  puts("done");
  
  //dispatch(rt, [seed](auto regionType) {
   // reproduction::run_test<decltype(regionType)::value>(101, 50, seed);
  //});
  return 0;
}

void run_test_RC(int a, int b, size_t c) {
  reproduction::run_test<RegionType::Rc>(a, b, c);
}

int main(int argc, char **argv) {
    run_benchmark(argc, argv);
}