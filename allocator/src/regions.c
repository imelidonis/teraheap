// The region array and allocation in regions (H2 objects go to the region of their (rdd, partition) id), and the region queries.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include "regions.h"
#include "region_types.h"
#include "sharedDefines.h"
#include "shadow.h"

uint64_t _MAX_PARTITIONS;

// Global lock to only allow one thread to request new
// region and create a new mapping.
static pthread_mutex_t alloc_region_lock;

struct region *region_array;

// See region_types.h
char *region_array_base __attribute__((aligned(CACHE_LINE_SIZE)));
uint64_t region_size;
unsigned region_shift;
struct id_to_reg_mapping *id_mapping_array;

static int32_t _next_region;

/* An empty region that starts at `start`, with an empty promotion buffer */
static void init_region(struct region *r, char *start) {
  r->start_address         = start;
  r->used                  = 0;
  r->last_allocated_end    = start;
  r->last_allocated_start  = NULL;
  r->first_allocated_start = NULL;
  r->deps                  = NULL;
  r->rdd_id                = _MAX_PARTITIONS - 1;
  r->part_id               = _MAX_PARTITIONS - 1;

  r->pr_buffer = malloc(sizeof(struct pr_buffer));
  check_allocation_failure(r->pr_buffer, "[ERROR] -- Failed to allocate memory for pr_buffer\n");
  r->pr_buffer->buffer         = NULL;
  r->pr_buffer->size           = 0;
  r->pr_buffer->alloc_ptr      = NULL;
  r->pr_buffer->first_obj_addr = NULL;
  pthread_mutex_init(&r->pr_buffer->buffer_lock, NULL);
}

/*
 * Initialize region array, tera_group array and their fields
 */
void init_regions(uint64_t partitions) {
  _MAX_PARTITIONS = partitions;

  // Cache-line aligned, so that each region (exactly one cache line, see
  // struct region) does not share a line with its neighbors: neighboring
  // regions can be active allocation regions of different labels, written
  // by other GC threads on every allocation
  region_array = aligned_alloc(CACHE_LINE_SIZE, region_array_size * sizeof(struct region));
  check_allocation_failure(region_array, "[ERROR] -- Failed to allocate memory for region_array\n");

  uint64_t n_ids = _MAX_PARTITIONS * max_rdd_id;
  id_mapping_array = malloc(n_ids * sizeof(struct id_to_reg_mapping));
  check_allocation_failure(
      id_mapping_array, "[ERROR] -- Failed to allocate memory for id_mapping_array\n");

#if DEBUG_PRINT 
  fprintf(stderr, "Total num of regions:%d\n", (int32_t) region_array_size);
#endif

  region_array_base = h2_start_addr_mem_pool();
  for (uint64_t i = 0; i < region_array_size; i++)
    init_region(&region_array[i], region_array_base + i * region_size);

  for (uint64_t i = 0; i < n_ids; i++) {
    id_mapping_array[i].mapped_region = NULL;
    pthread_mutex_init(&id_mapping_array[i].mapping_lock, NULL);
  }

  pthread_mutex_init(&alloc_region_lock, NULL);
}

/*
 * Returns the start of cont_regions empty regions
 */
static int32_t get_cont_regions(int32_t cont_regions) {
  static int32_t i = 0;
  int32_t j, index, end_index = i;

  for(; i < (region_array_size + end_index); i++) {
    for (j = i; j < (i + cont_regions); j++) {
      struct region *r = &region_array[j % (int32_t) region_array_size];
      // A freed region that still has shadow chunks is not reused yet
      if (!region_is_empty(r) || shadow_range_busy(r->start_address, region_size))
        break;
    }

    // Find region
    if (((j - 1) % (int32_t) region_array_size) == ((i % (int32_t) region_array_size) + cont_regions -1 )) {
      index = i;
      i = j % (int32_t) region_array_size;
      return index;
    } else {
      i = j;
    }
  }

  return -1;
}

/*
 * Finds an empty region and returns its starting address
 * Arguments: size is the size of the object we want to allocate (in
 * Bytes)

 * mark unused
 *  marking phase
        mark_used
   precompaction phase
      new address
 */
static char* new_region(size_t size) {
  int32_t i;
  int32_t cont_regions = (size % region_size != 0) ? (size / region_size) + 1 : (size / region_size);
  int32_t cur_region = get_cont_regions(cont_regions);

  if (cur_region == -1)
    return NULL;

  for (i = cur_region; i < cur_region + cont_regions; i++) {
    assertf(region_array[i].used == 0, "Error, write to an already used region");
    h2_mark_used(region_array[i].start_address);
    h2_references(region_array[cur_region].start_address, region_array[i].start_address);
    // h2_references(region_array[i].start_address, region_array[cur_region].start_address);
    region_array[i].last_allocated_start = region_array[cur_region].start_address;
    region_array[i].first_allocated_start = region_array[cur_region].start_address;
    region_array[i].last_allocated_end = region_array[cur_region].start_address + size;

    if (protect_regions_on_free) {
      // Enable accesses
      fprintf(stderr, "Enable permissions for region: %u\n", i);
      if (mprotect(region_array[i].start_address, region_size, PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "mprotect error\n");
      }
    }
  }

  return region_array[cur_region].start_address;
}

uint64_t get_id(uint64_t rdd_id, uint64_t partition_id) {
  return (rdd_id % max_rdd_id) * _MAX_PARTITIONS + partition_id;
}

/*
 * Allocate `size` bytes at the start of new empty regions and, for an
 * object smaller than a region, make the region that holds its end the
 * active region of mapping `m`. Called with m->mapping_lock held.
 */
static char *allocate_in_new_region(struct id_to_reg_mapping *m, size_t size,
                                    uint64_t rdd_id, uint64_t partition_id) {
  pthread_mutex_lock(&alloc_region_lock);
  char *res = new_region(size);
  pthread_mutex_unlock(&alloc_region_lock);

  if (res == NULL) {
    perror("[Error] - H2 Allocator is full");
    exit(EXIT_FAILURE);
  }

  // If the object spans more than one region, no other object goes after it
  if (size < region_size) {
    m->mapped_region = &region_array[region_index(res + size)];
    m->mapped_region->rdd_id = rdd_id;
    m->mapped_region->part_id = partition_id;
  }

  return res;
}

char* allocate_to_region(size_t size, uint64_t rdd_id, uint64_t partition_id) {
  struct id_to_reg_mapping *m = &id_mapping_array[get_id(rdd_id, partition_id)];
  char *res;

  pthread_mutex_lock(&m->mapping_lock);

  struct region *r = m->mapped_region;
  if (r == NULL || r->last_allocated_end + size > r->start_address + region_size) {
    res = allocate_in_new_region(m, size, rdd_id, partition_id);
  } else {
    h2_mark_used(r->start_address);
    r->last_allocated_start = r->last_allocated_end;
    r->last_allocated_end = r->last_allocated_start + size;
    res = r->last_allocated_start;
  }

  pthread_mutex_unlock(&m->mapping_lock);

#if DEBUG_PRINT
  printf("Allocating from region %lu until region %lu\n", region_index(res), region_index(res + size));
#endif

  return res;
}

void region_info(char *addr, char **top, bool *active) {
  struct region *r = &region_array[region_index(addr)];
  *top = r->last_allocated_end;
  *active = id_mapping_array[get_id(r->rdd_id, r->part_id)].mapped_region == r;
}

int region_free_prot(char *addr) {
  bool is_free = region_is_empty(&region_array[region_index(addr)]);
  return (protect_regions_on_free && is_free) ? PROT_NONE : (PROT_READ | PROT_WRITE);
}

/*
 * Checks if obj is before last object of region
 */
bool h2_is_before_last_object(char *obj) {
  return obj < region_of(obj)->last_allocated_end;
}

/*
 * Returns last object of region
 */
char* h2_get_last_object(char *obj) {
  return region_of(obj)->last_allocated_end;
}

// Returns true if object is first of its region false otherwise
bool h2_is_region_start(char *obj) {
  return region_of(obj)->first_allocated_start == obj;
}

/*
 * Start iteration over all active regions to print their object state.
 */
void h2_start_iterate_regions() {
	_next_region = 0;
}

/*
 * Return the next active region or NULL if we reached the end of the region
 * array.
 */
char* h2_get_next_region() {
  char *region_start_addr;

  // Find the next active region
  while (_next_region < region_array_size &&
         (region_array[_next_region].used == 0 ||
          region_array[_next_region].first_allocated_start != region_array[_next_region].start_address)) {
    _next_region++;
  }

  if (_next_region >= region_array_size)
    return NULL;

  fprintf(stderr, "[PLACEMENT] Region: %d\n", _next_region);

  region_start_addr = region_array[_next_region].start_address;
  _next_region++;

  return region_start_addr;
}

char *h2_get_first_object(char *addr) {
  return region_of(addr)->first_allocated_start;
}

int h2_get_num_of_continuous_regions(char *addr) {
  struct region *r = &region_array[region_index(addr)];

  if (region_is_empty(r))
    return 0;

  // Regions covered from the first object, rounded up
  uint64_t bytes = r->last_allocated_end - r->first_allocated_start;
  return (bytes + region_size - 1) / region_size;
}

/*
 * Get object 'groupId' (RDD Id). Each object is allocated based on a tera_group Id
 * and the partition Id that locates in teraflag.
 *
 * @obj: address of the object
 *
 * Return: the object rdd id
 */
uint64_t h2_get_obj_group_id(char *obj) {
  return region_of(obj)->rdd_id;
}

/*
 * Get object 'groupId'. Each object is allocated based on a tera_group Id
 * and the partition Id that locates in teraflag.
 *
 * obj: address of the object
 *
 * returns: the object partition Id
 */
uint64_t h2_get_obj_part_id(char *obj) {
  return region_of(obj)->part_id;
}

/* The allocation top of the last non-empty region (of region 0 if all are empty) */
char* top_in_last_region() {
  char *top = region_array[0].last_allocated_end;

  for (int32_t i = 1; i < region_array_size; i++) {
    if (!region_is_empty(&region_array[i]))
      top = region_array[i].last_allocated_end;
  }

  return top;
}

uint64_t h2_region_containing_addr(char *addr) {
  return region_index(addr);
}

int h2_is_used(uint64_t region) {
  return region_array[region].used;
}

bool h2_object_starts_from_region(char *obj) {
  struct region *r = region_of(obj);
  return r->first_allocated_start == r->start_address;
}
