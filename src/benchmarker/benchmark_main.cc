#include "util/gc_benchmark.h"

#include <debug/harness.h>
#include <dlfcn.h>
#include <iostream>
#include <test/opt.h>

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <path_to_so> [args...]\n";
    return 1;
  }

  const char* lib_path = argv[1];

  // Use RTLD_GLOBAL so library shares our RegionContext (and thus the GC callback)
  void* handle = dlopen(lib_path, RTLD_NOW | RTLD_GLOBAL);
  if (!handle)
  {
    std::cerr << "dlopen error: " << dlerror() << "\n";
    return 1;
  }

  // Clear any existing errors
  dlerror();

  using EntryFunc = int (*)(int, char**);

  auto entry = reinterpret_cast<EntryFunc>(
      dlsym(handle, "run_benchmark"));

  const char* error = dlerror();
  if (error != nullptr)
  {
    std::cerr << "dlsym error: " << error << "\n";
    dlclose(handle);
    return 1;
  }

  std::cout << "\nRunning benchmark: " << lib_path << "\n";

  int new_argc = argc - 1;
  char** new_argv = argv + 1;

  size_t runs = 2;
  size_t warmup_runs = 2;

  SystematicTestHarness harness(new_argc, new_argv);
  GCBenchmark benchmark;
  
  benchmark.run_benchmark(
    [&]() {
      harness.run([&]() { entry(new_argc, new_argv); });
    },
    runs,
    warmup_runs);

  benchmark.print_summary(lib_path);

  dlclose(handle);
  return 0;
}
