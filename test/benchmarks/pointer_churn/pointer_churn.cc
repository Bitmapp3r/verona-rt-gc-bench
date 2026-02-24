// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include "pointer_churn.h"

#include <debug/harness.h>
#include <test/opt.h>

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

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

extern "C" EXPORT int run_benchmark(int argc, char** argv)
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
    Logging::enable_logging();

  // Run test with selected GC type and parameters
  pointer_churn::run_test(gc_type, num_nodes, num_mutations, seed);

  return 0;
}
