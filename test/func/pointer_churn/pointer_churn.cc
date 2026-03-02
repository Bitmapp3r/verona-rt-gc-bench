// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include "pointer_churn.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Parse command-line arguments
  // Fixed seed in test for reproducibility
  size_t seed = opt.is<size_t>("--seed", 12345);
  size_t num_nodes = opt.is<size_t>("-n", 12);
  size_t num_mutations = opt.is<size_t>("-m", 1000);

  // Parse GC type manually (opt::Opt::is doesn't support strings)
  std::string gc_type = "trace";
  for (int i = 1; i < argc; i++)
  {
    if (std::string(argv[i]) == "-g" && i + 1 < argc)
    {
      gc_type = argv[i + 1];
      break;
    }
  }

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
  {
    Logging::enable_logging();
    std::cout << "LOG WORKING\n";
  } else {
    std::cout << "LOG NOT WORKING\n";
  }
    

   std::cout << "Running with trace region" << std::endl;
  GCBenchmark trace_benchmark;
  size_t runs = 10;
  size_t warmup_runs = 10;
  trace_benchmark.run_benchmark([&]() { pointer_churn::run_test("trace", num_nodes, num_mutations, seed); }, runs, warmup_runs);
  trace_benchmark.print_summary("Pointer Churn - Trace Region");

  std::cout << "\nRunning with rc region" << std::endl;
  GCBenchmark rc_benchmark;
  rc_benchmark.run_benchmark([&]() { pointer_churn::run_test("rc", num_nodes, num_mutations, seed); }, runs, warmup_runs);
  rc_benchmark.print_summary("Pointer Churn - RC Region");

  std::cout << "\nRunning with arena region" << std::endl;
  GCBenchmark arena_benchmark;
  arena_benchmark.run_benchmark([&]() { pointer_churn::run_test("arena", num_nodes, num_mutations, seed); }, runs, warmup_runs);
  arena_benchmark.print_summary("Pointer Churn - Arena Region");

  return 0;
}