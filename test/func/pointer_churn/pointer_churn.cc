// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include "pointer_churn.h"

#include <debug/harness.h>
#include <test/opt.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Parse command-line arguments
  // Fixed seed in test for reproducibility
  size_t seed = opt.is<size_t>("--seed", 12345);
  size_t num_nodes = opt.is<size_t>("-n", 12);
  size_t num_mutations = opt.is<size_t>("-m", 1000);

  // Parse GC type manually (opt::Opt::is doesn't support strings)
  std::string gc_type = "trace";
  for (int i = 1; i < argc; i++)
  {
    if (std::string(argv[i]) == "-g" && i + 1 < argc)
    {
      gc_type = argv[i + 1];
      break;
    }
  }

#ifdef CI_BUILD
  auto log = true;
#else
  auto log = opt.has("--log-all");
#endif

  if (log)
    Logging::enable_logging();

  // Run test with selected GC type and parameters
  pointer_churn::run_test(gc_type, num_nodes, num_mutations, seed);

  return 0;
}