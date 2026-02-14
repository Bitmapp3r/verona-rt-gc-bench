// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "reproduction.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  size_t runs = 3;
  size_t warmup_runs = 10;
  size_t seed =
    opt.is<size_t>("--seed", 42); // Default 0 = random seed each run

  std::cout << "Running with trace region" << std::endl;
  GCBenchmark trace_benchmark;
  trace_benchmark.run_benchmark(
    [seed]() { reproduction::run_test<RegionType::Trace>(101, 50, 10, seed); },
    runs,
    warmup_runs);
  trace_benchmark.print_summary("Reproduction - Trace Region");

  std::cout << "\nRunning with rc region" << std::endl;
  GCBenchmark rc_benchmark;
  rc_benchmark.run_benchmark(
    [seed]() { reproduction::run_test<RegionType::Rc>(101, 50, 10, seed); },
    runs,
    warmup_runs);
  rc_benchmark.print_summary("Reproduction - RC Region");

  std::cout << "\nRunning with arena region" << std::endl;
  GCBenchmark arena_benchmark;
  arena_benchmark.run_benchmark(
    [seed]() { reproduction::run_test<RegionType::Arena>(101, 50, 10, seed); },
    runs,
    warmup_runs);
  arena_benchmark.print_summary("Reproduction - Arena Region");

  return 0;
}