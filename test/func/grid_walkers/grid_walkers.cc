#include "grid_walkers.h"

int main(int argc, char** argv)
{
  opt::Opt opt(argc, argv);

  test_walker(40, 20, 10);
  return 0;
}