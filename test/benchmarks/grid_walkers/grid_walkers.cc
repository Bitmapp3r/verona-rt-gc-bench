// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "grid_walkers.h"

#include "util/gc_benchmark.h"

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  Logging::enable_logging();
  size_t cores = 6;
  Scheduler& sched = Scheduler::get();
  sched.init(cores);
  sched.set_fair(false);
  sched.set_concurrentGC(false);

  test_walker(40, 20, 10);
  sched.run();
  //  test();
  puts("done");
  return 0;
}

#if defined(_WIN32) || defined(_WIN64)
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

extern "C" EXPORT int run_benchmark(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Default values
  int gridsize = 40;
  int numsteps = 20;
  int numwalkers = 10;

  // Parse command line arguments
  if (argc >= 2)
  {
    gridsize = std::atoi(argv[1]);
  }
  if (argc >= 3)
  {
    numsteps = std::atoi(argv[2]);
  }
  if (argc >= 4)
  {
    numwalkers = std::atoi(argv[3]);
  }

 Logging::enable_logging();
  size_t cores = 6;
  Scheduler& sched = Scheduler::get();
  sched.init(cores);
  sched.set_fair(false);
  sched.set_concurrentGC(false);

  test_walker(gridsize, numsteps, numwalkers);
  sched.run();
  //  test();
  puts("done");
  return 0;
}
