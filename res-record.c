#define _GNU_SOURCE

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

#include "timer.h"
#include "res-record.h"


FILE *resultf[MAX_FILE_NUM];


#ifdef CPU_UTIL
FILE *cpu_util;
pthread_t util_thread;
struct cpu_stat record[SCAN_CORE_MAX], before[SCAN_CORE_MAX], after[SCAN_CORE_MAX];
int first = 1;
int running = 0;


static inline void cal_stat(int core)
{
	record[core].usr = after[core].usr - before[core].usr;
	record[core].sys = after[core].sys - before[core].sys;
	record[core].nice = after[core].nice - before[core].nice;
	record[core].idle = after[core].idle - before[core].idle;
	record[core].wait = after[core].wait - before[core].wait;
	record[core].irq = after[core].irq - before[core].irq;
	record[core].softirq = after[core].softirq - before[core].softirq;
	record[core].steal = after[core].steal - before[core].steal;
	record[core].guest = after[core].guest - before[core].guest;
	record[core].interval = record[core].usr + record[core].sys + record[core].nice +
				record[core].idle + record[core].wait + record[core].irq + 
				record[core].softirq + record[core].steal + record[core].guest;
}

static inline void cal_record()
{
	struct cpu_stat s;
	FILE *fp = fopen("/proc/stat", "r");

	fscanf(fp, "%*s %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u\n");
	for (int i=0; i<SCAN_CORE_MAX; i++) {
		fscanf(fp, "%*s %lu %lu %lu %lu %lu %lu %lu %lu %lu %*u\n",
				&(s.usr), &(s.sys), &(s.nice), &(s.idle),
				&(s.wait), &(s.irq), &(s.softirq), &(s.steal), &(s.guest));
		before[i] = after[i];
		after[i] = s;

		if (first) {
			continue;
		}

		cal_stat(i);
	}

	fclose(fp);
}

static inline void write_record()
{
	for (int i=0; i<SCAN_CORE_MAX; i++) {
		//fprintf(stderr, "write record\n");
		UTIL_RECORD(record, i);
	}
}

void *record_util()
{
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(UTIL_THREAD_CPU, &cpuset);
	int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		fprintf(stderr, "pthread setaffinity np error\n");
		assert(0);
	}

	struct timeval start, end;
	double time;

	gettimeofday(&start, NULL);
	for(;;) {
		gettimeofday(&end, NULL);

		time = elapsed(&start, &end);
		if (time <INTERVAL)
			continue;
		//fprintf(stderr, "bf running if\n");
		if (running) {
			cal_record();
			if (first)	first = 0;
			else		write_record();
		}
		
		gettimeofday(&start, NULL);
	}
		
}

void record_run_request()
{
	first = 1;
	running = 1;
}

void record_stop_request()
{
	running = 0;

}
#endif

void open_res(const char* fn, int fnum)
{
	if (fnum + 1 > FILE_NUM)
		return;

	resultf[fnum] = fopen(fn, "w+");
	if (resultf[fnum] == NULL) {
		fprintf(stderr, "result file open error (%s)\n", fn);
		assert(0);
	}
}

void open_res_file()
{
	int fnum = 0;
	
	open_res(N0, fnum++);
	open_res(N1, fnum++);
	open_res(N2, fnum++);
	open_res(N3, fnum++);
	open_res(N4, fnum++);
	open_res(N5, fnum++);
	open_res(N6, fnum++);
	open_res(N7, fnum++);
	open_res(N8, fnum++);
	open_res(N9, fnum++);

#ifdef CPU_UTIL
	if (pthread_create(&util_thread, NULL, record_util, 0) != 0) {
		fprintf(stderr, "pthread create error\n");
		assert(0);
	}

	cpu_util = fopen(UTIL_NAME, "w+");
	if (cpu_util == NULL) {
		fprintf(stderr, "file open error");
		assert(0);
	}

	UTIL_INIT();

#endif
}

