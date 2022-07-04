#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/ioctl.h>

#include "hemem.h"
#include "pebs.h"
#include "timer.h"
#include "res-record.h"

static struct fifo_list dram_free_list;
static struct fifo_list nvm_free_list;
static struct fifo_list dram_mmap_list;
static struct fifo_list nvm_mmap_list;
static struct fifo_list dram_to_dram_list;
static struct fifo_list dram_to_nvm_list;
static struct fifo_list nvm_to_dram_list;
static struct fifo_list nvm_to_nvm_list;

static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

uint64_t dram_mmap_list_num = 0;
uint64_t dram_to_nvm_list_num = 0;
uint64_t dram_to_dram_list_num = 0;
uint64_t dram_free_list_num = 0;
uint64_t nvm_mmap_list_num = 0;
uint64_t nvm_to_dram_list_num = 0;
uint64_t nvm_to_nvm_list_num = 0;
uint64_t nvm_free_list_num = 0;

void mmgr_stats()
{
	LOG_STATS("dram_free_list_num: [%ld]\tdram_mmap_list_num: [%ld]\tdram_to_dram_list_num: [%ld]\tdram_to_nvm_list_num: [%ld]\tnvm_free_list_num: [%ld]\tnvm_mmap_list_num: [%ld]\tnvm_to_nvm_list_num: [%ld]\tnvm_to_dram_list_num: [%ld]\n",
		dram_free_list_num,
		dram_mmap_list_num,
		dram_to_dram_list_num,
		dram_to_nvm_list_num,
		nvm_free_list_num,
		nvm_mmap_list_num,
		nvm_to_nvm_list_num,
		nvm_to_dram_list_num
	);
}

static void pebs_migrate_dram(struct hemem_page *page[], uint64_t *offset, int num)
{
	for (int i=0; i < num; i++)  page[i]->migrating = true;
	hemem_wp_page(page, true, num);
	hemem_migrate_dram(page, offset, num);
	for (int i=0; i < num; i++)  page[i]->migrating = false;
}
	
static void pebs_migrate_nvm(struct hemem_page *page[], uint64_t *offset, int num)
{
	for (int i=0; i < num; i++)  page[i]->migrating = true;
	hemem_wp_page(page, true, num);
	hemem_migrate_nvm(page, offset, num);
	for (int i=0; i < num; i++)  page[i]->migrating = false;
}

static void pebs_migrate_down(struct hemem_page *page[], uint64_t *offset, int num)
{
	for (int i=0; i < num; i++)  page[i]->migrating = true;
	hemem_wp_page(page, true, num);
	hemem_migrate_down(page, offset, num);
	for (int i=0; i < num; i++)  page[i]->migrating = false;
}

static void pebs_migrate_up(struct hemem_page *page[], uint64_t *offset, int num)
{
	for (int i=0; i < num; i++)  page[i]->migrating = true;
	hemem_wp_page(page, true, num);
	hemem_migrate_up(page, offset, num);
	for (int i=0; i < num; i++)  page[i]->migrating = false;
}

int get_total_mmap()
{
	return (dram_to_dram_list_num + dram_to_nvm_list_num + dram_mmap_list_num + nvm_to_nvm_list_num + nvm_to_dram_list_num + nvm_mmap_list_num);
}

int get_dram_to_dram_list_num()
{
	return dram_to_dram_list_num;
}

int get_dram_to_nvm_list_num()
{
	return dram_to_nvm_list_num;
}

int get_nvm_to_dram_list_num()
{
	return nvm_to_dram_list_num;
}

int get_nvm_to_nvm_list_num()
{
	return nvm_to_nvm_list_num;
}

int mig_dram_to_dram_request(int pages_num, bool random)
{
	struct hemem_page *page;
	int page_put = 0;
	int choose;

	if (random) {	
		srand(1);
		for (int i=0; i < pages_num; i++) {
			choose = rand() % dram_mmap_list_num;
			while (choose--) {
				enqueue_fifo(&dram_mmap_list, dequeue_fifo(&dram_mmap_list));
			}
			page = dequeue_fifo(&dram_mmap_list);
			if (page != NULL) {
				assert(page->in_dram);
				assert(page->present);
				enqueue_fifo(&dram_to_dram_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				dram_to_dram_list_num++;
				dram_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	else {
		for (int i=0; i < pages_num; i++) {
			page = dequeue_fifo(&dram_mmap_list);
			if (page != NULL) {
				assert(page->in_dram);
				assert(page->present);
				enqueue_fifo(&dram_to_dram_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				dram_to_dram_list_num++;
				dram_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	
	return page_put;
}

int mig_nvm_to_nvm_request(int pages_num, bool random)
{
	struct hemem_page *page;
	int page_put = 0;
	int choose;

	if (random) {	
		srand(1);
		for (int i=0; i < pages_num; i++) {
			choose = rand() % nvm_mmap_list_num;
			while (choose--) {
				enqueue_fifo(&nvm_mmap_list, dequeue_fifo(&nvm_mmap_list));
			}	
			page = dequeue_fifo(&nvm_mmap_list);
			if (page != NULL) {
				assert(!page->in_dram);
				assert(page->present);
				enqueue_fifo(&nvm_to_nvm_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				nvm_to_nvm_list_num++;
				nvm_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	else {
		for (int i=0; i < pages_num; i++) {
			page = dequeue_fifo(&nvm_mmap_list);
			if (page != NULL) {
				assert(!page->in_dram);
				assert(page->present);
				enqueue_fifo(&nvm_to_nvm_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				nvm_to_nvm_list_num++;
				nvm_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	return page_put;
}

int migrate_down_request(int pages_num, bool random)
{
	struct hemem_page *page;
	int page_put = 0;
	int choose;

	if (random) {	
		srand(1);
		for (int i=0; i < pages_num; i++) {
			choose = rand() % dram_mmap_list_num;
			while (choose--) {
				enqueue_fifo(&dram_mmap_list, dequeue_fifo(&dram_mmap_list));
			}
			page = dequeue_fifo(&dram_mmap_list);
			if (page != NULL) {
				assert(page->in_dram);
				assert(page->present);
				enqueue_fifo(&dram_to_nvm_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				dram_to_nvm_list_num++;
				dram_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	else {
		for (int i=0; i < pages_num; i++) {
			page = dequeue_fifo(&dram_mmap_list);
			if (page != NULL) {
				assert(page->in_dram);
				assert(page->present);
				enqueue_fifo(&dram_to_nvm_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				dram_to_nvm_list_num++;
				dram_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	
	return page_put;
}

int migrate_up_request(int pages_num, bool random)
{
	struct hemem_page *page;
	int page_put = 0;
	int choose;

	if (random) {	
		srand(1);
		for (int i=0; i < pages_num; i++) {
			choose = rand() % nvm_mmap_list_num;
			while (choose--) {
				enqueue_fifo(&nvm_mmap_list, dequeue_fifo(&nvm_mmap_list));
			}	
			page = dequeue_fifo(&nvm_mmap_list);
			if (page != NULL) {
				assert(!page->in_dram);
				assert(page->present);		
				enqueue_fifo(&nvm_to_dram_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				nvm_to_dram_list_num++;
				nvm_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}
	else {
		for (int i=0; i < pages_num; i++) {
			page = dequeue_fifo(&nvm_mmap_list);
			if (page != NULL) {
				assert(!page->in_dram);
				assert(page->present);		
				enqueue_fifo(&nvm_to_dram_list, page);
				page_put++;
				pthread_mutex_lock(&stats_lock);
				nvm_to_dram_list_num++;
				nvm_mmap_list_num--;
				pthread_mutex_unlock(&stats_lock);
			}
		}
	}

	return page_put;
}

void *migration_request()
{
	cpu_set_t cpuset;
	pthread_t thread;
	struct hemem_page *page[NUM_BATCHS];
	struct hemem_page *next_page[NUM_BATCHS];
	uint64_t offset[NUM_BATCHS];
	struct timeval start, end;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(MIGRATION_THREAD_CPU, &cpuset);
	int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		perror("pthread_setaffinity_np error\n");
		assert(0);
	}
	
	for(;;) {
		while (dram_to_dram_list_num) {
			int num = dram_to_dram_list_num > NUM_BATCHS ? NUM_BATCHS : dram_to_dram_list_num;
			//gettimeofday(&start, NULL);
	
			for (int i = 0; i < num; i++) {
				page[i] = dequeue_fifo(&dram_to_dram_list);
				next_page[i] = dequeue_fifo(&dram_free_list);
				offset[i] = next_page[i]->devdax_offset;
			}		
			pebs_migrate_dram(page, offset, num);

			for (int i = 0; i < num; i++) {
				next_page[i]->devdax_offset = offset[i];
				next_page[i]->present = false;
				next_page[i]->in_dram = true;
				enqueue_fifo(&dram_mmap_list, page[i]);
				enqueue_fifo(&dram_free_list, next_page[i]);
			}

			//gettimeofday(&end, NULL);

			pthread_mutex_lock(&stats_lock);
			dram_mmap_list_num += num;
			dram_to_dram_list_num -= num;
			pthread_mutex_unlock(&stats_lock);

			//RESULT(up,"%lf\n", (elapsed(&start, &end)*1000000));
		}
		
		while (dram_to_nvm_list_num) {
			int num = dram_to_nvm_list_num > NUM_BATCHS ? NUM_BATCHS : dram_to_nvm_list_num;
			//gettimeofday(&start, NULL);
	
			for (int i = 0; i < num; i++) {
				page[i] = dequeue_fifo(&dram_to_nvm_list);
				next_page[i] = dequeue_fifo(&nvm_free_list);
				offset[i] = next_page[i]->devdax_offset;
			}		
			pebs_migrate_down(page, offset, num);

			for (int i = 0; i < num; i++) {
				next_page[i]->devdax_offset = offset[i];
				next_page[i]->present = false;
				next_page[i]->in_dram = true;
				enqueue_fifo(&nvm_mmap_list, page[i]);
				enqueue_fifo(&dram_free_list, next_page[i]);
			}

			//gettimeofday(&end, NULL);

			pthread_mutex_lock(&stats_lock);
			nvm_mmap_list_num += num;
			dram_free_list_num += num;
			nvm_free_list_num -= num;
			dram_to_nvm_list_num -= num;
			pthread_mutex_unlock(&stats_lock);

			//RESULT(down, "%lf\n", (elapsed(&start, &end)*1000000));
		}

		while (nvm_to_dram_list_num) {
			int num = nvm_to_dram_list_num > NUM_BATCHS ? NUM_BATCHS : nvm_to_dram_list_num;
			//gettimeofday(&start, NULL);
	
			for (int i = 0; i < num; i++) {
				page[i] = dequeue_fifo(&nvm_to_dram_list);
				next_page[i] = dequeue_fifo(&dram_free_list);
				offset[i] = next_page[i]->devdax_offset;
			}		
			pebs_migrate_up(page, offset, num);

			for (int i = 0; i < num; i++) {
				next_page[i]->devdax_offset = offset[i];
				next_page[i]->present = false;
				next_page[i]->in_dram = false;
				enqueue_fifo(&dram_mmap_list, page[i]);
				enqueue_fifo(&nvm_free_list, next_page[i]);
			}

			//gettimeofday(&end, NULL);

			pthread_mutex_lock(&stats_lock);
			dram_mmap_list_num += num;
			nvm_free_list_num += num;
			dram_free_list_num -= num;
			nvm_to_dram_list_num -= num;
			pthread_mutex_unlock(&stats_lock);

			//RESULT(up, "%lf\n", (elapsed(&start, &end)*1000000));
		}

		while (nvm_to_nvm_list_num) {
			int num = nvm_to_nvm_list_num > NUM_BATCHS ? NUM_BATCHS : nvm_to_nvm_list_num;
			//gettimeofday(&start, NULL);
	
			for (int i = 0; i < num; i++) {
				page[i] = dequeue_fifo(&nvm_to_nvm_list);
				next_page[i] = dequeue_fifo(&nvm_free_list);
				offset[i] = next_page[i]->devdax_offset;
			}		
			pebs_migrate_nvm(page, offset, num);

			for (int i = 0; i < num; i++) {
				next_page[i]->devdax_offset = offset[i];
				next_page[i]->present = false;
				next_page[i]->in_dram = false;
				enqueue_fifo(&nvm_mmap_list, page[i]);
				enqueue_fifo(&nvm_free_list, next_page[i]);
			}

			//gettimeofday(&end, NULL);

			pthread_mutex_lock(&stats_lock);
			nvm_mmap_list_num += num;
			nvm_to_nvm_list_num -= num;
			pthread_mutex_unlock(&stats_lock);

			//RESULT(down, "%lf\n", (elapsed(&start, &end)*1000000));
		}
		
	}

	return NULL;
		
}


static struct hemem_page* pebs_allocate_page()
{
  struct hemem_page *page;

  page = dequeue_fifo(&dram_free_list);
  if (page != NULL) {
    assert(page->in_dram);
    assert(!page->present);

    page->present = true;
    enqueue_fifo(&dram_mmap_list, page);

    return page;
  }
    
  // DRAM is full, fall back to NVM
  page = dequeue_fifo(&nvm_free_list);
  if (page != NULL) {
    assert(!page->in_dram);
    assert(!page->present);

    page->present = true;
    enqueue_fifo(&nvm_mmap_list, page);


    return page;
  }

  assert(!"Out of memory");
}

struct hemem_page* pebs_pagefault(void)
{
  struct hemem_page *page;

  // do the heavy lifting of finding the devdax file offset to place the page
  page = pebs_allocate_page();
  assert(page != NULL);

  return page;
}


void pebs_init(void)
{
  pthread_t kswapd_thread;
  //LOG("pebs_init: started\n");


  pthread_mutex_init(&(dram_free_list.list_lock), NULL);
  for (int i = 0; i < DRAMSIZE / PAGE_SIZE; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = true;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);

    enqueue_fifo(&dram_free_list, p);
  
    dram_free_list_num++;
  }

  pthread_mutex_init(&(nvm_free_list.list_lock), NULL);
  for (int i = 0; i < NVMSIZE / PAGE_SIZE; i++) {
    struct hemem_page *p = calloc(1, sizeof(struct hemem_page));
    p->devdax_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = false;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    pthread_mutex_init(&(p->page_lock), NULL);

    enqueue_fifo(&nvm_free_list, p);

    nvm_free_list_num++;
  }

  pthread_mutex_init(&(dram_mmap_list.list_lock), NULL);
  pthread_mutex_init(&(nvm_mmap_list.list_lock), NULL);

  pthread_mutex_init(&(dram_to_dram_list.list_lock), NULL);
  pthread_mutex_init(&(nvm_to_dram_list.list_lock), NULL);
  pthread_mutex_init(&(dram_to_nvm_list.list_lock), NULL);
  pthread_mutex_init(&(nvm_to_nvm_list.list_lock), NULL);

  int r = pthread_create(&kswapd_thread, NULL, migration_request, NULL);
  assert(r == 0);

  //LOG("pebs_init: finished\n");

}

void pebs_shutdown()
{
}

