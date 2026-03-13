#pragma once

#include "region/region_base.h"

#include <debug/harness.h>
#include <filesystem>
#include <verona.h>

RegionType string_to_region_type(const std::string& gc_type)
{
  if (gc_type == "trace")
  {
    return RegionType::Trace;
  }
  else if (gc_type == "arena")
  {
    return RegionType::Arena;
  }
  else if (gc_type == "rc")
  {
    return RegionType::Rc;
  }
  else if (gc_type == "semispace")
  {
    return RegionType::SemiSpace;
  }
  else
  {
    // Handle invalid input - default to Rc or throw exception
    return RegionType::Rc;
  }
}

RegionType parse_region_type(opt::Opt& opt)
// Default to Trace if no region is specified
{
  if (opt.has("--trace"))
    return RegionType::Trace;
  else if (opt.has("--arena"))
    return RegionType::Arena;
  else if (opt.has("--rc"))
    return RegionType::Rc;
  else if (opt.has("--semispace"))
    return RegionType::SemiSpace;
  else
    std::cout << "Warning: no region specified, defaulting to Trace\n";
  return RegionType::Trace;
}

template<typename F, typename... Args>
decltype(auto) run_with_region(RegionType rt, F&& f, Args&&... args)
{
  switch (rt)
  {
    case RegionType::Trace:
      return f.template operator()<RegionType::Trace>(
        std::forward<Args>(args)...);

    case RegionType::Arena:
      return f.template operator()<RegionType::Arena>(
        std::forward<Args>(args)...);

    case RegionType::Rc:
      return f.template operator()<RegionType::Rc>(std::forward<Args>(args)...);

    case RegionType::SemiSpace:
      return f.template operator()<RegionType::SemiSpace>(
        std::forward<Args>(args)...);

    default:
      throw std::invalid_argument("Unknown RegionType");
  }
}

#define MAKE_REGION_WRAPPER(name, func) \
  struct name \
  { \
    template<RegionType R, typename... Args> \
    decltype(auto) operator()(Args&&... args) const \
    { \
      return func<R>(std::forward<Args>(args)...); \
    } \
  };

#define DISPATCH_REGION(rt, wrapper, ...) \
  run_with_region(rt, wrapper{}, __VA_ARGS__)

inline void enable_benchmark_logging(opt::Opt& opt)
{
#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();
}

std::string getLibraryExtension() {
#ifdef _WIN32
    return ".dll";
#elif __APPLE__
    return ".dylib";
#else
    return ".so";
#endif
}
  namespace fs = std::filesystem;

std::string buildLibraryPath(const std::string& basePath, const std::string& libName) {
    return (fs::path(basePath) / (libName + getLibraryExtension())).string();
}

