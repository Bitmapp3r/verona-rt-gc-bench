#include "arbitrary_nodes.h"

#include "debug/logging.h"

#include <test/opt.h>
#include <util/gc_benchmark.h>

using namespace verona::rt::api;

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  // Default values
  int size = 1010;
  int regions = 100;
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

    size_t runs = 5;
    size_t warmup_runs = 10;

    std::cout << "Running arbitrary nodes benchmark" << std::endl;
    GCBenchmark benchmark;
    benchmark.run_benchmark(
      [size, regions]() { arbitrary_nodes::run_test(size, regions); },
      runs,
      warmup_runs);
    benchmark.print_summary("Arbitrary Nodes");

    return 0;
  }
}