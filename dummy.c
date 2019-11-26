#ifdef CS333_P2
#include "types.h"
#include "user.h"

#define stdout 1
#define MILI 1000
#define TIME 4
#define WAIT MILI * TIME

int
main(int argc, char* argv[])
{
  int sc = fork();

  if(sc < 0){ // Fork failed
    printf(stdout, "Forke failed!\n");
  }
  else if(sc == 0){ // Child
    printf(stdout, "In child, sleeping for %ds...\n", TIME);
    sleep(WAIT);
  }
  else{ // Parent
    printf(stdout, "Waiting on child...\n");
    wait();
    printf(stdout, "Child done!\n");
  }
  exit();
}
#endif
