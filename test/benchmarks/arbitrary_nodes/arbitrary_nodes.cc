// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "arbitrary_nodes.h"

#include "debug/logging.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

using namespace verona::rt::api;

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Default values
  int size = 1010;
  int regions = 100;
  bool enable_log = true;

  // Parse command line arguments
  if (argc >= 3)
  {
    size = std::atoi(argv[1]);
    regions = std::atoi(argv[2]);
  }

  if (argc >= 4)
  {
    std::string log_arg = argv[3];
    if (log_arg == "log")
    {
      enable_log = true;
    }
  }

  // Enable logging if requested
  if (enable_log)
  {
    Logging::enable_logging();
  }

  size_t runs = 10;
  size_t warmup_runs = 10;

  SystematicTestHarness harness(argc, argv);

  std::cout << "\nRunning with arena region" << std::endl;
  GCBenchmark trace_benchmark;

  trace_benchmark.run_benchmark(
    [&, size, regions]() {
      harness.run(
        [=]() { arbitrary_nodes::run_test<RegionType::Trace>(size, regions); });
    },
    runs,
    warmup_runs);
  trace_benchmark.print_summary("Arbitrary Nodes - Using Trace");

  trace_benchmark.run_benchmark(
    [&, size, regions]() {
      harness.run(
        [=]() { arbitrary_nodes::run_test<RegionType::Arena>(size, regions); });
    },
    runs,
    warmup_runs);
  trace_benchmark.print_summary("Arbitrary Nodes - Using Arena");

  return 0;
}

extern "C" int run_benchmark(int argc, char** argv) {
    opt::Opt opt(argc, argv);
  
    // Default values
    int size = 1010;
    int regions = 100;
    bool enable_log = true;
  
    // Parse command line arguments
    if (argc >= 3)
    {
      size = std::atoi(argv[1]);
      regions = std::atoi(argv[2]);
    }
  
    if (argc >= 4)
    {
      std::string log_arg = argv[3];
      if (log_arg == "log")
      {
        enable_log = true;
      }
    }
  
    // Enable logging if requested
    // if (enable_log)
    // {
    //   Logging::enable_logging();
    // }
    // 
    SystematicTestHarness harness(argc, argv);
    GCBenchmark trace_benchmark;
    GCBenchmark arena_benchmark;


    trace_benchmark.run_benchmark(
    [&, size, regions]() {
      harness.run(
        [&]() { arbitrary_nodes::run_test<RegionType::Trace>(size, regions); });
    },
    2,
    2);
    
    arena_benchmark.run_benchmark(
    [&, size, regions]() {
      harness.run(
        [&]() { arbitrary_nodes::run_test<RegionType::Arena>(size, regions); });
    },
    2,
    2);
    
    arena_benchmark.print_summary("Arbitrary Nodes - Using Arena");
    trace_benchmark.print_summary("Arbitrary Nodes - Using Trace");
    
    return 0;
}