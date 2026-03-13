#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  define BENCHMARK_EXPORT __declspec(dllexport)
#else
#  define BENCHMARK_EXPORT
#endif

// Macro to define the Windows DLL callback bridge
// Linux shares symbols by default with RTLD_GLOBAL, but Windows DLLs need
// explicit bridging
#if defined(_WIN32) || defined(_WIN64)
#  define BENCHMARK_WINDOWS_CALLBACK_BRIDGE() \
    using namespace verona::rt::api::internal; \
    extern "C" BENCHMARK_EXPORT void set_gc_callback( \
      void (*callback)(uint64_t, verona::rt::RegionType, size_t, size_t)) \
    { \
      static std::function<void( \
        uint64_t, verona::rt::RegionType, size_t, size_t)> \
        func; \
      if (callback) \
      { \
        func = callback; \
        verona::rt::set_gc_callback(&func); \
      } \
      else \
      { \
        verona::rt::set_gc_callback(nullptr); \
      } \
    }
#else
#  define BENCHMARK_WINDOWS_CALLBACK_BRIDGE()
#endif

#define RUN_BENCHMARK_MAIN() \
  int main(int argc, char** argv) \
  { \
    SystematicTestHarness harness(argc, argv); \
    harness.run([&]() { run_benchmark(argc, argv); }); \
    return 0; \
  }
