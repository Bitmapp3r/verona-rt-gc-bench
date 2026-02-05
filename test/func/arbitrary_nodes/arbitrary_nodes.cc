#include "arbitrary_nodes.h"
#include "debug/logging.h"
#include <test/opt.h>
#include <util/gc_benchmark.h>

using namespace verona::rt::api;

int main(int argc, char** argv) {


    opt::Opt opt(argc, argv);
    
    Logging::enable_logging();
    
    size_t runs = 5;
    size_t warmup_runs = 10;

    std::cout << "Running arbitrary nodes benchmark" << std::endl;
    GCBenchmark benchmark;
    benchmark.run_benchmark([]() { run_test(3, 1); }, runs, warmup_runs);
    benchmark.print_summary("Arbitrary Nodes");
    
    return 0;
}
