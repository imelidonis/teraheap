#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <aio.h>
#include <pthread.h>

#include "../include/regions.h"
#include "../include/sharedDefines.h"
#include "../include/asyncIO.h"
#include "../include/segments.h"
#include "../include/shadow.h"

#define HEAPWORD (8)      // In the JVM the heap is aligned to 8 words
#define HEADER_SIZE (32)  // Header size of the Dummy object	
#define align_size_up_(size, alignment) (((size) + ((alignment) - 1)) & ~((alignment) - 1))

static uint64_t _MAX_PARTITIONS;

struct _mem_pool{
    char *mmap_start;                                   //< Memory mapped allocation start addresss
    char* start_address;                                //< Aligned start address of TeraCache
    char* cur_alloc_ptr;                                //< Current allocation pointer of TeraCache
    char* stop_address;                                 //< Last address of TeraCache

    uint64_t size;                                              //< Current allocated bytes in TeraCache
};

char dev[150] = { '\0' };

uint64_t dev_size = 0;
uint64_t region_array_size = 0;
uint64_t max_rdd_id = 0;

// Global lock to prevent multiple threads
// from updating global variables.
pthread_mutex_t th_mem_pool_lock;
volatile struct _mem_pool th_mem_pool;
int fd;

int n_gc_threads = 0;

// Per-Thread state
#ifdef LOCKFREE_GLOBAL_UPDATE
size_t *per_thread_size;
char **per_thread_alloc_ptr;

// Each GC thread's entry of per_thread_size and per_thread_alloc_ptr is
// on its own cache line (stride of 8 entries of 8 bytes), so threads that
// allocate at the same time do not invalidate each other's line.
#define PER_THREAD_STRIDE 8
#define PER_THREAD_IDX(i) ((size_t) (i) * PER_THREAD_STRIDE)
#endif /* ifdef LOCKFREE_GLOBAL_UPDATE */

intptr_t align_size_up(intptr_t size, intptr_t alignment) {
	return align_size_up_(size, alignment);
}

void* align_ptr_up(void* ptr, size_t alignment) {
	return (void*)align_size_up((intptr_t)ptr, (intptr_t)alignment);
}

void create_file(const char *path, uint64_t size) {
  if (path == NULL || size == 0) {
    fprintf(stderr, "Cannot create H2 file! Path is [%s], size is [%lu]\n", path, size);
    return;
  }

  assertf(size >= 1024*1024*1024LU, "Size should be grater than 1GB");
  size_t path_size = strlen(path);

  // 142 chars + 7 for the the file name + 1 null-terminator = 150
  if (path_size > 142) {
    fprintf(stderr, "Path size is too long!\n");
    return;
  }

  // Copy the path and its terminating '\0' (path_size + 1 bytes). memcpy
  // instead of strncpy: same result, and no -Wstringop-truncation at -O3.
  memcpy(dev, path, path_size + 1);

  if (dev[path_size + 1] != '\0') {
    perror("[ERROR] - strncpy failed!");
    exit(EXIT_FAILURE);
  }

  // dev --> "/path/to/tempfile/.XXXXXX"
  strcat(dev, ".XXXXXX");

  fd = mkstemp(dev);
  unlink(dev);

  assertf(fd >= 1, "temp file was not created!");

  int status = posix_fallocate(fd, 0, size);

  if (status != 0) {
    fprintf(stderr, "[%s|%s|%d] Fallocate error %d\n",__FILE__,__func__,__LINE__, status);
    exit(EXIT_FAILURE);
  }
}

void reset_per_thread_state() {
#ifdef LOCKFREE_GLOBAL_UPDATE
  for (int i = 0; i < n_gc_threads; i++) {
    per_thread_size[PER_THREAD_IDX(i)] = 0;
    per_thread_alloc_ptr[PER_THREAD_IDX(i)] = th_mem_pool.cur_alloc_ptr;
  }
#endif /* ifdef LOCKFREE_GLOBAL_UPDATE */
}

// Initialize allocator
void h2_init(uint64_t align, const char *h2_file_path, uint64_t h2_file_size, uint64_t partitions, int total_gc_threads) {
  fd = -1;
  _MAX_PARTITIONS = partitions;

#if ANONYMOUS
  // Anonymous mmap
  fd = open(DEV, O_RDWR);
  th_mem_pool.mmap_start = mmap(0, V_SPACE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
#else
  dev_size = h2_file_size;
  create_file(h2_file_path, dev_size);
  // Memory-mapped a file over a storage device
  th_mem_pool.mmap_start = mmap(0, dev_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
#endif

  assertf(th_mem_pool.mmap_start != MAP_FAILED, "Mapping Failed");

  // Card table in JVM needs the start address of TeraHeap to be align up
  th_mem_pool.start_address = (char *) align_ptr_up(th_mem_pool.mmap_start, align);
  th_mem_pool.cur_alloc_ptr = th_mem_pool.start_address;
  th_mem_pool.size = 0;

#if ANONYMOUS
  th_mem_pool.stop_address = th_mem_pool.mmap_start + V_SPACE;
  printf("Start address: %p\n", th_mem_pool.start_address);
  printf("Stop address: %p\n", th_mem_pool.stop_address);
#else
  th_mem_pool.stop_address = th_mem_pool.mmap_start + dev_size;
#endif

  region_array_size = dev_size / REGION_SIZE;

  assertf(region_array_size >= _MAX_PARTITIONS,
          "Device size should be larger, because region_array_size is "
          "calculated to be smaller than _MAX_PARTITIONS!");

  n_gc_threads = total_gc_threads;

#ifdef CUSTOM_GROUPING
  fprintf(stderr, "[INFO] custom grouping enabled: using thread id for groups\n");
  // NOTE: or the number of gc threads
  max_rdd_id = n_gc_threads;
#else
  max_rdd_id = region_array_size / _MAX_PARTITIONS;
#endif /* ifdef CUSTOM_GROUPING */

  pthread_mutex_init(&th_mem_pool_lock, NULL);

#ifdef LOCKFREE_GLOBAL_UPDATE
  fprintf(stderr, "[INFO] Lockfree enabled\n");
  per_thread_size = aligned_alloc(64, PER_THREAD_IDX(n_gc_threads) * sizeof(size_t));
  per_thread_alloc_ptr = aligned_alloc(64, PER_THREAD_IDX(n_gc_threads) * sizeof(char *));

  reset_per_thread_state();
#endif /* ifdef LOCKFREE_GLOBAL_UPDATE */

  init_regions(_MAX_PARTITIONS);
  req_init();
}

// Return the start address of the memory allocation pool
char* h2_start_addr_mem_pool() {
  assertf(th_mem_pool.start_address != NULL, "Start address is NULL");
  return th_mem_pool.start_address;
}

// Return the last address of the memory allocation pool
char* h2_stop_addr_mem_pool() {
  assertf(th_mem_pool.stop_address != NULL, "Stop address is NULL");
  return th_mem_pool.stop_address;
}

// Return the `size` of the memory allocation pool
size_t h2_mem_pool_size() {
  assertf(th_mem_pool.start_address != NULL, "Start address is NULL");
#if ANONYMOUS
  return V_SPACE;
#else
  return dev_size;
#endif
}

char* h2_allocate(size_t size, uint64_t rdd_id, uint64_t partition_id, uint64_t thread_id) {
  assert(thread_id < n_gc_threads);

  char* alloc_ptr = NULL;

#ifdef CUSTOM_GROUPING
  rdd_id = thread_id;
#endif /* ifdef CUSTOM_GROUPING */

  assertf(size > 0, "Object should be > 0");

  alloc_ptr = allocate_to_region(size * HEAPWORD, rdd_id, partition_id);

  if (alloc_ptr == NULL) {
    perror("[Error] - H2 Allocator is full");
    exit(EXIT_FAILURE);
  }

  assertf(alloc_ptr >= h2_start_addr_mem_pool() && alloc_ptr < h2_stop_addr_mem_pool(),
          "[ERROR] out of bounds allocation! %p !E [%p, %p)",
          alloc_ptr, h2_start_addr_mem_pool(), h2_stop_addr_mem_pool());

  // Back new SH space with shadow chunks during mixed GCs (shadow.h)
  shadow_ensure(alloc_ptr, size * HEAPWORD, thread_id);

  char *cur_allocation_ptr = (char *) (((uint64_t) alloc_ptr) + size * HEAPWORD);

#ifdef LOCKFREE_GLOBAL_UPDATE
  per_thread_size[PER_THREAD_IDX(thread_id)] += size;

  if (cur_allocation_ptr > per_thread_alloc_ptr[PER_THREAD_IDX(thread_id)]) {
    per_thread_alloc_ptr[PER_THREAD_IDX(thread_id)] = cur_allocation_ptr;
  }
#else
  pthread_mutex_lock(&th_mem_pool_lock);

  char *prev_allocation_ptr = th_mem_pool.cur_alloc_ptr;

  th_mem_pool.size += size;

  if (cur_allocation_ptr > prev_allocation_ptr) {
    th_mem_pool.cur_alloc_ptr = cur_allocation_ptr;
  }

  pthread_mutex_unlock(&th_mem_pool_lock);
#endif /* ifdef LOCKFREE_GLOBAL_UPDATE */


#ifndef LOCKFREE_GLOBAL_UPDATE
  // NOTE: no sure if these are necessary.
  // NOTE: this code is executed in `update_allocator_global_state` once
  //    after the GC instead of once per allocation
  assertf(prev_allocation_ptr <= th_mem_pool.cur_alloc_ptr,
          "Error alloc ptr: Prev = %p, Cur = %p", prev_allocation_ptr,
          th_mem_pool.cur_alloc_ptr);

  // Alighn to 8 words the pointer (TODO: CHANGE TO ASSERTION)
  if ((uint64_t)th_mem_pool.cur_alloc_ptr % HEAPWORD != 0) {
    fprintf(stderr, "[INFO] alignment");
    th_mem_pool.cur_alloc_ptr =
        (char *)((((uint64_t)th_mem_pool.cur_alloc_ptr) + (HEAPWORD - 1)) &
                 -HEAPWORD);
  }
#endif /* ifndef LOCKFREE_GLOBAL_UPDATE */

  return alloc_ptr;
}

// Update global variables to avoid locking
void h2_update_allocator_global_state() {
#ifdef LOCKFREE_GLOBAL_UPDATE
  char *prev_allocation_ptr = th_mem_pool.cur_alloc_ptr;
  for (int i = 0; i < n_gc_threads; i++) {
    // update_size
    th_mem_pool.size += per_thread_size[PER_THREAD_IDX(i)];
    // update_top
    if (per_thread_alloc_ptr[PER_THREAD_IDX(i)] > th_mem_pool.cur_alloc_ptr) {
      th_mem_pool.cur_alloc_ptr = per_thread_alloc_ptr[PER_THREAD_IDX(i)];
    }
  }

  assertf(prev_allocation_ptr <= th_mem_pool.cur_alloc_ptr,
          "Error alloc ptr: Prev = %p, Cur = %p", prev_allocation_ptr,
          th_mem_pool.cur_alloc_ptr);
  (void)prev_allocation_ptr; // NOTE: otherwise compiler complains

  // Alighn to 8 words the pointer (TODO: CHANGE TO ASSERTION)
  if ((uint64_t)th_mem_pool.cur_alloc_ptr % HEAPWORD != 0) {
    fprintf(stderr, "[INFO] alignment");
    th_mem_pool.cur_alloc_ptr =
        (char *)((((uint64_t)th_mem_pool.cur_alloc_ptr) + (HEAPWORD - 1)) &
                 -HEAPWORD);
  }

  reset_per_thread_state();
#endif /* ifdef LOCKFREE_GLOBAL_UPDATE */
}

// Return the current allocation pointer
// NOTE: Does not require lock as it is not called during updates
char* h2_cur_alloc_ptr() {
  assertf(th_mem_pool.cur_alloc_ptr >= th_mem_pool.start_address &&
          th_mem_pool.cur_alloc_ptr < th_mem_pool.stop_address,
          "Allocation pointer out-of-bound")

#ifdef LOCKFREE_GLOBAL_UPDATE
  for (int i = 0; i < n_gc_threads; i++) {
    if (per_thread_alloc_ptr[PER_THREAD_IDX(i)] != th_mem_pool.cur_alloc_ptr) {
      fprintf(stderr, "[ERROR] Top not updated yet: [%d]:%p != %p\n", i, per_thread_alloc_ptr[PER_THREAD_IDX(i)], th_mem_pool.cur_alloc_ptr);
      exit(1);
    }
  }
#endif /* ifdef LOCKFREE_GLOBAL_UPDATE */

  return th_mem_pool.cur_alloc_ptr;
}

// Return 'true' if the allocator is empty, 'false' otherwise.
// Invariant: Initialize allocator
// NOTE: Does not require lock as it is not called during updates
int h2_is_empty() {
	assertf(th_mem_pool.start_address != NULL, "Allocator should be initialized");

  return th_mem_pool.size == 0;
}

// Close allocator and unmap pages
void h2_shutdown(void) {
	printf("CALL HERE");
  munmap(th_mem_pool.mmap_start, dev_size);
}

// Give advise to kernel to expect page references in sequential order.  (Hence,
// pages in the given range can be aggressively read ahead, and may be freed
// soon after they are accessed.)
void h2_enable_seq() {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_SEQUENTIAL);
}

// Give advise to kernel to expect page references in random order (Hence, read
// ahead may be less useful than normally.)
void h2_enable_rand() {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_NORMAL);
}

// Explicit write 'data' with 'size' in certain 'offset' using system call
// without memcpy.
void h2_write(char *data, char *offset, size_t size) {
#ifdef ASSERT
	ssize_t s_check = 0;
	uint64_t diff = offset - th_mem_pool.mmap_start;

	s_check = pwrite(fd, data, size * HEAPWORD, diff);
	assertf(s_check == size * HEAPWORD, "Sanity check: s_check = %ld", s_check);
#else
  uint64_t diff = offset - th_mem_pool.mmap_start;
  // The result is not checked in product builds (as before)
  ssize_t s_unused = pwrite(fd, data, size * HEAPWORD, diff);
  (void) s_unused;
#endif
}
	
// Explicit asynchronous write 'data' with 'size' in certain 'offset' using
// system call without memcpy.
// Do not use r_awrite with r_write
static void awrite_bytes(char *data, char *offset, size_t bytes) {
  req_add(fd, data, bytes, offset - th_mem_pool.mmap_start);
}

void h2_awrite(char *data, char *offset, size_t size) {
  // Parts inside shadow chunks are copied into them: a write to the file
  // there would land behind the chunk and be lost (shadow.h)
  shadow_split_write(data, offset, size * HEAPWORD, awrite_bytes);
}

int h2_shadow_init(size_t pool_bytes, int precopy_rounds) {
  return shadow_init(fd, th_mem_pool.mmap_start, dev_size, th_mem_pool.start_address,
                     pool_bytes, precopy_rounds, n_gc_threads);
}
	
// Check if all the asynchronous requestes have been completed
// Return 1 on succesfull, and 0 otherwise
int	h2_areq_completed() {
	return is_all_req_completed();
}
	
// We need to ensure that all the writes will be flushed from buffer
// cur_alloc_ptrcur_alloc_ptrhe and they will be written to the device.
void h2_fsync() {
#ifdef ASSERT
	int check = fsync(fd);
	assertf(check == 0, "Error in fsync");
#else
	fsync(fd);
#endif
}

// This function if for the FastMap hybrid version. Give advise to kernel to
// serve all the pagefault using regular pages.
void h2_enable_regular_flts(void) {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_NOHUGEPAGE);
}

// This function if for the FastMap hybrid version. Give advise to kernel to
// serve all the pagefault using huge pages.
void h2_enable_huge_flts(void) {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_HUGEPAGE);
}

int h2_verify_top(void) {
  return h2_cur_alloc_ptr() >= top_in_last_region() &&
         h2_cur_alloc_ptr() >= h2_start_addr_mem_pool() &&
         h2_cur_alloc_ptr() < h2_stop_addr_mem_pool();
}
