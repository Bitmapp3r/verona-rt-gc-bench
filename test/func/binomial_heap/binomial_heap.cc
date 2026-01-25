// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "binomial_heap.h"
#include <debug/harness.h>
#include <test/opt.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  // Parses `--seed` option with default of 0
  size_t seed = opt.is<size_t>("--seed", 0); 
  UNUSED(seed); // Not used for now

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  memory_tree::run_test();

  return 0;
}
