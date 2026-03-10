// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

/**
 * This test creates a ring of Organisms, each containing a tree of Nodes. The
 * test simulates multiple generations with a configurable population size.
 *
 * Each generation has a killing phase where organisms are randomly removed from
 * the ring based on a kill percentage, followed by a reproduction phase where
 * new organisms are created by combining genetic material (node trees) from two
 * parent organisms.
 *
 * This tests region-based GC with a dynamic population that grows and shrinks,
 * where garbage collection runs after the killing phase to reclaim unreachable
 * organisms and their associated node trees.
 **/

#pragma once

#include "cpp/cown.h"
#include "cpp/when.h"
#include "region/region_base.h"
#include <cstddef>
#include <debug/harness.h>
#include <iostream>
#include <vector>
#include <verona.h>
#include <functional>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>

using namespace verona::cpp;

namespace father
{
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

    struct Workload : V<Workload>
    {
        using BenchmarkFunc = int (*)(int, char**);

        LibHandle lib_handle = nullptr;
        BenchmarkFunc benchmark_fn_ptr = nullptr;
        std::vector<std::string> args;
        bool initialized = false;
        std::string error_msg;

        // Default constructor for region allocation
        Workload() : initialized(false) {}

        // Constructor: takes library path and argument strings
        Workload(const std::string& lib_path, const std::vector<std::string>& benchmark_args)
            : args(benchmark_args), initialized(false)
        {
            initialize(lib_path);
        }

        void initialize(const std::string& lib_path)
        {
            lib_handle = LIB_OPEN(lib_path.c_str());
            if (!lib_handle)
            {
                error_msg = std::string("Library open error: ") + lib_last_error();
                return;
            }

    #ifndef PLATFORM_WINDOWS
            dlerror();  // Clear any previous errors
    #endif

            benchmark_fn_ptr = reinterpret_cast<BenchmarkFunc>(LIB_SYM(lib_handle, "run_benchmark"));

    #ifdef PLATFORM_WINDOWS
            if (!benchmark_fn_ptr)
            {
                error_msg = std::string("Symbol lookup error: ") + lib_last_error();
                LIB_CLOSE(lib_handle);
                lib_handle = nullptr;
                return;
            }
    #else
            const char* error = dlerror();
            if (error != nullptr)
            {
                error_msg = std::string("Symbol lookup error: ") + error;
                LIB_CLOSE(lib_handle);
                lib_handle = nullptr;
                return;
            }
    #endif

            initialized = true;
        }

        // Destructor: clean up library handle
        ~Workload()
        {
            if (lib_handle)
            {
                LIB_CLOSE(lib_handle);
                lib_handle = nullptr;
            }
        }

        // Prevent copying (handle can't be safely copied)
        Workload(const Workload&) = delete;
        Workload& operator=(const Workload&) = delete;

        // Allow moving
        Workload(Workload&& other) noexcept
            : lib_handle(other.lib_handle),
              benchmark_fn_ptr(other.benchmark_fn_ptr),
              args(std::move(other.args)),
              initialized(other.initialized),
              error_msg(std::move(other.error_msg))
        {
            other.lib_handle = nullptr;
            other.benchmark_fn_ptr = nullptr;
            other.initialized = false;
        }

        Workload& operator=(Workload&& other) noexcept
        {
            if (this != &other)
            {
                if (lib_handle)
                    LIB_CLOSE(lib_handle);

                lib_handle = other.lib_handle;
                benchmark_fn_ptr = other.benchmark_fn_ptr;
                args = std::move(other.args);
                initialized = other.initialized;
                error_msg = std::move(other.error_msg);

                other.lib_handle = nullptr;
                other.benchmark_fn_ptr = nullptr;
                other.initialized = false;
            }
            return *this;

        }

        // Check if initialization was successful
        bool is_valid() const { return initialized; }
        const std::string& get_error() const { return error_msg; }

        // Trace function for garbage collection
        void trace(ObjectStack& st) const
        {
            // args are stored as strings which manage their own memory
            // lib_handle is just a handle pointer, not a managed object
            // Nothing to trace for GC
        }
    };

    // Usage example:
    /*
    Workload w("./libbenchmark.so", {"-O3", "--threads=4", "--iterations=1000"});
    if (!w.is_valid())
    {
        std::cerr << "Error: " << w.get_error() << "\n";
        return 1;
    }

    auto benchmark = w.get_benchmark();
    benchmark();  // Call it
    */

    struct WorkloadRegion : V<WorkloadRegion>
    {
        Workload* workload;

        WorkloadRegion() : workload(nullptr) {}

        void trace(ObjectStack& st) const
        {
            if (workload != nullptr)
                st.push(workload);
        }
    };

    class WorkloadCown
    {
    public:
        WorkloadCown(WorkloadRegion* region, size_t workload_id)
        : region(region), workload_id(workload_id) {}

        WorkloadRegion* region;
        size_t workload_id;

        ~WorkloadCown()
        {
            region_release(region);
        }
    };

    class Dummy {
        char dummy[100];
    };

    template<RegionType rt>
    cown_ptr<WorkloadCown> create_cown(size_t cown_num, const std::string& lib_path, const std::vector<std::string>& args)
    {
        WorkloadRegion* region = new (rt) WorkloadRegion();
        auto cown = make_cown<WorkloadCown>(region, cown_num);

        {
            UsingRegion ur(region);

            auto dummy = new Dummy();
            Workload* workload = new Workload();
            region->workload = workload;

            // Initialize the workload with library and args
            workload->initialize(lib_path);
            workload->args = args;

            if (!workload->is_valid())
            {
                std::cerr << "Failed to initialize workload " << cown_num
                          << ": " << workload->get_error() << "\n";
            }
            else
            {
                std::cout << "Initialized workload in region " << cown_num << "\n";
            }
        }

        return cown;
    }

    template<RegionType rt>
    void run_test(const std::vector<std::pair<std::string, std::vector<std::string>>>& workloads,
                  size_t num_regions = 1,
                  size_t iterations = 1)
    {

      std::cout << "\nStarting father test with " << workloads.size() << " mixed workloads...\n";
      std::cout << "Region type: ";
      switch(rt) {
        case RegionType::Trace: std::cout << "Trace"; break;
        case RegionType::Arena: std::cout << "Arena"; break;
        case RegionType::Rc:    std::cout << "Rc"; break;
        case RegionType::SemiSpace: std::cout << "SemiSpace"; break;
        default: std::cout << "Unknown"; break;
      }

      std::cout << "Num regions: " << num_regions << ", Iterations: " << iterations << "\n";

      std::vector<cown_ptr<WorkloadCown>> cowns;

      // Create workload cowns for each provided workload
      // Run each workload multiple times based on num_regions
      for (size_t region_id = 0; region_id < num_regions; region_id++)
      {
          for (size_t i = 0; i < workloads.size(); i++)
          {
              if (!workloads[i].first.empty())
              {
                  size_t cown_id = region_id * workloads.size() + i;
                  cown_ptr<WorkloadCown> cown = create_cown<rt>(cown_id, workloads[i].first, workloads[i].second);
                  cowns.push_back(cown);
              }
          }
      }

      if (cowns.empty())
      {
          std::cout << "No workloads configured. Provide library paths to run benchmarks.\n";
          return;
      }

      std::cout << "Created " << cowns.size() << " total region instances\n";

      // Execute workloads multiple times (iterations)
      for (size_t iter = 0; iter < iterations; iter++)
      {
        for (auto& cown : cowns)
        {
          when(cown) << [iter, iterations](auto c) {
              UsingRegion ur(c->region);
              if (c->region->workload && c->region->workload->is_valid())
              {
                  if (iter == 0)
                  {
                      std::cout << "Executing workload " << c->workload_id << " (iteration " << iter << ")...\n";
                  }
                  try
                  {
                      // Create proper argv array
                      std::vector<char*> argv;
                      for (auto& arg : c->region->workload->args)
                      {
                          argv.push_back(const_cast<char*>(arg.c_str()));
                      }

                      // Call the benchmark function

                      int result = c->region->workload->benchmark_fn_ptr(
                          static_cast<int>(argv.size()),
                          argv.data()
                      );

                      if (iter == 0 || iter == iterations - 1)
                      {
                          std::cout << "Workload " << c->workload_id << " iteration " << iter << " completed with result: " << result << "\n";
                      }
                  }
                  catch (const std::exception& e)
                  {
                      std::cerr << "Workload " << c->workload_id << " threw exception: " << e.what() << "\n";
                  }
                  catch (...)
                {
                    std::cerr << "Workload " << c->workload_id << " threw unknown exception\n";
                }
            }
            else
            {
                std::cerr << "Workload " << c->workload_id << " is not valid!\n";
            }
        };
        }
      }

      std::cout << "Father test scheduling complete (ran " << iterations << " iterations)\n";
    }

} // namespace father
