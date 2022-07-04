#ifndef HEMEM_PEBS_H
#define HEMEM_PEBS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "hemem.h"

#define PEBS_KSWAPD_INTERVAL      (10000) // in us (10ms)
#define PEBS_KSWAPD_MIGRATE_RATE  (10UL * 1024UL * 1024UL * 1024UL) // 10GB

#define MIGRATION_THREAD_CPU (FAULT_THREAD_CPU + 1)
#define COPY_THREAD_CPU (MIGRATION_THREAD_CPU + 1)

#define MIGRATION_START_THRESHOLD (1)

extern uint64_t dram_to_dram_list_num;
extern uint64_t dram_to_nvm_list_num;
extern uint64_t dram_mmap_list_num;
extern uint64_t dram_free_list_num;
extern uint64_t nvm_to_nvm_list_num;
extern uint64_t nvm_to_dram_list_num;
extern uint64_t nvm_mmap_list_num;
extern uint64_t nvm_free_list_num;

struct hemem_page* pebs_pagefault(void);
struct hemem_page* pebs_pagefault_unlocked(void);
void pebs_init(void);
void pebs_shutdown();
void mmgr_stats();
int mig_dram_to_dram_request(int pages_num, bool random);
int mig_nvm_to_nvm_request(int pages_num, bool random);
int migrate_down_request(int pages_num, bool random);
int migrate_up_request(int pages_num, bool random);
int get_total_mmap();
int get_dram_to_dram_list_num();
int get_nvm_to_nvm_list_num();
int get_dram_to_nvm_list_num();
int get_nvm_to_dram_list_num();

#ifdef __cplusplus
}
#endif

#endif

