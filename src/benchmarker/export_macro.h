#pragma once

#if defined(_WIN32) || defined(_WIN64)
  #define BENCHMARK_EXPORT __declspec(dllexport)
#else
  #define BENCHMARK_EXPORT
#endif