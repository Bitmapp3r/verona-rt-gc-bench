#include "util/gc_benchmark.h"

#include <debug/harness.h>
#include <iostream>
#include <region/region_api.h>
#include <test/opt.h>

#if defined(_WIN32) || defined(_WIN64)
#  define PLATFORM_WINDOWS
#endif

#ifdef PLATFORM_WINDOWS
#  include <windows.h>
using LibHandle = HMODULE;
#  define LIB_OPEN(path) LoadLibraryA(path)
#  define LIB_SYM(handle, name) GetProcAddress(handle, name)
#  define LIB_CLOSE(handle) FreeLibrary(handle)
inline const char* lib_last_error()
{
  static char buf[256];
  FormatMessageA(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr,
    GetLastError(),
    0,
    buf,
    sizeof(buf),
    nullptr);
  return buf;
}
#else
#  include <dlfcn.h>
using LibHandle = void*;
#  define LIB_OPEN(path) dlopen(path, RTLD_NOW | RTLD_GLOBAL)
#  define LIB_SYM(handle, name) dlsym(handle, name)
#  define LIB_CLOSE(handle) dlclose(handle)
inline const char* lib_last_error()
{
  return dlerror();
}
#endif

int main(int argc, char** argv)
{
  size_t runs = 0;
  size_t warmup_runs = 0;
  int filepath_index = -1;

  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
    {
      runs = std::stoul(argv[++i]);
    }
    else if (std::strcmp(argv[i], "--warmup_runs") == 0 && i + 1 < argc)
    {
      warmup_runs = std::stoul(argv[++i]);
    }
    else
    {
      filepath_index = i;
      break;
    }
  }

  if (filepath_index == -1 || runs == 0 || warmup_runs == 0)
  {
    std::cerr << "Usage: " << argv[0] << " --runs <n> --warmup_runs <n> <path_to_so> [args...]\n";
    return 1;
  }

  // Shift argc/argv to point at filepath and beyond
  int new_argc = argc - filepath_index;
  char** new_argv = argv + filepath_index;

  const char* lib_path = new_argv[0];
  LibHandle handle = LIB_OPEN(lib_path);
  if (!handle)
  {
    std::cerr << "Library open error: " << lib_last_error() << "\n";
    return 1;
  }
#ifndef PLATFORM_WINDOWS
  dlerror();
#endif
  using EntryFunc = int (*)(int, char**);
  auto entry = reinterpret_cast<EntryFunc>(LIB_SYM(handle, "run_benchmark"));
#ifdef PLATFORM_WINDOWS
  if (!entry)
  {
    std::cerr << "Symbol lookup error: " << lib_last_error() << "\n";
    LIB_CLOSE(handle);
    return 1;
  }
#else
  const char* error = dlerror();
  if (error != nullptr)
  {
    std::cerr << "Symbol lookup error: " << error << "\n";
    LIB_CLOSE(handle);
    return 1;
  }
#endif
  std::cout << "\nRunning benchmark: " << lib_path << "\n";
  SystematicTestHarness harness(new_argc, new_argv);
  GCBenchmark benchmark;
#ifdef PLATFORM_WINDOWS
  using CallbackSetter = void (*)(void (*)(uint64_t, verona::rt::RegionType, size_t, size_t));
  auto set_callback = reinterpret_cast<CallbackSetter>(LIB_SYM(handle, "set_gc_callback"));
  
  auto test_wrapper = [&]() {
    if (set_callback)
    {
      auto* local_callback = verona::rt::api::internal::RegionContext::get_gc_callback();
      if (local_callback)
      {
        static std::function<void(uint64_t, verona::rt::RegionType, size_t, size_t)>* current = nullptr;
        current = local_callback;
        set_callback([](uint64_t d, verona::rt::RegionType r, size_t m, size_t o) {
          if (current && *current) (*current)(d, r, m, o);
        });
      }
    }
    harness.run([&]() { entry(new_argc, new_argv); });
    if (set_callback) set_callback(nullptr);
  };
  
  benchmark.run_benchmark(test_wrapper, runs, warmup_runs);
#else
  benchmark.run_benchmark(
    [&]() { harness.run([&]() { entry(new_argc, new_argv); }); },
    runs,
    warmup_runs);
#endif
  benchmark.print_summary(lib_path);
  LIB_CLOSE(handle);
  return 0;
}