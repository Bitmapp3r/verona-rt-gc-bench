// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "grid_walkers.h"
#include "util/gc_benchmark.h"

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  test_walker(40, 20, 10);
  return 0;
}

extern "C" int run_benchmark(int argc, char** argv) {
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
    
    test_walker(gridsize, numsteps, numwalkers);
    return 0;
}
