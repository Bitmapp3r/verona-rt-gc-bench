// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "gol.h"

#include <benchmark_main.h>
#include "region/region_base.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

// Linux shares symbols by default with RTLD_GLOBAL, but Windows DLLs need
// explicit bridging
using namespace verona::rt::api::internal;

#if defined(_WIN32) || defined(_WIN64)
extern "C" EXPORT void set_gc_callback(
  void (*callback)(uint64_t, verona::rt::RegionType, size_t, size_t))
{
  static std::function<void(uint64_t, verona::rt::RegionType, size_t, size_t)>
    func;
  if (callback)
  {
    func = callback;
    RegionContext::set_gc_callback(&func);
  }
  else
  {
    RegionContext::set_gc_callback(nullptr);
  }
}
#endif

extern "C" EXPORT int run_benchmark(RegionType rt,int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Default values
  int size = 8;
  int generations = 10;

  // Parse command line arguments
  if (argc >= 2)
  {
    size = std::atoi(argv[1]);
  }
  if (argc >= 3)
  {
    generations = std::atoi(argv[2]);
  }

  run_test_with_region(rt, [&]<RegionType R>() {
    return gol::run_test<R>(size, generations);
  });
  
  return 0;
}
