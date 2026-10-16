#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "string.h"

#include "rupgen_api.h"

void get_rupture_root(char* rup_root) {
    //Determine executable location - Linux-only
    char exe_loc[256];
    readlink("/proc/self/exe", exe_loc, 256);
    //Determine directory
    char* tok, *last_tok;
    char directory[256];
    directory[0] = '\0';
    last_tok = strtok(exe_loc, "/");
    tok = strtok(NULL, "/");
    while (tok!=NULL) {
        sprintf(directory, "%s/%s", directory, last_tok);
        last_tok = tok;
        tok = strtok(NULL, "/");
    }
    //Go back 3 levels
    sprintf(directory, "%s/../../../", directory);
    //Config file
    char cs_config_file[512];
    sprintf(cs_config_file, "%s/cybershake.cfg", directory);
    FILE* fp_in = fopen(cs_config_file, "r");
    char key[256];
    char value[256];
    while (fscanf(fp_in, "%s = %s", key, value)>0) {
        if (strcmp(key, "RUPTURE_ROOT")==0) {
            fclose(fp_in);
            sprintf(rup_root, value);
            return;
        }
    }
    printf("Couldn't find key RUPTURE_ROOT in configuration file %s.\n", cs_config_file);
    exit(1);
    return;
}

int main(int argc, char** argv) {
	/* AR: There is no memcached on MN5, so I skip the server setup when it is turned off at compile time. */
#ifndef BSC_DISABLE_MEMCACHED
	set_memcached_server("localhost");
#endif
	/* AR: argv[4] = rupture_root path, passed by workflow (avoids hardcoded site-specific path) */
	if (argc<5) {
		printf("Usage: %s <erf id> <src id> <rup id> <rupture_root>\n", argv[0]);
		exit(1);
	}
	int erf = atoi(argv[1]);
	int src = atoi(argv[2]);
	int rup = atoi(argv[3]);

	char rupture_root[256];
	strncpy(rupture_root, argv[4], sizeof(rupture_root) - 1);
	rupture_root[sizeof(rupture_root) - 1] = '\0';

	char rupture_file[512];
	sprintf(rupture_file, "%s/Ruptures_erf%d/%d/%d/%d_%d.txt", rupture_root, erf, src, rup, src, rup);
	rg_stats_t stats;
	int slip, hypo;
	rupgen_get_num_rv(rupture_file, &stats, RUPGEN_UNIFORM_HYPO);
	printf("%d\n", stats.numslip*stats.numhypo);
	return 0;
}
