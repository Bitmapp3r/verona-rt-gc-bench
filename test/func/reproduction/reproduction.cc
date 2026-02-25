// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "reproduction.h"

#include <debug/harness.h>
#include <test/opt.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  size_t runs = 10;
  size_t warmup_runs = 10;
  size_t seed =
    opt.is<size_t>("--seed", 42); // Default 0 = random seed each run

  std::cout << "Running with trace region" << std::endl;
  reproduction::run_test<RegionType::Trace>(101, 50, 10, seed);

  std::cout << "\nRunning with rc region" << std::endl;
  reproduction::run_test<RegionType::Rc>(101, 50, 10, seed);

  std::cout << "\nRunning with arena region" << std::endl;
  reproduction::run_test<RegionType::Arena>(101, 50, 10, seed);

  return 0;
}