/********************************************************************/
/* This Header File contains a macro that records the results and   */
/* the file name that you want to record.                           */
/* Modify the information in the "N#" and "num macro",              */
/* "result-file-list.json" in this file to record the results.      */
/*								    */
/* - N# : Result File Name					    */
/* - num macro : nickname of FILE pointer access index  	    */
/* - "result-file-list.json"           				    */
/*	: A file containing information about the result file.      */
/* 	  Take the file name as the key and list the column names   */
/*   	  of the resulting csv file.				    */
/*								    */
/* Enter the column name of the csv file using the RESINIT macro    */
/* before recording the results. 				    */
/********************************************************************/

#define CPU_UTIL

#ifdef CPU_UTIL
	#define SCAN_CORE_MAX (7)
	#define UTIL_THREAD_CPU (15)
	#define UTIL_NAME "per_core_util.csv"
	#define INTERVAL (0.1)  // sec	
	#define UTIL_ITEM "core,usr,sys,nice,idle,wait,interval\n"

	#define percent(t, T) ((double)(t)/T*100)

	extern FILE *cpu_util;
	#define UTIL_INIT() fprintf(cpu_util, UTIL_ITEM)
	#define UTIL_RECORD(r, c) \
		 fprintf(cpu_util, "%d,%lu,%lu,%lu,%lu,%lu,%lu\n" \
				, c, r[c].usr, r[c].sys, r[c].nice, r[c].idle, r[c].wait, r[c].interval)

	void *record_util();
	void record_run_request();
	void record_stop_request();

	struct cpu_stat {
		unsigned long usr, sys, nice, idle, wait, irq, softirq, steal, guest, interval;
	};
#endif

#define MAX_FILE_NUM (10)
#define MAX_FNAME_LEN (100)


#define FILE_NUM (4)


#define N0 "mig_up_time.csv"
#define up (0)

#define N1 "mig_down_time.csv"
#define down (1)

#define N2 "mig_dram_to_dram_time.csv"
#define mvdram (2)

#define N3 "mig_nvm_to_nvm_time.csv"
#define mvnvm (3)

#define N4 ""
#define N5 ""
#define N6 ""
#define N7 ""
#define N8 ""
#define N9 ""

extern FILE *resultf[MAX_FILE_NUM];
#define RESINIT(name, str) fprintf(resultf[name], str)
//#define RESULT(name, str, ...) fprintf(stderr, str,  __VA_ARGS__)
#define RESULT(name, str, ...) fprintf(resultf[name], str, __VA_ARGS__)
//#define RESULT(name, str, ...) while (0) {}


void open_res_file();

