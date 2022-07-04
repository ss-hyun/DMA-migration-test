#ifndef HEMEM_H

#define HEMEM_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#ifndef __cplusplus
#include <stdatomic.h>
#else
#include <atomic>
#define _Atomic(X) std::atomic< X >
#endif

#ifdef __cplusplus
extern "C" {
#endif


#include "pebs.h"
#include "timer.h"
#include "interpose.h"
#include "uthash.h"
#include "fifo.h"

//#define STATS_THREAD

#define USE_DMA
#define NUM_CHANNS (1666666)
#define SIZE_PER_DMA_REQUEST (256*1024)
#define NUM_BATCHS (1)

//#define MAX_COPY_THREADS  (4)
#define MAX_COPY_THREADS  (1)

#define MEM_BARRIER() __sync_synchronize()

#define NVMSIZE   (240L * (1024L * 1024L * 1024L))
#define DRAMSIZE  (16L * (1024L * 1024L * 1024L))

#define DRAMPATH  "/dev/dax0.0"
#define NVMPATH   "/dev/dax1.0"

//#define PAGE_SIZE (1024 * 1024 * 1024)
//#define PAGE_SIZE (2 * (1024 * 1024))
#define BASEPAGE_SIZE	  (4UL * 1024UL)
#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define GIGAPAGE_SIZE   (1024UL * 1024UL * 1024UL)
//#define PAGE_SIZE 	    BASEPAGE_SIZE
#define PAGE_SIZE 	    HUGEPAGE_SIZE

#define FASTMEM_PAGES   ((DRAMSIZE) / (PAGE_SIZE))
#define SLOWMEM_PAGES   ((NVMSIZE) / (PAGE_SIZE))

#define BASEPAGE_MASK	(BASEPAGE_SIZE - 1)
#define HUGEPAGE_MASK	(HUGEPAGE_SIZE - 1)
#define GIGAPAGE_MASK   (GIGAPAGE_SIZE - 1)

#define BASE_PFN_MASK	(BASEPAGE_MASK ^ UINT64_MAX)
#define HUGE_PFN_MASK	(HUGEPAGE_MASK ^ UINT64_MAX)
#define GIGA_PFN_MASK   (GIGAPAGE_MASK ^ UINT64_MAX)

#define FAULT_THREAD_CPU  (0)
#define STATS_THREAD_CPU  (15)

#define pagefault(...) pebs_pagefault(__VA_ARGS__)
#define setup(...) pebs_init(__VA_ARGS__)
#define shutdown(...) pebs_shutdown(__VA_ARGS__)


extern FILE *copydramf;
//#define COPY_UP_TIME(str, ...) fprintf(stderr, str,  __VA_ARGS__)
#define COPY_DRAM_TIME(str, ...) fprintf(copydramf, str, __VA_ARGS__)
//#define COPY_UP_TIME(str, ...) while (0) {}

extern FILE *copynvmf;
//#define COPY_DOWN_TIME(str, ...) fprintf(stderr, str,  __VA_ARGS__)
#define COPY_NVM_TIME(str, ...) fprintf(copynvmf, str, __VA_ARGS__)
//#define COPY_DOWN_TIME(str, ...) while (0) {}

extern FILE *migdramf;
//#define UP_STATS(str, ...) fprintf(stderr, str,  __VA_ARGS__)
#define MIG_DRAM_STATS(str, ...) fprintf(migdramf, str, __VA_ARGS__)
//#define UP_STATS(str, ...) while (0) {}

extern FILE *mignvmf;
//#define DOWN_STATS(str, ...) fprintf(stderr, str,  __VA_ARGS__)
#define MIG_NVM_STATS(str, ...) fprintf(mignvmf, str, __VA_ARGS__)
//#define DOWN_STATS(str, ...) while (0) {}

extern FILE *statsf;
//#define LOG_STATS(str, ...) fprintf(stderr, str,  __VA_ARGS__)
//#define LOG_STATS(str, ...) fprintf(statsf, str, __VA_ARGS__)
#define LOG_STATS(str, ...) while (0) {}

#define MAX_UFFD_MSGS	    (1)



extern uint64_t cr3;
extern int dramfd;
extern int nvmfd;
extern bool is_init;
extern uint64_t missing_faults_handled;
extern uint64_t migrations_up;
extern uint64_t migrations_down;
extern __thread bool internal_malloc;
extern __thread bool internal_call;
extern __thread bool internal_munmap;
extern void* devmem_mmap;

enum pagetypes {
  HUGEP = 0,
  BASEP = 1,
  NPAGETYPES
};

struct hemem_page {
  uint64_t va;
  uint64_t devdax_offset;
  bool in_dram;
  enum pagetypes pt;
  volatile bool migrating;
  bool present;
  bool written;
  uint64_t migrations_up, migrations_down;
  pthread_mutex_t page_lock;

  UT_hash_handle hh;
  struct hemem_page *next, *prev;
  struct fifo_list *list;
};

static inline uint64_t pt_to_pagesize(enum pagetypes pt)
{
  switch(pt) {
  case HUGEP: return HUGEPAGE_SIZE;
  case BASEP: return BASEPAGE_SIZE;
  default: assert(!"Unknown page type");
  }
}

static inline enum pagetypes pagesize_to_pt(uint64_t pagesize)
{
  switch (pagesize) {
    case BASEPAGE_SIZE: return BASEP;
    case HUGEPAGE_SIZE: return HUGEP;
    default: assert(!"Unknown page ssize");
  }
}

void hemem_init();
void hemem_stop();
void* hemem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int hemem_munmap(void* addr, size_t length);
void *handle_fault();
void hemem_migrate_up(struct hemem_page *page[], uint64_t *new_offset, int num);
void hemem_migrate_down(struct hemem_page *page[], uint64_t *new_offset, int num);
void hemem_migrate_dram(struct hemem_page *page[], uint64_t *new_offset, int num);
void hemem_migrate_nvm(struct hemem_page *page[], uint64_t *new_offset, int num);
void hemem_wp_page(struct hemem_page *page[], bool protect, int num);
void hemem_print_stats();

#ifdef __cplusplus
}
#endif

#endif
