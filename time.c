#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "uproc.h"

int
main(int argc, char* argv[])
{
	char* sub_command;
	char** sub_args = (char**) malloc((argc - 1) * sizeof(char*));
	sub_command = argv[1];
	printf(1, "Executing: %s [ ", sub_command);
	for(int i = 1; i < argc - 1; ++i){
		sub_args[i] = malloc(sizeof(char[strlen(argv[i])]));
		sub_args[i] = argv[i + 1];
		printf(1, "%s ", sub_args[i]);
	}
	printf(1, "]\n");
	
	uint start = 420;
	exec(sub_command, sub_args);
	uint elapsed = 860 - start;

	printf(1, "Program ran %d ticks!\n", elapsed);

	exit();
}
#endif
