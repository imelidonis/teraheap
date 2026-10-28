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

#include "regions.h"
#include "mem_pool.h"
#include "async_io.h"
#include "sharedDefines.h"
#include "shadow.h"

#define align_size_up_(size, alignment) (((size) + ((alignment) - 1)) & ~((alignment) - 1))

static char dev[150] = { '\0' };   // H2 file path (create_file)

uint64_t dev_size = 0;
uint64_t region_array_size = 0;
uint64_t max_rdd_id = 0;

volatile struct _mem_pool th_mem_pool;
int fd;

static int n_gc_threads = 0;

// Group regions by GC thread (thread_id) instead of by rdd (h2_init())
static bool group_by_gc_thread = false;

// Per-Thread state
static size_t *per_thread_size;
static char **per_thread_alloc_ptr;

// Each GC thread's entry of per_thread_size and per_thread_alloc_ptr is
// on its own cache line (stride of 8 entries of 8 bytes), so threads that
// allocate at the same time do not invalidate each other's line.
#define PER_THREAD_STRIDE 8
#define PER_THREAD_IDX(i) ((size_t) (i) * PER_THREAD_STRIDE)

static intptr_t align_size_up(intptr_t size, intptr_t alignment) {
	return align_size_up_(size, alignment);
}

static void* align_ptr_up(void* ptr, size_t alignment) {
	return (void*)align_size_up((intptr_t)ptr, (intptr_t)alignment);
}

static void create_file(const char *path, uint64_t size) {
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

static void reset_per_thread_state(void) {
  for (int i = 0; i < n_gc_threads; i++) {
    per_thread_size[PER_THREAD_IDX(i)] = 0;
    per_thread_alloc_ptr[PER_THREAD_IDX(i)] = th_mem_pool.cur_alloc_ptr;
  }
}

// Initialize allocator
void h2_init(uint64_t align, const char *h2_file_path, uint64_t h2_file_size, uint64_t partitions, int total_gc_threads,
             bool group_by_thread) {
  fd = -1;

  dev_size = h2_file_size;
  create_file(h2_file_path, dev_size);
  // Memory-mapped a file over a storage device
  th_mem_pool.mmap_start = mmap(0, dev_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  assertf(th_mem_pool.mmap_start != MAP_FAILED, "Mapping Failed");

  // Card table in JVM needs the start address of TeraHeap to be align up
  th_mem_pool.start_address = (char *) align_ptr_up(th_mem_pool.mmap_start, align);
  th_mem_pool.cur_alloc_ptr = th_mem_pool.start_address;
  th_mem_pool.size = 0;

  th_mem_pool.stop_address = th_mem_pool.mmap_start + dev_size;

  // Regions start at start_address (aligned up), so only whole regions
  // between start_address and stop_address are inside the mapping
  region_array_size = (uint64_t) (th_mem_pool.stop_address - th_mem_pool.start_address) / REGION_SIZE;

  assertf(region_array_size >= partitions,
          "Device size should be larger, because region_array_size is "
          "calculated to be smaller than _MAX_PARTITIONS!");

  n_gc_threads = total_gc_threads;

  group_by_gc_thread = group_by_thread;
  if (group_by_gc_thread) {
    fprintf(stderr, "[INFO] custom grouping enabled: using thread id for groups\n");
    max_rdd_id = n_gc_threads;
  } else {
    max_rdd_id = region_array_size / partitions;
  }

  fprintf(stderr, "[INFO] Lockfree enabled\n");
  per_thread_size = aligned_alloc(64, PER_THREAD_IDX(n_gc_threads) * sizeof(size_t));
  per_thread_alloc_ptr = aligned_alloc(64, PER_THREAD_IDX(n_gc_threads) * sizeof(char *));

  reset_per_thread_state();

  init_regions(partitions);
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
  return dev_size;
}

char* h2_allocate(size_t size, uint64_t rdd_id, uint64_t partition_id, uint64_t thread_id) {
  assert(thread_id < n_gc_threads);

  char* alloc_ptr = NULL;

  if (group_by_gc_thread)
    rdd_id = thread_id;

  assertf(size > 0, "Object should be > 0");

  alloc_ptr = allocate_to_region(size * HeapWordSize, rdd_id, partition_id);

  if (alloc_ptr == NULL) {
    perror("[Error] - H2 Allocator is full");
    exit(EXIT_FAILURE);
  }

  assertf(alloc_ptr >= h2_start_addr_mem_pool() && alloc_ptr < h2_stop_addr_mem_pool(),
          "[ERROR] out of bounds allocation! %p !E [%p, %p)",
          alloc_ptr, h2_start_addr_mem_pool(), h2_stop_addr_mem_pool());

  // Back new SH space with shadow chunks during mixed GCs (shadow.h)
  shadow_ensure(alloc_ptr, size * HeapWordSize, thread_id);

  char *cur_allocation_ptr = (char *) (((uint64_t) alloc_ptr) + size * HeapWordSize);

  per_thread_size[PER_THREAD_IDX(thread_id)] += size;

  if (cur_allocation_ptr > per_thread_alloc_ptr[PER_THREAD_IDX(thread_id)]) {
    per_thread_alloc_ptr[PER_THREAD_IDX(thread_id)] = cur_allocation_ptr;
  }

  return alloc_ptr;
}

// Update global variables to avoid locking
void h2_update_allocator_global_state() {
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
  if ((uint64_t)th_mem_pool.cur_alloc_ptr % HeapWordSize != 0) {
    fprintf(stderr, "[INFO] alignment");
    th_mem_pool.cur_alloc_ptr =
        (char *)((((uint64_t)th_mem_pool.cur_alloc_ptr) + (HeapWordSize - 1)) &
                 -HeapWordSize);
  }

  reset_per_thread_state();
}

// Return the current allocation pointer
// NOTE: Does not require lock as it is not called during updates
char* h2_cur_alloc_ptr() {
  assertf(th_mem_pool.cur_alloc_ptr >= th_mem_pool.start_address &&
          th_mem_pool.cur_alloc_ptr < th_mem_pool.stop_address,
          "Allocation pointer out-of-bound")

  for (int i = 0; i < n_gc_threads; i++) {
    if (per_thread_alloc_ptr[PER_THREAD_IDX(i)] != th_mem_pool.cur_alloc_ptr) {
      fprintf(stderr, "[ERROR] Top not updated yet: [%d]:%p != %p\n", i, per_thread_alloc_ptr[PER_THREAD_IDX(i)], th_mem_pool.cur_alloc_ptr);
      exit(1);
    }
  }

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

int h2_shadow_init(size_t pool_bytes, int precopy_rounds) {
  // A shadow chunk is attached only by its region's owner (shadow.h)
  if (!group_by_gc_thread) {
    fprintf(stderr, "[SHADOW] needs regions grouped by GC thread (h2_init)\n");
    return -1;
  }
  return shadow_init(fd, th_mem_pool.mmap_start, dev_size, th_mem_pool.start_address,
                     pool_bytes, precopy_rounds, n_gc_threads,
                     region_info, region_free_prot);
}
	
int h2_verify_top(void) {
  return h2_cur_alloc_ptr() >= top_in_last_region() &&
         h2_cur_alloc_ptr() >= h2_start_addr_mem_pool() &&
         h2_cur_alloc_ptr() < h2_stop_addr_mem_pool();
}
