// The region array and allocation in regions (H2 objects go to the region of their (rdd, partition) id), and the region queries.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <pthread.h>
#include "../include/regions.h"
#include "../include/region_types.h"
#include "../include/sharedDefines.h"
#include "../include/shadow.h"

uint64_t _MAX_PARTITIONS;

// Global lock to only allow one thread to request new
// region and create a new mapping.
static pthread_mutex_t alloc_region_lock;

struct region *region_array;

// See region_types.h
char *region_array_base __attribute__((aligned(CACHE_LINE_SIZE)));
struct id_to_reg_mapping *id_mapping_array;
struct offset *offset_list;

static int32_t _next_region;

/*
 * Initialize region array, tera_group array and their fields
 */
void init_regions(uint64_t partitions) {
  int32_t i;
  _MAX_PARTITIONS = partitions;

  offset_list = NULL;

  // Cache-line aligned, so that each region (exactly one cache line, see
  // struct region) does not share a line with its neighbors: neighboring
  // regions can be active allocation regions of different labels, written
  // by other GC threads on every allocation
  region_array = aligned_alloc(CACHE_LINE_SIZE, region_array_size * sizeof(struct region));
  check_allocation_failure(region_array, "[ERROR] -- Failed to allocate memory for region_array\n");

  id_mapping_array =
      malloc((_MAX_PARTITIONS * max_rdd_id) * sizeof(struct id_to_reg_mapping));
  check_allocation_failure(
      id_mapping_array, "[ERROR] -- Failed to allocate memory for id_mapping_array\n");

#if DEBUG_PRINT 
  fprintf(stderr, "Total num of regions:%d\n", (int32_t) region_array_size);
#endif

  for (i = 0; i < region_array_size; i++) {
    region_array[i].start_address             = (i == 0) ? h2_start_addr_mem_pool() : (region_array[i - 1].start_address + (uint64_t) REGION_SIZE);
    if (i == 0) {
      region_array_base = region_array[0].start_address;
    }
    region_array[i].used                      = 0;
    region_array[i].last_allocated_end        = region_array[i].start_address;
    region_array[i].last_allocated_start      = NULL;
    region_array[i].first_allocated_start     = NULL;
    region_array[i].deps           = NULL;
#if ANONYMOUS
    region_array[i].size_mapped               = 0;
    region_array[i].offset_list               = NULL;
#endif
    region_array[i].rdd_id                    = _MAX_PARTITIONS - 1;
    region_array[i].part_id                   = _MAX_PARTITIONS - 1;
#if PR_BUFFER
    region_array[i].pr_buffer                 = malloc(sizeof(struct pr_buffer));
    region_array[i].pr_buffer->buffer         = NULL;
    region_array[i].pr_buffer->size           = 0;
    region_array[i].pr_buffer->alloc_ptr      = NULL;
    region_array[i].pr_buffer->first_obj_addr = NULL;
    pthread_mutex_init(&region_array[i].pr_buffer->buffer_lock, NULL);
#endif
  }

  for (i = 0; i < _MAX_PARTITIONS * max_rdd_id; i++) {
    id_mapping_array[i].mapped_region = NULL;
    pthread_mutex_init(&id_mapping_array[i].mapping_lock, NULL);
  }

  pthread_mutex_init(&alloc_region_lock, NULL);

#if ANONYMOUS
  struct offset *prev = NULL;

  for (i = 0 ; i < DEV_SIZE / MMAP_SIZE ; i++) {
    struct offset *ptr = malloc(sizeof(struct offset));
    ptr->offset = MMAP_SIZE * i;
    ptr->next = NULL;
    if (offset_list == NULL) {
      offset_list = ptr;
    } else {
      prev->next = ptr;
    }
    prev = ptr;
  }
#endif  
}

/*
 * Returns the start of cont_regions empty regions
 */
int32_t get_cont_regions(int32_t cont_regions) {
  static int32_t i = 0;
  int32_t j, index, end_index = i;

  for(; i < (region_array_size + end_index); i++) {
    for (j = i; j < (i + cont_regions); j++) {
      // A freed region that still has shadow chunks is not reused yet
      if (region_array[j % (int32_t) region_array_size].last_allocated_end == 
          region_array[j % (int32_t) region_array_size].start_address &&
          !shadow_range_busy(region_array[j % (int32_t) region_array_size].start_address, REGION_SIZE))
        continue;
      else
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
char* new_region(size_t size) {
  int32_t i;
  int32_t cont_regions = (size % REGION_SIZE != 0) ? (size / REGION_SIZE) + 1 : (size / REGION_SIZE);
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
      if (mprotect(region_array[i].start_address, REGION_SIZE, PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "mprotect error\n");
      }
    }
  }

  return region_array[cur_region].start_address;
}

uint64_t get_id(uint64_t rdd_id, uint64_t partition_id) {
  return (rdd_id % max_rdd_id) * _MAX_PARTITIONS + partition_id;
}

char* allocate_to_region(size_t size, uint64_t rdd_id, uint64_t partition_id) {
#if STATISTICS
  struct timeval start_time, end_time;
  gettimeofday(&start_time, NULL);
#endif

#if ANONYMOUS
  assert(size <= REGION_SIZE);
#endif

  int32_t id_index = get_id(rdd_id, partition_id);

  pthread_mutex_lock(&id_mapping_array[id_index].mapping_lock);

  if (id_mapping_array[id_index].mapped_region == NULL) {
    pthread_mutex_lock(&alloc_region_lock);
    char* res = new_region(size);
    pthread_mutex_unlock(&alloc_region_lock);
  
    if (res == NULL) {
      perror("[Error] - H2 Allocator is full");
      exit(EXIT_FAILURE);
    }
    /* If object spans more than 1 region we don't want to allocate more objects with it*/
    if (size < (uint64_t) REGION_SIZE) {
      id_mapping_array[id_index].mapped_region =
          &region_array[((res + size) - region_array_base) /
                        ((uint64_t)REGION_SIZE)];
      id_mapping_array[id_index].mapped_region->rdd_id = rdd_id;
      id_mapping_array[id_index].mapped_region->part_id = partition_id;
    }

#if ANONYMOUS
    // TODO: maybe needs patching
    uint64_t i = 0;
    struct offset *mmap_offset = offset_list;
    for (i = 0; i < (size/MMAP_SIZE)+1 ; i++) {
      struct offset *tmp = offset_list;
      assert(tmp != NULL);
      offset_list = offset_list->next;
      tmp->next = id_array[id_index]->offset_list;
      id_array[id_index]->offset_list = tmp;
    }
    char *address_mmapped = mmap(res, MMAP_SIZE * ((size/MMAP_SIZE)+1), PROT_READ|PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, mmap_offset->offset);
    id_array[id_index]->size_mapped += MMAP_SIZE * ((size/MMAP_SIZE)+1);
    if (address_mmapped == MAP_FAILED) {
      fprintf(stderr, "mmap to file failed 1\n");
    }
#endif

#if DEBUG_PRINT
    printf("Allocating from region %ld until region %ld\n",((res) - region_array_base) / ((uint64_t)REGION_SIZE),((res+size) - region_array_base) / ((uint64_t)REGION_SIZE));
#endif

    pthread_mutex_unlock(&id_mapping_array[id_index].mapping_lock);

    return res;
  }

  struct region *mapped_region = id_mapping_array[id_index].mapped_region;

  if (mapped_region->last_allocated_end + size >
      ((mapped_region->start_address + (uint64_t)REGION_SIZE))) {

    pthread_mutex_lock(&alloc_region_lock);
    char* res = new_region(size);
    pthread_mutex_unlock(&alloc_region_lock);
    
    if (res == NULL) {
      perror("[Error] - H2 Allocator is full");
      exit(EXIT_FAILURE);
    }

    /* If object spans more than 1 region we don't want to allocate more objects with it*/
    if (size < (uint64_t) REGION_SIZE) {
      id_mapping_array[id_index].mapped_region =
          &region_array[((res + size) - region_array_base) /
                        ((uint64_t)REGION_SIZE)];
      id_mapping_array[id_index].mapped_region->rdd_id = rdd_id;
      id_mapping_array[id_index].mapped_region->part_id = partition_id;
    }

    assertf(res != NULL, "No empty region");

#if ANONYMOUS
    // TODO: maybe needs patching
    uint64_t i = 0;
    for (i = 0; i < (size/MMAP_SIZE)+1 ; i++) {
      struct offset *tmp = offset_list;
      assert(tmp != NULL);
      offset_list = offset_list->next;
      tmp->next = id_array[id_index]->offset_list;
      id_array[id_index]->offset_list = tmp;
      char *address_mmapped = mmap(res + id_array[id_index]->size_mapped, MMAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, tmp->offset);
      id_array[id_index]->size_mapped += MMAP_SIZE;
      if (address_mmapped == MAP_FAILED) {
        fprintf(stderr, "mmap to file failed 2\n");
      }
    }
#endif

#if DEBUG_PRINT
    printf("Allocating from region %ld until region %ld\n",((res) - region_array_base) / ((uint64_t)REGION_SIZE),((res+size) - region_array_base) / ((uint64_t)REGION_SIZE));
#endif
    
    pthread_mutex_unlock(&id_mapping_array[id_index].mapping_lock);

    return res;
  }

  h2_mark_used(mapped_region->start_address);
  mapped_region->last_allocated_start =
      mapped_region->last_allocated_end;
  mapped_region->last_allocated_end =
      mapped_region->last_allocated_start + size;

#if ANONYMOUS
  if (size > MMAP_SIZE || id_array[id_index]->last_allocated_end > id_array[id_index]->start_address+id_array[id_index]->size_mapped) {
    size_t missing_size =  id_array[id_index]->last_allocated_end - (id_array[id_index]->start_address + id_array[id_index]->size_mapped); 
    uint64_t i = 0;
    for (i = 0; i < (missing_size/MMAP_SIZE)+1 ; i++) {
      struct offset *tmp = offset_list;
      assert(tmp != NULL);
      offset_list = offset_list->next;
      tmp->next = id_array[id_index]->offset_list;
      id_array[id_index]->offset_list = tmp;
      void *address_mmapped = mmap(id_array[id_index]->start_address + id_array[id_index]->size_mapped, MMAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, tmp->offset);
      id_array[id_index]->size_mapped += MMAP_SIZE;
      if (address_mmapped == MAP_FAILED) {
        fprintf(stderr, "mmap to file failed 3\n");
        fprintf(stderr, "Start of region:%p\n",id_array[id_index]->start_address);
        fprintf(stderr, "Last object ends at:%p\n",id_array[id_index]->last_allocated_start );
        fprintf(stderr, "MMAPS needed:%zu\n",((missing_size/MMAP_SIZE)+1));
        fprintf(stderr, "size:%zu\n",size);
        fprintf(stderr, "missing size:%zu\n",missing_size);
        return NULL;
      }
    }
  }
#endif

#if STATISTICS
  gettimeofday(&end_time, NULL);
  alloc_elapsedtime += (end_time.tv_sec - start_time.tv_sec) * 1000.0;
  alloc_elapsedtime += (end_time.tv_usec - start_time.tv_usec) / 1000.0;
#endif

#if DEBUG_PRINT
  printf("Allocating from region %ld until region %ld\n",((id_array[id_index]->last_allocated_start) - region_array_base) / ((uint64_t)REGION_SIZE),((id_array[id_index]->last_allocated_start+size) - region_array_base) / ((uint64_t)REGION_SIZE));
#endif

  char *last_alloc_start = mapped_region->last_allocated_start;

  pthread_mutex_unlock(&id_mapping_array[id_index].mapping_lock);

  return last_alloc_start;
}

void region_info(char *addr, char **top, bool *active) {
  uint64_t seg = (addr - region_array_base) / ((uint64_t)REGION_SIZE);
  struct region *r = &region_array[seg];
  *top = r->last_allocated_end;
  *active = id_mapping_array[get_id(r->rdd_id, r->part_id)].mapped_region == r;
}

int region_free_prot(char *addr) {
  uint64_t seg = (addr - region_array_base) / ((uint64_t)REGION_SIZE);
  bool is_free = region_array[seg].last_allocated_end == region_array[seg].start_address;
  return (protect_regions_on_free && is_free) ? PROT_NONE : (PROT_READ | PROT_WRITE);
}

/*
 * Checks if obj is before last object of region
 */
bool h2_is_before_last_object(char *obj) {
  uint64_t seg = (obj - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return (obj >= region_array[seg].last_allocated_end) ? false : true;
}

/*
 * Returns last object of region
 */
char* h2_get_last_object(char *obj) {
  uint64_t seg = (obj - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return region_array[seg].last_allocated_end;
}

// Returns true if object is first of its region false otherwise
bool h2_is_region_start(char *obj) {
  uint64_t seg = (obj - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return (region_array[seg].first_allocated_start == obj) ? true : false;
}

void print_objects_temporary_function(char *obj,const char *string) {
  printf("Object name: %s\n",string);
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
  uint64_t seg = (addr - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size, "Segment index is out of range %lu", seg);
  return region_array[seg].first_allocated_start;
}

int h2_get_num_of_continuous_regions(char *addr) {
  uint64_t seg = (addr - region_array_base) / ((uint64_t)REGION_SIZE);

  if (region_array[seg].last_allocated_end == region_array[seg].start_address)
    return 0;

  return ((region_array[seg].last_allocated_end - region_array[seg].first_allocated_start) % (uint64_t)REGION_SIZE != 0) ? 
          (region_array[seg].last_allocated_end - region_array[seg].first_allocated_start) / (uint64_t)REGION_SIZE + 1 :
          (region_array[seg].last_allocated_end - region_array[seg].first_allocated_start) / (uint64_t)REGION_SIZE;
}

/*
 * Get objects 'obj' region start address
 */
char* get_region_start_addr(char *obj, uint64_t rdd_id, uint64_t part_id) {
	uint64_t index = get_id(rdd_id, part_id);
  char *start_addr = NULL;

  pthread_mutex_lock(&id_mapping_array[index].mapping_lock);
  start_addr = id_mapping_array[index].mapped_region->start_address;
  pthread_mutex_unlock(&id_mapping_array[index].mapping_lock);

  return start_addr;
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
  uint64_t seg = (obj - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return region_array[seg].rdd_id;
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
  uint64_t seg = (obj - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return region_array[seg].part_id;
}

char* top_in_last_region() {
  int32_t i;
  struct region top_reg = region_array[0];

  for (i = 1 ; i < region_array_size; i++) {
    if (region_array[i].start_address == region_array[i].last_allocated_end) {
      continue;
    }
    top_reg = region_array[i];
  }

  return top_reg.last_allocated_end;
}

uint64_t h2_region_containing_addr(char *addr) {
  return (addr - region_array_base) / ((uint64_t)REGION_SIZE);
}

int h2_is_used(uint64_t region) {
  return region_array[region].used;
}

struct region *get_region(uint64_t region_index) {
  return &region_array[region_index];
}

char *region_get_start_address(struct region *region) {
  return region->start_address;
}

char *region_get_last_allocated_end(struct region *region) {
  return region->last_allocated_end;
}

bool h2_object_starts_from_region(char *obj) {
  uint64_t seg = (obj - region_array_base) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);
  return (region_array[seg].first_allocated_start != region_array[seg].start_address) ? false : true;
}
