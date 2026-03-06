// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "tree_transform.h"

#include <debug/harness.h>
#include <test/opt.h>

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

#if defined(_WIN32) || defined(_WIN64)
extern "C" EXPORT void set_gc_callback(void (*callback)(uint64_t, verona::rt::RegionType, size_t, size_t))
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

extern "C" EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Parse command-line arguments
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed);
  int depth = opt.is<int>("-d", 10);
  int transforms = opt.is<int>("-t", 5);

  // Parse GC type manually
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
  tree_transform::run_test(gc_type, depth, transforms);

  return 0;
}

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  run_benchmark(argc, argv);
  return 0;
}
