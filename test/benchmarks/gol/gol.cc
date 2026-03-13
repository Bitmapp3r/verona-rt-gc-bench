// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "gol.h"

#include "../../../src/benchmarker/export_macro.h"
#include "../benchmarker/benchmark_main_helper.h"

#include <debug/harness.h>
#include <test/opt.h>
#include <util/gc_benchmark.h>

BENCHMARK_WINDOWS_CALLBACK_BRIDGE()

MAKE_REGION_WRAPPER(test, gol::run_test);

extern "C" BENCHMARK_EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  enable_benchmark_logging(opt);
  RegionType rt = parse_region_type(opt);

  size_t seed = opt.is<size_t>("--seed", 42);
  UNUSED(seed);
  int generations = opt.is<int>("--generations", 10);
  int size = opt.is<int>("--size", 8);

  // Print region type
  std::cout << "[gol.cc] Using region type: ";
  switch (rt)
  {
    case RegionType::Trace:
      std::cout << "Trace";
      break;
    case RegionType::Arena:
      std::cout << "Arena";
      break;
    case RegionType::Rc:
      std::cout << "Rc";
      break;
    case RegionType::SemiSpace:
      std::cout << "SemiSpace";
      break;
    default:
      std::cout << "Unknown";
      break;
  }
  std::cout << std::endl;

  // Print arguments
  std::cout << "[gol.cc] Arguments:" << std::endl;
  for (int i = 0; i < argc; ++i)
  {
    std::cout << "  argv[" << i << "]: " << argv[i] << std::endl;
  }
  std::cout << "[gol.cc] Parsed options:" << std::endl;
  std::cout << "  --seed: " << seed << std::endl;
  std::cout << "  --generations: " << generations << std::endl;
  std::cout << "  --size: " << size << std::endl;

  DISPATCH_REGION(rt, test, size, generations);
  return 0;
}

RUN_BENCHMARK_MAIN()