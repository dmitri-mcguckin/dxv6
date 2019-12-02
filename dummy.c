#ifdef CS333_P2
#include "types.h"
#include "user.h"

#define stdout 1
#define wait 10
#define max_wait wait * 10000

int
main(int argc, char* argv[])
{
  int x = 0;

  printf(stdout, "Starting...\n");
  while(TRUE){
    //sleep(wait);
    x += wait;
  }
  printf(stdout, "Done!\n");
  exit();
}
#endif
