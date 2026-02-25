// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "arbitrary_nodes.h"

#include "debug/logging.h"

#include <debug/harness.h>
#include <test/opt.h>

using namespace verona::rt::api;

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Default values
  // int size = 10;
  int size = 101;
  int regions = 10;
  // int regions = 1;
  bool enable_log = false;

  // Parse command line arguments
  if (argc >= 3)
  {
    size = std::atoi(argv[1]);
    regions = std::atoi(argv[2]);
  }

  if (argc >= 4)
  {
    std::string log_arg = argv[3];
    if (log_arg == "log")
    {
      enable_log = true;
    }
  }

  // Enable logging if requested
  if (enable_log)
  {
    Logging::enable_logging();
  }

  size_t runs = 1;
  size_t warmup_runs = 1;

  SystematicTestHarness harness(argc, argv);

  arbitrary_nodes::run_churn_test<RegionType::Trace>(size, regions);

  arbitrary_nodes::run_test<RegionType::Trace>(size, regions);

  arbitrary_nodes::run_test<RegionType::Arena>(size, regions);

  return 0;
}
