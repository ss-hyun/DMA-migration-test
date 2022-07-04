#define _GNU_SOURCE

#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

#include "pebs.h"
#include "res-record.h"

#define SIZE (8UL * 1024UL * 1024UL * 1024UL)
#define PG_SIZE (2UL * 1024UL * 1024UL)
#define BENCH_THREAD_CPU (6)


int main()
{
	char *c;
	cpu_set_t cpuset;
	uint64_t npage = (SIZE/PG_SIZE);

	CPU_ZERO(&cpuset);
	CPU_SET(BENCH_THREAD_CPU, &cpuset);
	sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

	c = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_POPULATE | MAP_SHARED, NULL, 0);

	srand(1);
	for (uint64_t i = 0; i < SIZE; i++) {
		c[i] = rand() & 0x00ff;
	}

	record_run_request();

	printf("%d\n", migrate_down_request(npage, true));
	while (get_dram_to_nvm_list_num());

	sleep(1);

	printf("%d\n", migrate_up_request(npage, true));
	while (get_nvm_to_dram_list_num());

	record_stop_request();

	munmap(c, SIZE);
	while (get_total_mmap());


	return 0;
}

