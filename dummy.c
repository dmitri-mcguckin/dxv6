#ifdef CS333_P2
#include "types.h"
#include "user.h"

#define stdout 1
#define wait 10
#define max_wait (wait*10000000000)

int
main(int argc, char* argv[])
{
  int x = 0;
  printf(stdout, "Starting...\n");
  while(x < max_wait){
    //sleep(wait);
    x += wait;
  }
  printf(stdout, "Done!\n");

  /*int sc = fork();

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
  }*/
  exit();
}
#endif
