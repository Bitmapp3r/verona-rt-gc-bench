// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "workload_tree.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed);

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  size_t runs = 10;
  size_t warmup_runs = 10;

  std::cout << "Running with trace region" << std::endl;
  GCBenchmark trace_benchmark;
  trace_benchmark.run_benchmark(
    []() { workload_tree::run_test<RegionType::Trace>(); }, runs, warmup_runs);
  trace_benchmark.print_summary("Tree Transformation - Trace Region");

  std::cout << "\nRunning with rc region" << std::endl;
  GCBenchmark rc_benchmark;
  rc_benchmark.run_benchmark(
    []() { workload_tree::run_test<RegionType::Rc>(); }, runs, warmup_runs);
  rc_benchmark.print_summary("Tree Transformation - RC Region");

  std::cout << "Running with arena region" << std::endl;
  GCBenchmark arena_benchmark;
  arena_benchmark.run_benchmark(
    []() { workload_tree::run_test<RegionType::Arena>(); }, runs, warmup_runs);
  arena_benchmark.print_summary("Tree Transformation - Arena Region");

  return 0;
}
