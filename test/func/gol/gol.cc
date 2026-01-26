#include "gol.h"

int main(int argc, char** argv) {
    // Standard Verona test harness setup
    opt::Opt opt(argc, argv);

    // Run a 20x20 grid for 100 generations
    // This creates high churn without taking too long
    test_game_of_life(20, 100);

    return 0;
}
