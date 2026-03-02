#pragma once

#include "region/region_base.h"
#include <debug/harness.h>
#include <verona.h>

RegionType stringToRegionType(const std::string& gc_type)
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
  else
  {
    // Handle invalid input - default to Rc or throw exception
    return RegionType::Rc;
  }
}

template <typename F, typename... Args>
decltype(auto) run_with_region(RegionType rt, F&& f, Args&&... args) {
    switch (rt) {
        case RegionType::Trace:
            return f.template operator()<RegionType::Trace>(
                std::forward<Args>(args)...);

        case RegionType::Arena:
        return f.template operator()<RegionType::Arena>(
                std::forward<Args>(args)...);

        case RegionType::Rc:
            return f.template operator()<RegionType::Rc>(
                std::forward<Args>(args)...);

        default:
            throw std::invalid_argument("Unknown RegionType");
    }
}

#define MAKE_REGION_WRAPPER(name, func)                          \
struct name                                                      \
{                                                                \
  template <RegionType R, typename... Args>                     \
  decltype(auto) operator()(Args&&... args) const               \
  {                                                              \
    return func<R>(std::forward<Args>(args)...);                \
  }                                                              \
};

#define DISPATCH_REGION(rt, wrapper, ...) run_with_region(rt, wrapper{}, __VA_ARGS__)