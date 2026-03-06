#pragma once

#if defined(_WIN32) || defined(_WIN64)
  #define BENCHMARK_EXPORT __declspec(dllexport)
#else
  #define BENCHMARK_EXPORT
#endif

#if defined(_WIN32) || defined(_WIN64)
extern "C" BENCHMARK_EXPORT void set_gc_callback(void (*callback)(uint64_t, verona::rt::RegionType, size_t, size_t))
{
  static std::function<void(uint64_t, verona::rt::RegionType, size_t, size_t)> func;
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