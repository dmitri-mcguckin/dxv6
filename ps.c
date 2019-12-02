#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "uproc.h"

/*char* priorities[] = {
[0]         "low",
[1]         "medium",
[2]         "high",
[3]         "super"
};*/

int
main(int argc, char* argv[])
{
  const uint max = 16;
  struct uproc* procs = (struct uproc*) malloc(max * sizeof(struct uproc));
  uint status = getprocs(max, procs);
  if(status <= 0) exit();

  printf(1, "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\n");
  for(uint i = 0; i < status; ++i){
      uint et = procs[i].elapsed_ticks; // Elapsed time
      uint eseconds = et / 1000;
      uint edecimal = et - (eseconds * 1000);
      char* epadding;
      if(edecimal < 10) epadding = "00";
      else if(edecimal < 100) epadding = "0";
      else epadding = "";

      uint ct = procs[i].CPU_total_ticks; // CPU time
      uint cseconds = ct / 1000;
      uint cdecimal = ct - (cseconds * 1000);
      char* cpadding;
      if(cdecimal < 10) cpadding = "00";
      else if(cdecimal < 100) cpadding = "0";
      else cpadding = "";

      printf(1, "%d\t%s\t     %d\t%d\t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\n",
      procs[i].pid,
      procs[i].name,
      procs[i].uid,
      procs[i].gid,
      procs[i].ppid,
      procs[i].priority,
      eseconds,
      epadding,
      edecimal,
      cseconds,
      cpadding,
      cdecimal,
      procs[i].state,
      procs[i].size);
  }

  free(procs);
  exit();
}
#endif
