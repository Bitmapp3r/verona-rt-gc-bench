#include <debug/harness.h>

#include <region/region_api.h>
#include <test/opt.h>

template <typename Func>
auto run_test_with_region(RegionType rt, Func&& f) {
    switch (rt) {
        case RegionType::Arena:
            return f.template operator()<RegionType::Arena>();
        case RegionType::Trace:
        return f.template operator()<RegionType::Trace>();
        case RegionType::Rc:
            return f.template operator()<RegionType::Rc>();
    }
    throw std::runtime_error("Invalid RegionType");
}