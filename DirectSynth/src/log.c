/*
 * log.c
 *
 *  Created on: Jan 22, 2015
 *      Author: scott
 */

#include "include.h"

FILE* log_fp = NULL;

void open_log(int my_id) {
	if (log_fp==NULL) {
		char log_filename[256];
		sprintf(log_filename, "log.%d", my_id);
		log_fp = fopen(log_filename, "w");
	}
}

void write_log(char* string) {
	struct timeval tv;
	time_t cur_time;
	struct tm* time_struct;
	char time_buf[60];
	gettimeofday(&tv, NULL);
	cur_time = tv.tv_sec;
	time_struct = localtime(&cur_time);
	strftime(time_buf, 59, "%b %d %T", time_struct);
	fprintf(log_fp, "%s.%06d> %s\n", time_buf, tv.tv_usec, string);
	fflush(log_fp);
}

void close_log() {
	fflush(log_fp);
	fclose(log_fp);
}

//AR: I added this because the memory numbers I could get before only gave one total figure
//AR: for a whole multi hour run, not how memory use changes over time or per worker. This
//AR: reads the memory this process is using right now straight from the operating
//AR: system, no extra library needed, and writes it through the same logging function
//AR: everything else uses, so it only costs anything when debug is turned on. It writes to
//AR: the same per rank log file already used to track progress. I call this once per
//AR: finished task, not once per point in the rupture, to keep the amount of extra writing
//AR: small.
void write_log_memory(char* label) {
	FILE* status_fp = fopen("/proc/self/status", "r");
	if (status_fp == NULL) {
		return;
	}
	char line[256];
	long vmrss_kb = -1;
	while (fgets(line, sizeof(line), status_fp) != NULL) {
		if (strncmp(line, "VmRSS:", 6) == 0) {
			sscanf(line + 6, "%ld", &vmrss_kb);
			break;
		}
	}
	fclose(status_fp);
	if (vmrss_kb >= 0) {
		char buf[300];
		sprintf(buf, "MEMORY %s: VmRSS=%ld kB", label, vmrss_kb);
		write_log(buf);
	}
}
