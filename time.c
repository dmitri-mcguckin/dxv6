#ifdef CS333_P2
#include "types.h"
#include "user.h"

int
main(int argc, char* argv[])
{
  uint subc = argc - 2;
  char* sub_command = (char*) malloc(strlen(argv[1]));
  char** sub_args;

  strcpy(sub_command, argv[1]);

  if(subc > 0){
    sub_args = (char**) malloc(subc * sizeof(char*));
    for(int i = 2; i < argc; ++i){
      sub_args[i - 2] = (char*) malloc(strlen(argv[i]));
      strcpy(sub_args[i - 2], argv[i]);
    }
  }
  else sub_args = NULL;

  uint start = uptime();
  exec(sub_command, sub_args);
  while(wait() > 0){}
  uint elapsed = uptime() - start;

  char* padding;
  uint seconds = elapsed / 1000;
  uint decimal = elapsed - (seconds * 1000);

  if(decimal < 10) padding = "00";
  else if(decimal < 100) padding = "0";
  else padding = "";

  printf(1, "%s ran in %d.%s%d seconds.\n", sub_command, seconds, padding, decimal);

  exit();
}
#endif
