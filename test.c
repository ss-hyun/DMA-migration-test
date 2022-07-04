#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include "timer.h"
#include "res-record.h"

int main(){
	struct timeval start, end;
	FILE* fp;

	open_res_file();
	RESINIT(up, "up");
	RESINIT(down, "down");

	record_run_request();
	sleep(5);
	record_stop_request();

}
/*
struct cpu_stat {
	char name[6];
	unsigned long usr, sys, nice, idle, wait, irq, softirq, steal, guest;
};

struct cpu_stat before[16], current[16];

struct core_util {
	unsigned long usr, sys, nice, idle, wait, irq, softirq, steal, guest, interval;
};

struct core_util jiff[16];

#define percent(t, T) ((double)(t)/T*100)
void cal_jiff(int core)
{
	jiff[core].usr = current[core].usr - before[core].usr;
	jiff[core].sys = current[core].sys - before[core].sys;
	jiff[core].nice = current[core].nice - before[core].nice;
	jiff[core].idle = current[core].idle - before[core].idle;
	jiff[core].wait = current[core].wait - before[core].wait;
	jiff[core].irq = current[core].irq - before[core].irq;
	jiff[core].softirq = current[core].softirq - before[core].softirq;
	jiff[core].steal = current[core].steal - before[core].steal;
	jiff[core].guest = current[core].guest - before[core].guest;
	jiff[core].interval = jiff[core].usr + jiff[core].sys + jiff[core].nice
				+ jiff[core].idle + jiff[core].wait + jiff[core].irq
				+ jiff[core].softirq + jiff[core].steal + jiff[core].guest;
}


int main(){
	struct timeval start, end;
	FILE* fp;

//	printf("1\n");
//	open_res_file();
//	printf("2\n");
//	RESINIT(hello, "hello");
//	RESINIT(hi, "hi");

//	system("cat /proc/stat");
//	printf("%s\n", system("cat /proc/stat"));

	gettimeofday(&start, NULL);
	gettimeofday(&end, NULL);
	
	long a=10000000;
	unsigned long itv;
	double t;
	struct cpu_stat s;
	while (a--) {
		t = elapsed(&start, &end);
		if (t < 0.1) {
			gettimeofday(&end, NULL);
			continue;
		}

		gettimeofday(&start, NULL);
		fp = fopen("/proc/stat", "r");

		fscanf(fp, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %*lu\n",
				&(s.name), &(s.usr), &(s.sys), &(s.nice), &(s.idle),
				&(s.wait), &(s.irq), &(s.softirq), &(s.steal), &(s.guest));
		for (int i=0; i<16; i++) {
			fscanf(fp, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %*lu\n",
					&(s.name), &(s.usr), &(s.sys), &(s.nice), &(s.idle),
					&(s.wait), &(s.irq), &(s.softirq), &(s.steal), &(s.guest));
			before[i] = current[i];
			current[i] = s;
			cal_jiff(i);	
			itv = jiff[i].interval;
			printf("%lu %lf %lf %lf %lf %lf %lf %lf %lf %lf\n",itv,
				percent(jiff[i].usr, itv), percent(jiff[i].sys, itv), percent(jiff[i].nice, itv), 
				percent(jiff[i].idle, itv), percent(jiff[i].wait, itv), percent(jiff[i].irq, itv), 
				percent(jiff[i].softirq, itv), percent(jiff[i].steal, itv), percent(jiff[i].guest, itv));	
		}

		
		fclose(fp);

		gettimeofday(&end, NULL);
		printf("%lf\t%lf\n\n", t, elapsed(&start, &end)*1000000);
	}

	return 0;
}
*/
