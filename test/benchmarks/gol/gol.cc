// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "gol.h"

#include "gol_rc.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  // Parses `--seed` option with default of 0
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed); // Not used for now

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  std::cout << "Running with trace region" << std::endl;
  GCBenchmark trace_benchmark;
  size_t runs = 10;
  size_t warmup_runs = 10;
  trace_benchmark.run_benchmark([]() { gol::run_test(); }, runs, warmup_runs);
  trace_benchmark.print_summary("Game of Life - Trace Region");

  std::cout << "\nRunning with rc region" << std::endl;
  GCBenchmark rc_benchmark;
  rc_benchmark.run_benchmark([]() { gol_rc::run_test(); }, runs, warmup_runs);
  rc_benchmark.print_summary("Game of Life - RC Region");

  return 0;
}

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

// Linux shares symbols by default with RTLD_GLOBAL, but Windows DLLs need explicit bridging
using namespace verona::rt::api::internal;

#if defined(_WIN32) || defined(_WIN64)
extern "C" EXPORT void set_gc_callback(void (*callback)(uint64_t, verona::rt::RegionType, size_t, size_t))
{
  static std::function<void(uint64_t, verona::rt::RegionType, size_t, size_t)> func;
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

extern "C" EXPORT int run_benchmark(int argc, char** argv)
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

  gol::run_test(size, generations);
  return 0;
}
