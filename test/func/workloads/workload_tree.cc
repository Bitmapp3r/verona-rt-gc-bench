// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "workload_tree.h"

#include <debug/harness.h>
#include <test/opt.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed);

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  std::cout << "Running with trace region" << std::endl;
  workload_tree::run_test<RegionType::Trace>();

  std::cout << "Running with rc region" << std::endl;
  workload_tree::run_test<RegionType::Rc>();

  std::cout << "Running with arena region" << std::endl;
  workload_tree::run_test<RegionType::Arena>();

  return 0;
}
