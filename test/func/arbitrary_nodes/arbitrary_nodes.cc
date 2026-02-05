#include "arbitrary_nodes.h"
#include "debug/logging.h"

int main(int argc, char** argv) {


    opt::Opt opt(argc, argv);
    
    Logging::enable_logging();
    arbitrary_nodes::run_test(1, 2);
    return 0;
}
