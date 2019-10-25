#ifdef CS333_P2
#include "types.h"
#include "user.h"

#define stdout 1
#define TIME 4000

int
main(int argc, char* argv[])
{
  printf(stdout, "Sleeping for %d ticks.\n", TIME);
  sleep(TIME);
  exit();
}
#endif
