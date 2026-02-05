// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include "pointer_churn.h"

#include <debug/harness.h>
#include <test/opt.h>

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);
  
  // Parse command-line arguments
  size_t seed = opt.is<size_t>("--seed", 0);
  UNUSED(seed); // Fixed seed in test for reproducibility
  
  // Get GC type from command line (default: trace)
  std::string gc_type = "trace";
  for (int i = 1; i < argc; i++)
  {
    if (std::string(argv[i]) == "--gc" && i + 1 < argc)
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

  // Run test with selected GC type
  pointer_churn_gc::run_test(gc_type);

  return 0;
}
