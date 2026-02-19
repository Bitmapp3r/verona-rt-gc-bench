#include "util/gc_benchmark.h"

#include <debug/harness.h>
#include <iostream>
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
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <path_to_so> [args...]\n";
    return 1;
  }

  const char* lib_path = argv[1];

  LibHandle handle = LIB_OPEN(lib_path);
  if (!handle)
  {
    std::cerr << "Library open error: " << lib_last_error() << "\n";
    return 1;
  }

#ifndef PLATFORM_WINDOWS
  // Clear any existing errors (Linux only — dlerror is stateful)
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

  int new_argc = argc - 1;
  char** new_argv = argv + 1;
  size_t runs = 2;
  size_t warmup_runs = 2;

  SystematicTestHarness harness(new_argc, new_argv);
  GCBenchmark benchmark;

  benchmark.run_benchmark(
    [&]() { harness.run([&]() { entry(new_argc, new_argv); }); },
    runs,
    warmup_runs);

  benchmark.print_summary(lib_path);
  LIB_CLOSE(handle);
  return 0;
}