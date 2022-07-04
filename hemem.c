#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <sched.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <poll.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#include "hemem.h"
#include "uthash.h"
#include "pebs.h"
#include "res-record.h"

FILE *statsf;

pthread_t fault_thread;
pthread_t stats_thread;

int dramfd = -1;
int nvmfd = -1;
long uffd = -1;
uint64_t thread_number = 0;

bool is_init = false;

uint64_t memcps = 0;

static bool cr3_set = false;
uint64_t cr3 = 0;

struct hemem_page *pages = NULL;
pthread_mutex_t pages_lock = PTHREAD_MUTEX_INITIALIZER;

void *dram_devdax_mmap;
void *nvm_devdax_mmap;

__thread bool internal_call = false;

#ifndef USE_DMA
pthread_t copy_threads[MAX_COPY_THREADS];

struct pmemcpy {
	pthread_mutex_t lock;
	pthread_barrier_t barrier;
	_Atomic bool write_zeros;
	_Atomic void *dst;
	_Atomic void *src;
	_Atomic size_t length;
};

static struct pmemcpy pmemcpy;

void *memcpy_thread(void *arg)
{
	cpu_set_t cpuset;
	pthread_t thread;
	uint64_t tid = (uint64_t)arg;
	void *src, *dst;
	size_t length, chunk_size;

	assert(tid < MAX_COPY_THREADS);

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(COPY_THREAD_CPU + thread_number, &cpuset);
	thread_number++;
	int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		perror("pthread_setaffinity_np error\n");
		assert(0);
	}

	for(;;) {
		int r = pthread_barrier_wait(&pmemcpy.barrier);
		assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
		
		if (tid == 0) memcps++;

		length = pmemcpy.length;
		chunk_size = length / MAX_COPY_THREADS;
		dst = pmemcpy.dst + (tid * chunk_size);
		if (!pmemcpy.write_zeros) {
			src = pmemcpy.src + (tid * chunk_size);
			memcpy(dst, src, chunk_size);
		}
		else {
			memset(dst, 0, chunk_size);
		}

		r = pthread_barrier_wait(&pmemcpy.barrier);
		assert(r == 0 || r ==  PTHREAD_BARRIER_SERIAL_THREAD);
	}
	return NULL;
}

static void hemem_parallel_memcpy(void *dst, void *src, size_t length)
{
	pthread_mutex_lock(&pmemcpy.lock);
	pmemcpy.dst = dst;
	pmemcpy.src = src;
	pmemcpy.length = length;
	pmemcpy.write_zeros = false;

	int r = pthread_barrier_wait(&pmemcpy.barrier);
	assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);

	r = pthread_barrier_wait(&pmemcpy.barrier);
	assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);

	pthread_mutex_unlock(&(pmemcpy.lock));
}

static void hemem_parallel_memset(void* addr, int c, size_t n)
{
	pthread_mutex_lock(&(pmemcpy.lock));
	pmemcpy.dst = addr;
	pmemcpy.length = n;
	pmemcpy.write_zeros = true;

	int r = pthread_barrier_wait(&pmemcpy.barrier);
	assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);

	r = pthread_barrier_wait(&pmemcpy.barrier);
	assert(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
 
	pthread_mutex_unlock(&(pmemcpy.lock));
}

#endif

static void *hemem_stats_thread()
{
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(STATS_THREAD_CPU, &cpuset);
	int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		perror("pthread_setaffinity_np error\n");
		assert(0);
	}

	for (;;) {
		sleep(1);

		hemem_print_stats();
	}

	return NULL;
}


void add_page(struct hemem_page *page)
{
  struct hemem_page *p;
  pthread_mutex_lock(&pages_lock);
  HASH_FIND(hh, pages, &(page->va), sizeof(uint64_t), p);
  assert(p == NULL);
  HASH_ADD(hh, pages, va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&pages_lock);
}

void remove_page(struct hemem_page *page)
{
  pthread_mutex_lock(&pages_lock);
  HASH_DEL(pages, page);
  pthread_mutex_unlock(&pages_lock);
}

struct hemem_page* find_page(uint64_t va)
{
  struct hemem_page *page;
  HASH_FIND(hh, pages, &va, sizeof(uint64_t), page);
  return page;
}

void hemem_init()
{
	struct uffdio_api uffdio_api;
	
	
	internal_call = true;


	dramfd = open(DRAMPATH, O_RDWR);
	if (dramfd < 0)
		perror("dram open error\n");
	assert(dramfd >= 0);

	nvmfd = open(NVMPATH, O_RDWR);
	if (nvmfd < 0)
		perror("nvm open error\n");
	assert(nvmfd >= 0);

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1){
		perror("uffd error\n");
		assert(0);
	}

	statsf = fopen("stats.txt", "w+");
	if (statsf == NULL) {
		perror("stats file open error\n");
		assert(0);
	}

	open_res_file();
	RESINIT(up, "migrate_up_time\n");
	RESINIT(down, "migrate_down_time\n");
	RESINIT(mvdram, "migrate_dram_to_dram_time\n");
	RESINIT(mvnvm, "migrate_nvm_to_nvm_time\n");


	uffdio_api.api = UFFD_API;
	uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP |  UFFD_FEATURE_MISSING_SHMEM | UFFD_FEATURE_MISSING_HUGETLBFS;// | UFFD_FEATURE_EVENT_UNMAP | UFFD_FEATURE_EVENT_REMOVE;
	uffdio_api.ioctls = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
 		perror("ioctl uffdio_api");
		assert(0);
	}

	int s = pthread_create(&fault_thread, NULL, handle_fault, 0);
	if (s != 0) {
		perror("pthread_create");
		assert(0);
	}


#if DRAMSIZE != 0
	dram_devdax_mmap =libc_mmap(NULL, DRAMSIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, dramfd, 0);
	if (dram_devdax_mmap == MAP_FAILED) {
		perror("dram devdax mmap");
	assert(0);
	}
#endif

#if NVMSIZE !=0
	nvm_devdax_mmap =libc_mmap(NULL, NVMSIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, nvmfd, 0);
	if (nvm_devdax_mmap == MAP_FAILED) {
		perror("nvm devdax mmap");
		assert(0);
	}
#endif


#ifdef USE_DMA
	struct uffdio_dma_channs uffdio_dma_channs;
	
	printf("Use DMA\n");

	uffdio_dma_channs.num_channs = NUM_CHANNS;
	uffdio_dma_channs.size_per_dma_request = SIZE_PER_DMA_REQUEST;
	if (ioctl(uffd, UFFDIO_DMA_REQUEST_CHANNS, &uffdio_dma_channs) == -1) {
		printf("ioctl UFFDIO_API fails\n");
		exit(1);
	}
#else
	uint64_t i;
	int r = pthread_barrier_init(&pmemcpy.barrier, NULL, MAX_COPY_THREADS+1);
	assert(r == 0);

	r = pthread_mutex_init(&pmemcpy.lock, NULL);
	assert(r == 0);

	for (i = 0; i < MAX_COPY_THREADS; i++) {
		s = pthread_create(&copy_threads[i], NULL, memcpy_thread, (void *)i);
		assert(s == 0);
	}
#endif

	setup();

	s = pthread_create(&stats_thread, NULL, hemem_stats_thread, NULL);
	assert(s == 0);

	is_init = true;

	internal_call = false;

}

void hemem_stop()
{
#ifdef USE_DMA
	struct uffdio_dma_channs uffdio_dma_channs;
	
	uffdio_dma_channs.num_channs = NUM_CHANNS;
	uffdio_dma_channs.size_per_dma_request = SIZE_PER_DMA_REQUEST;
	if (ioctl(uffd, UFFDIO_DMA_RELEASE_CHANNS, &uffdio_dma_channs) == -1) {
		printf("ioctl UFFDIO_API fails\n");
		exit(1);
	}
#endif
	shutdown();
}


static void hemem_mmap_populate(void* addr, size_t length)
{
	// Page mising fault case - probably the first touch case
	// allocate in DRAM via LRU
	void* newptr;
	uint64_t offset;
	struct hemem_page *page;
	bool in_dram;
	uint64_t page_boundry;
	void* tmpaddr;
	uint64_t pagesize;

	assert(addr != 0);
	assert(length != 0);

	for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
		page = pagefault();
		assert(page != NULL);

		// let policy algorithm do most of the heavy lifting of finding a free page
		offset = page->devdax_offset;
		in_dram = page->in_dram;
		pagesize = pt_to_pagesize(page->pt);

		tmpaddr = (in_dram ? dram_devdax_mmap + offset : nvm_devdax_mmap + offset);
	#ifndef USE_DMA
		hemem_parallel_memset(tmpaddr, 0, pagesize);
	#else
		memset(tmpaddr, 0, pagesize);
	#endif
  
		// now that we have an offset determined via the policy algorithm, actually map
		// the page for the application
		newptr = libc_mmap((void*)page_boundry, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, (in_dram ? dramfd : nvmfd), offset);
		if (newptr == MAP_FAILED) {
			perror("newptr mmap");
			assert(0);
		}
 		 
  		pthread_mutex_lock(&pages_lock);
		if (in_dram) {
			dram_mmap_list_num++;
			dram_free_list_num--;
		}
		else {
			nvm_mmap_list_num++;
			nvm_free_list_num--;
		}
		pthread_mutex_unlock(&pages_lock);

		if (newptr != (void*)page_boundry) {
			fprintf(stderr, "hemem: mmap populate: warning, newptr != page boundry\n");
		}

		// re-register new mmap region with hememfaultfd
		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (uint64_t)newptr;
		uffdio_register.range.len = pagesize;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
		uffdio_register.ioctls = 0;
		if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
			perror("ioctl uffdio_register");
			assert(0);
		}

		// use mmap return addr to track new page's virtual address
		page->va = (uint64_t)newptr;
		assert(page->va != 0);
		assert(page->va % PAGE_SIZE == 0);
		page->migrating = false;
		page->migrations_up = page->migrations_down = 0;
 
		pthread_mutex_init(&(page->page_lock), NULL);

		// place in hemem's page tracking list
		add_page(page);
		page_boundry += pagesize;
	}

}

#define PAGE_ROUND_UP(x) (((x) + (PAGE_SIZE)-1) & (~((PAGE_SIZE)-1)))

void* hemem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	void *p;
	struct uffdio_cr3 uffdio_cr3;

	internal_call = true;

	assert(is_init);
	assert(length != 0);
  
	if ((flags & MAP_PRIVATE) == MAP_PRIVATE) {
		flags &= ~MAP_PRIVATE;
		flags |= MAP_SHARED;
		//LOG("hemem_mmap: changed flags to MAP_SHARED\n");
	}

	if ((flags & MAP_ANONYMOUS) == MAP_ANONYMOUS) {
		flags &= ~MAP_ANONYMOUS;
		//LOG("hemem_mmap: unset MAP_ANONYMOUS\n");
	}

	if ((flags & MAP_HUGETLB) == MAP_HUGETLB) {
		flags &= ~MAP_HUGETLB;
		//LOG("hemem_mmap: unset MAP_HUGETLB\n");
	}
  
	// reserve block of memory
	length = PAGE_ROUND_UP(length);
	p = libc_mmap(addr, length, prot, flags, dramfd, offset);
	if (p == NULL || p == MAP_FAILED) {
		perror("mmap");
		assert(0);
	}

	// register with uffd
	struct uffdio_register uffdio_register;
	uffdio_register.range.start = (uint64_t)p;
	uffdio_register.range.len = length;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
	uffdio_register.ioctls = 0;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		perror("ioctl uffdio_register");
		assert(0);
	}

	if (!cr3_set) {
		if (ioctl(uffd, UFFDIO_CR3, &uffdio_cr3) < 0) {
			perror("ioctl uffdio_cr3");
			assert(0);
		}
		cr3 = uffdio_cr3.cr3;
		cr3_set = true;
	}

   
	if ((flags & MAP_POPULATE) == MAP_POPULATE) {
		hemem_mmap_populate(p, length);
	}

 
	internal_call = false;
  
	return p;
}


int hemem_munmap(void* addr, size_t length)
{
	uint64_t page_boundry;
	struct hemem_page *page;
	int ret;

	internal_call = true;

	// for each page in region specified...
	for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
		page = find_page(page_boundry);
		if (page != NULL) {
			remove_page(page);
			
  			pthread_mutex_lock(&pages_lock);
			if (page->in_dram) {
				dram_mmap_list_num--;
				dram_free_list_num++;
			}
			else {
				nvm_mmap_list_num--;
				nvm_free_list_num++;
			}
  			pthread_mutex_unlock(&pages_lock);
			// move to next page
			page_boundry += pt_to_pagesize(page->pt);
		}
		else {
			page_boundry += BASEPAGE_SIZE;
		}
	}


	ret = libc_munmap(addr, length);

	internal_call = false;

	return ret;
}

void hemem_migrate_nvm(struct hemem_page *page[], uint64_t *new_offset, int num)
{
	void *old_addr;
	void *new_addr;
	void *newptr;
	uint64_t old_addr_offset, new_addr_offset;
	uint64_t pagesize;
	struct timeval start, end;
#ifdef USE_DMA
	struct uffdio_dma_copy uffdio_dma_copy;
#endif

	internal_call = true;

	gettimeofday(&start, NULL);

	for (int i=0; i < num; i++) {
		assert(!page[i]->in_dram);

		assert(page[i] != NULL);

		pagesize = pt_to_pagesize(page[i]->pt);

		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		old_addr = nvm_devdax_mmap + old_addr_offset;
		assert((uint64_t)old_addr_offset < NVMSIZE);
		assert((uint64_t)old_addr_offset + pagesize <= NVMSIZE);
	
		new_addr = nvm_devdax_mmap + new_addr_offset;
		assert((uint64_t)new_addr_offset < NVMSIZE);
		assert((uint64_t)new_addr_offset + pagesize <= NVMSIZE);

#ifdef USE_DMA
		uffdio_dma_copy.src[i] = (uint64_t)old_addr;
		uffdio_dma_copy.dst[i] = (uint64_t)new_addr;
		uffdio_dma_copy.len[i] = pagesize;
#else
		hemem_parallel_memcpy(new_addr, old_addr, pagesize);
#endif
	}

#ifdef USE_DMA
	uffdio_dma_copy.count = num;
	uffdio_dma_copy.mode = 0;
	uffdio_dma_copy.copy = 0;
	if (ioctl(uffd, UFFDIO_DMA_COPY, &uffdio_dma_copy) == -1) {
		//LOG("hemem_migrate_up, ioctl dma_copy fails for src:%lly, dst:%llu\n", old_addr, new_addr); 
		assert(0);
	}
#endif

	gettimeofday(&end, NULL);

	assert(libc_mmap != NULL);

	for (int i=0; i < num; i++) {
		pagesize = pt_to_pagesize(page[i]->pt);

		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		newptr = libc_mmap((void*)page[i]->va, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, nvmfd, new_addr_offset);
		if (newptr == MAP_FAILED) {
			perror("newptr mmap\n");
			assert(0);
		}
		if (newptr != (void*)page[i]->va) {
		fprintf(stderr, "mapped address is not same as faulting address\n");
		}
		assert(page[i]->va % PAGE_SIZE == 0);

		// re-register new mmap region with hememfaultfd
		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (uint64_t)newptr;
		uffdio_register.range.len = pagesize;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
		uffdio_register.ioctls = 0;
		if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
			perror("ioctl uffdio_register");
			assert(0);
		}

		page[i]->in_dram = false;
		page[i]->devdax_offset = new_addr_offset;
	}

	RESULT(mvnvm, "%lf\n", (elapsed(&start, &end)*1000000));

	internal_call = false;

}

void hemem_migrate_up(struct hemem_page *page[], uint64_t *new_offset, int num)
{
	void *old_addr;
	void *new_addr;
	void *newptr;
	uint64_t old_addr_offset, new_addr_offset;
	uint64_t pagesize;
	struct timeval start, end;
#ifdef USE_DMA
	struct uffdio_dma_copy uffdio_dma_copy;
#endif

	internal_call = true;

	gettimeofday(&start, NULL);

	for (int i=0; i < num; i++) {
		assert(!page[i]->in_dram);

		assert(page[i] != NULL);

		pagesize = pt_to_pagesize(page[i]->pt);

		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		old_addr = nvm_devdax_mmap + old_addr_offset;
		assert((uint64_t)old_addr_offset < NVMSIZE);
		assert((uint64_t)old_addr_offset + pagesize <= NVMSIZE);
	
		new_addr = dram_devdax_mmap + new_addr_offset;
		assert((uint64_t)new_addr_offset < DRAMSIZE);
		assert((uint64_t)new_addr_offset + pagesize <= DRAMSIZE);

#ifdef USE_DMA
		uffdio_dma_copy.src[i] = (uint64_t)old_addr;
		uffdio_dma_copy.dst[i] = (uint64_t)new_addr;
		uffdio_dma_copy.len[i] = pagesize;
#else
		hemem_parallel_memcpy(new_addr, old_addr, pagesize);
#endif
	}

#ifdef USE_DMA
	uffdio_dma_copy.count = num;
	uffdio_dma_copy.mode = 0;
	uffdio_dma_copy.copy = 0;
	if (ioctl(uffd, UFFDIO_DMA_COPY, &uffdio_dma_copy) == -1) {
		//LOG("hemem_migrate_up, ioctl dma_copy fails for src:%lly, dst:%llu\n", old_addr, new_addr); 
		assert(0);
	}
#endif

	gettimeofday(&end, NULL);

	assert(libc_mmap != NULL);

	for (int i=0; i < num; i++) {
		pagesize = pt_to_pagesize(page[i]->pt);

		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		newptr = libc_mmap((void*)page[i]->va, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, dramfd, new_addr_offset);
		if (newptr == MAP_FAILED) {
			perror("newptr mmap\n");
			assert(0);
		}
		if (newptr != (void*)page[i]->va) {
		fprintf(stderr, "mapped address is not same as faulting address\n");
		}
		assert(page[i]->va % PAGE_SIZE == 0);

		// re-register new mmap region with hememfaultfd
		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (uint64_t)newptr;
		uffdio_register.range.len = pagesize;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
		uffdio_register.ioctls = 0;
		if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
			perror("ioctl uffdio_register");
			assert(0);
		}

		page[i]->migrations_up++;
		page[i]->in_dram = true;
		page[i]->devdax_offset = new_addr_offset;
	}

	RESULT(up, "%lf\n", (elapsed(&start, &end)*1000000));

	internal_call = false;

}


void hemem_migrate_down(struct hemem_page *page[], uint64_t *new_offset, int num)
{
	void *old_addr;
	void *new_addr;
	void *newptr;
	uint64_t old_addr_offset, new_addr_offset;
	uint64_t pagesize;
	struct timeval start, end;
#ifdef USE_DMA
	struct uffdio_dma_copy uffdio_dma_copy;
#endif

	internal_call = true;
	
	gettimeofday(&start, NULL);
	

	for (int i=0; i < num; i++) {
		assert(page[i]->in_dram);

		pagesize = pt_to_pagesize(page[i]->pt);
  		assert(page != NULL);
		
		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		assert((uint64_t)new_addr_offset < NVMSIZE);
		assert((uint64_t)new_addr_offset + pagesize <= NVMSIZE);
	
		new_addr = nvm_devdax_mmap + new_addr_offset;

		assert((uint64_t)old_addr_offset < DRAMSIZE);
		assert((uint64_t)old_addr_offset + pagesize <= DRAMSIZE);
		old_addr = dram_devdax_mmap + old_addr_offset;

#ifdef USE_DMA	
		uffdio_dma_copy.src[i] = (uint64_t)old_addr;
		uffdio_dma_copy.dst[i] = (uint64_t)new_addr;
		uffdio_dma_copy.len[i] = pagesize;
#else
		hemem_parallel_memcpy(new_addr, old_addr, pagesize);
#endif
	}

#ifdef USE_DMA
	uffdio_dma_copy.count = num;
	uffdio_dma_copy.mode = 0;
	uffdio_dma_copy.copy = 0;
	if (ioctl(uffd, UFFDIO_DMA_COPY, &uffdio_dma_copy) == -1) {
		//LOG("hemem_migrate_down, ioctl dma_copy fails for src:%lly, dst:%llu\n", old_addr, new_addr); 
		assert(0);
	}
#endif

	gettimeofday(&end, NULL);

	for (int i=0; i < num; i++) {
		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		newptr = libc_mmap((void*)page[i]->va, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, nvmfd, new_addr_offset);

		if (newptr == MAP_FAILED) {
			perror("newptr mmap\n");
			assert(0);
		}
		if (newptr != (void*)page[i]->va) {
			fprintf(stderr, "mapped address is not same as faulting address\n");
		}
		assert(page[i]->va % PAGE_SIZE == 0);

	// re-register new mmap region with hememfaultfd
		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (uint64_t)newptr;
		uffdio_register.range.len = pagesize;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
		uffdio_register.ioctls = 0;
		if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
			perror("ioctl uffdio_register");
			assert(0);
		}
 
		page[i]->migrations_down++;
		page[i]->in_dram = false;
		page[i]->devdax_offset = new_addr_offset;
	}

	RESULT(down, "%lf\n", (elapsed(&start, &end)*1000000));

	internal_call = false;

}

void hemem_migrate_dram(struct hemem_page *page[], uint64_t *new_offset, int num)
{
	void *old_addr;
	void *new_addr;
	void *newptr;
	uint64_t old_addr_offset, new_addr_offset;
	uint64_t pagesize;
	struct timeval start, end;
#ifdef USE_DMA
	struct uffdio_dma_copy uffdio_dma_copy;
#endif

	internal_call = true;
	
	gettimeofday(&start, NULL);
	

	for (int i=0; i < num; i++) {
		assert(page[i]->in_dram);

		pagesize = pt_to_pagesize(page[i]->pt);
  		assert(page != NULL);
		
		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		assert((uint64_t)new_addr_offset < DRAMSIZE);
		assert((uint64_t)new_addr_offset + pagesize <= DRAMSIZE);
	
		new_addr = dram_devdax_mmap + new_addr_offset;

		assert((uint64_t)old_addr_offset < DRAMSIZE);
		assert((uint64_t)old_addr_offset + pagesize <= DRAMSIZE);
		old_addr = dram_devdax_mmap + old_addr_offset;

#ifdef USE_DMA	
		uffdio_dma_copy.src[i] = (uint64_t)old_addr;
		uffdio_dma_copy.dst[i] = (uint64_t)new_addr;
		uffdio_dma_copy.len[i] = pagesize;
#else
		hemem_parallel_memcpy(new_addr, old_addr, pagesize);
#endif
	}

#ifdef USE_DMA
	uffdio_dma_copy.count = num;
	uffdio_dma_copy.mode = 0;
	uffdio_dma_copy.copy = 0;
	if (ioctl(uffd, UFFDIO_DMA_COPY, &uffdio_dma_copy) == -1) {
		//LOG("hemem_migrate_down, ioctl dma_copy fails for src:%lly, dst:%llu\n", old_addr, new_addr); 
		assert(0);
	}
#endif

	gettimeofday(&end, NULL);

	for (int i=0; i < num; i++) {
		old_addr_offset = page[i]->devdax_offset;
		new_addr_offset = new_offset[i];

		newptr = libc_mmap((void*)page[i]->va, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, dramfd, new_addr_offset);

		if (newptr == MAP_FAILED) {
			perror("newptr mmap\n");
			assert(0);
		}
		if (newptr != (void*)page[i]->va) {
			fprintf(stderr, "mapped address is not same as faulting address\n");
		}
		assert(page[i]->va % PAGE_SIZE == 0);

	// re-register new mmap region with hememfaultfd
		struct uffdio_register uffdio_register;
		uffdio_register.range.start = (uint64_t)newptr;
		uffdio_register.range.len = pagesize;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
		uffdio_register.ioctls = 0;
		if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
			perror("ioctl uffdio_register");
			assert(0);
		}
 
		page[i]->in_dram = true;
		page[i]->devdax_offset = new_addr_offset;
	}

	RESULT(mvdram, "%lf\n", (elapsed(&start, &end)*1000000));

	internal_call = false;

}

void hemem_wp_page(struct hemem_page *page[], bool protect, int num)
{

	uint64_t addr;
	struct uffdio_writeprotect wp;
	int ret;
	uint64_t pagesize;


	internal_call = true;
	
	for (int i=0; i < num; i++) {
		addr = page[i]->va;
		pagesize = pt_to_pagesize(page[i]->pt);

		assert(addr != 0);
		assert(addr % PAGE_SIZE == 0);

		wp.range.start = addr;
		wp.range.len = pagesize;
		wp.mode = (protect ? UFFDIO_WRITEPROTECT_MODE_WP : 0);
		ret = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);

		if (ret < 0) {
			perror("uffdio writeprotect\n");
			assert(0);
		}
	}

	internal_call = false;
}


void handle_wp_fault(uint64_t page_boundry)
{
	struct hemem_page *page;

	internal_call = true;

	page = find_page(page_boundry);
	assert(page != NULL);


	//LOG("hemem: handle_wp_fault: waiting for migration for page %lx\n", page_boundry);

	while (page->migrating);
	internal_call = false;
}


void handle_missing_fault(uint64_t page_boundry)
{
	// Page mising fault case - probably the first touch case
	// allocate in DRAM via LRU
	void* newptr;
	struct hemem_page *page;
	uint64_t offset;
	void* tmp_offset;
	bool in_dram;
	uint64_t pagesize;
	
	internal_call = true;

	assert(page_boundry != 0);

	// let policy algorithm do most of the heavy lifting of finding a free page
	page = pagefault(); 
	assert(page != NULL);
  
	offset = page->devdax_offset;
	in_dram = page->in_dram;
	pagesize = pt_to_pagesize(page->pt);

	tmp_offset = (in_dram) ? dram_devdax_mmap + offset : nvm_devdax_mmap + offset;

#ifdef USE_DMA
	memset(tmp_offset, 0, pagesize);
#else
	hemem_parallel_memset(tmp_offset, 0, pagesize);
#endif


	// now that we have an offset determined via the policy algorithm, actually map
	// the page for the application
	newptr = libc_mmap((void*)page_boundry, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_FIXED, (in_dram ? dramfd : nvmfd), offset);
	if (newptr == MAP_FAILED) {
		perror("newptr mmap");
		assert(0);
	}
	//LOG("hemem: mmaping at %p\n", newptr);
	if(newptr != (void *)page_boundry) {
		fprintf(stderr, "Not mapped where expected (%p != %p)\n", newptr, (void *)page_boundry);
		assert(0);
	}

	// re-register new mmap region with hememfaultfd
	struct uffdio_register uffdio_register;
	uffdio_register.range.start = (uint64_t)newptr;
	uffdio_register.range.len = pagesize;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
	uffdio_register.ioctls = 0;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		perror("ioctl uffdio_register");
		assert(0);
	}

	// use mmap return addr to track new page's virtual address
	page->va = (uint64_t)newptr;
	assert(page->va != 0);
	assert(page->va % PAGE_SIZE == 0);
	page->migrating = false;
	page->migrations_up = page->migrations_down = 0;
 
	// place in hemem's page tracking list
	add_page(page);


	internal_call = false;

}



void *handle_fault()
{
	static struct uffd_msg msg[MAX_UFFD_MSGS];
	ssize_t nread;
	uint64_t fault_addr;
	uint64_t fault_flags;
	uint64_t page_boundry;
	struct uffdio_range range;
	int ret;
	int nmsgs;
	int i;

	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(FAULT_THREAD_CPU, &cpuset);
	int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		perror("pthread_setaffinity_np");
		assert(0);
	}

	for (;;) {
		struct pollfd pollfd;
		int pollres;
		pollfd.fd = uffd;
		pollfd.events = POLLIN;

		pollres = poll(&pollfd, 1, -1);

		switch (pollres) {
			case -1:
				perror("poll");
				assert(0);
			case 0:
				fprintf(stderr, "poll read 0\n");
				continue;
			case 1:
				break;
			default:
				fprintf(stderr, "unexpected poll result\n");
				assert(0);
		}

		if (pollfd.revents & POLLERR) {
			fprintf(stderr, "pollerr\n");
			assert(0);
		}

		if (!pollfd.revents & POLLIN) {
			continue;
		}

		nread = read(uffd, &msg[0], MAX_UFFD_MSGS * sizeof(struct uffd_msg));
		if (nread == 0) {
			fprintf(stderr, "EOF on hememfaultfd\n");
			assert(0);
		}

		if (nread < 0) {
			if (errno == EAGAIN) {
				continue;
			}
			perror("read");
			assert(0);
		}

		if ((nread % sizeof(struct uffd_msg)) != 0) {
			fprintf(stderr, "invalid msg size: [%ld]\n", nread);
			assert(0);
		}

		nmsgs = nread / sizeof(struct uffd_msg);

		for (i = 0; i < nmsgs; i++) {
			//TODO: check page fault event, handle it
			if (msg[i].event & UFFD_EVENT_PAGEFAULT) {
				fault_addr = (uint64_t)msg[i].arg.pagefault.address;
				fault_flags = msg[i].arg.pagefault.flags;

				// allign faulting address to page boundry
				// huge page boundry in this case due to dax allignment
				page_boundry = fault_addr & ~(PAGE_SIZE - 1);

				if (fault_flags & UFFD_PAGEFAULT_FLAG_WP) {
					handle_wp_fault(page_boundry);
				}
				else {
					handle_missing_fault(page_boundry);
				}

				// wake the faulting thread
				range.start = (uint64_t)page_boundry;
				range.len = PAGE_SIZE;

				ret = ioctl(uffd, UFFDIO_WAKE, &range);

				if (ret < 0) {
					perror("uffdio wake\n");
					assert(0);
				}
			}
			else if (msg[i].event & UFFD_EVENT_UNMAP){
				fprintf(stderr, "Received an unmap event\n");
				assert(0);
			}
			else if (msg[i].event & UFFD_EVENT_REMOVE) {
				fprintf(stderr, "received a remove event\n");
				assert(0);
			}
			else {
				fprintf(stderr, "received a non page fault event\n");
				assert(0);
			}
		}
	}
}

void hemem_print_stats()
{
//	LOG_STATS("\n");
	mmgr_stats();
}
