#ifdef CS333_P2
#include "types.h"
#include "user.h"

int
main(int argc, char* argv[]){
  uint start = uptime();
  uint pid = fork();

  if(pid < 0){ printf(1, "Fork failed!\n"); exit(); }
  else if(pid == 0){
    exec(argv[1], &argv[1]);
    exit();
  }
  else{
    wait();
    uint elapsed = uptime() - start;
        
    char* padding;
    uint seconds = elapsed / 1000;
    uint decimal = elapsed - (seconds * 1000);

    if(decimal < 10) padding = "00";
    else if(decimal < 100) padding = "0";
    else padding = "";

    printf(1, "%s ran in %d.%s%d seconds.\n", argv[1], seconds, padding, decimal);
    exit();

  }
  exit();
}
#endif
