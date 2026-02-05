#include "reproduction.h"

int main(int argc, char** argv) {


    opt::Opt opt(argc, argv);
    //Logging::enable_logging();
    test_reproduction(101, 50, 10);
    return 0;
}