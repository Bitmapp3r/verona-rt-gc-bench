#include "arbitrary_nodes.h"
#include "debug/logging.h"

int main(int argc, char** argv) {


    opt::Opt opt(argc, argv);
    
    Logging::enable_logging();
    run_test(3, 1);
    return 0;
}
