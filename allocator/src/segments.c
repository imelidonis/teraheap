#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <limits.h>
#include <pthread.h>
#include "../include/segments.h"
#include "../include/regions.h"
#include "../include/sharedDefines.h"
#include "../include/sllist.h"

static uint64_t _MAX_PARTITIONS;

struct offset {
  uint64_t offset;
  struct offset *next;
};

#if PR_BUFFER
/* We use promotion buffer in each region to reduce the number of system calls
 * for small sized objects.
 */
struct pr_buffer {
  pthread_mutex_t buffer_lock;  /* Lock per buffer */
  char *buffer;                 /* Allocation buffer */
  char *first_obj_addr;         /* First object address in region */
  char *alloc_ptr;              /* Allocation pointer for the buffer */
  size_t size;					        /* Current size of the buffer */
};
#endif

/*
 * The struct for regions
 */
struct region {
    char *start_address;
    char *last_allocated_end;
    char *last_allocated_start;
    char *first_allocated_start;
    SLinkedList *dependency_list;
#if ANONYMOUS
  struct offset *offset_list;
  size_t size_mapped;
#endif
#if PR_BUFFER
    struct pr_buffer *pr_buffer;
#endif
    int8_t used;
    uint32_t rdd_id;
    uint32_t part_id;
};

// Mapping of rdd_id to the corresponding region.
// Locking the Mapping prevents multiple threads from
// allocating to the same same region.
//
// NOTE: we do not lock the region itself, as a thread
// can only access a region through the mapping.
struct id_to_reg_mapping {
  struct region *mapped_region;
  pthread_mutex_t mapping_lock;
};

// Global lock to only allow one thread to request new
// region and create a new mapping.
pthread_mutex_t alloc_region_lock;

struct region *region_array;
struct id_to_reg_mapping *id_mapping_array;
struct offset *offset_list;

int32_t		 _next_region;

#if STATISTICS
uint32_t    total_deps = 0;
double		  alloc_elapsedtime = 0.0;
double		  free_elapsedtime = 0.0;
#endif

uint        reclaimed_regions_count; 

static inline void check_allocation_failure(void *ptr, const char *msg) {
  if (!ptr) {
    perror(msg);
    exit(EXIT_FAILURE);
  }
}

/*
 * Initialize region array, tera_group array and their fields
 */
void init_regions(uint64_t partitions) {
  int32_t i;
  _MAX_PARTITIONS = partitions;

  offset_list = NULL;

  region_array = malloc(region_array_size * sizeof(struct region));
  check_allocation_failure(region_array, "[ERROR] -- Failed to allocate memory for region_array\n");

  id_mapping_array =
      malloc((_MAX_PARTITIONS * max_rdd_id) * sizeof(struct id_to_reg_mapping));
  check_allocation_failure(
      id_mapping_array, "[ERROR] -- Failed to allocate memory for id_mapping_array\n");

#if DEBUG_PRINT 
  fprintf(stderr, "Total num of regions:%d\n", (int32_t) region_array_size);
#endif

  for (i = 0; i < region_array_size; i++) {
    region_array[i].start_address             = (i == 0) ? start_addr_mem_pool() : (region_array[i - 1].start_address + (uint64_t) REGION_SIZE);
    region_array[i].used                      = 0;
    region_array[i].last_allocated_end        = region_array[i].start_address;
    region_array[i].last_allocated_start      = NULL;
    region_array[i].first_allocated_start     = NULL;
    region_array[i].dependency_list           = NULL;
#if ANONYMOUS
    region_array[i].size_mapped               = 0;
    region_array[i].offset_list               = NULL;
#endif
    region_array[i].rdd_id                    = _MAX_PARTITIONS * max_rdd_id;
    region_array[i].part_id                   = _MAX_PARTITIONS * max_rdd_id;
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
      if (region_array[j % (int32_t) region_array_size].last_allocated_end == 
          region_array[j % (int32_t) region_array_size].start_address) 
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
  #ifdef DBG_LOST_REGION
    mark_used(region_array[i].start_address, "new_region", -1);
  #else
    mark_used(region_array[i].start_address);
  #endif /* ifdef DBG_LOST_REGION */
    references(region_array[cur_region].start_address, region_array[i].start_address);
    // references(region_array[i].start_address, region_array[cur_region].start_address);
    region_array[i].last_allocated_start = region_array[cur_region].start_address;
    region_array[i].first_allocated_start = region_array[cur_region].start_address;
    region_array[i].last_allocated_end = region_array[cur_region].start_address + size;

#ifdef DBG_PROTECT_FREE_REGIONS
    // Enable accesses
    fprintf(stderr, "Enable permissions for region: %u\n", i);
    if (mprotect(region_array[i].start_address, REGION_SIZE, PROT_READ | PROT_WRITE) != 0) {
      fprintf(stderr, "mprotect error\n");
    }
#endif /* ifdef DBG_PROTECT_FREE_REGIONS */
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
          &region_array[((res + size) - region_array[0].start_address) /
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
    printf("Allocating from region %ld until region %ld\n",((res) - region_array[0].start_address) / ((uint64_t)REGION_SIZE),((res+size) - region_array[0].start_address) / ((uint64_t)REGION_SIZE));
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
          &region_array[((res + size) - region_array[0].start_address) /
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
    printf("Allocating from region %ld until region %ld\n",((res) - region_array[0].start_address) / ((uint64_t)REGION_SIZE),((res+size) - region_array[0].start_address) / ((uint64_t)REGION_SIZE));
#endif
    
    pthread_mutex_unlock(&id_mapping_array[id_index].mapping_lock);

    return res;
  }

#ifdef DBG_LOST_REGION
  mark_used(mapped_region->start_address, "allocate_to_region", -1);
#else
  mark_used(mapped_region->start_address);
#endif /* ifdef DBG_LOST_REGION */
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
  printf("Allocating from region %ld until region %ld\n",((id_array[id_index]->last_allocated_start) - region_array[0].start_address) / ((uint64_t)REGION_SIZE),((id_array[id_index]->last_allocated_start+size) - region_array[0].start_address) / ((uint64_t)REGION_SIZE));
#endif

  char *last_alloc_start = mapped_region->last_allocated_start;

  pthread_mutex_unlock(&id_mapping_array[id_index].mapping_lock);

  return last_alloc_start;
}

/*
 * region_cmp - Comparator for sll_contains(), sll_find() and sll_delete().
 *
 * Regions reside in a flat array (region_array) for their entire lifetime,
 * so pointer identity is logical identity: two pointers refer to the same
 * region if and only if they hold the same address.
 *
 * Follows the standard comparator convention: returns 0 when equal.
 */
static int region_cmp(const void *a, const void *b) {
  return (a != b);
}

/*
 * function that connects two regions in a tera_group
 * arguments:
 * - obj1: the object that references the other
 * - obj2 the object that is referenced
 */

void references(char *obj1, char *obj2) {
  int32_t seg1 = (obj1 - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  int32_t seg2 = (obj2 - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  if (seg1 >= region_array_size || seg2 >= region_array_size || seg1 < 0 || seg2 < 0)
    return;

  if (seg1 == seg2)
    return;

  // Dependency_list is empty
  if (!region_array[seg1].dependency_list) {
    region_array[seg1].dependency_list = sll_create(region_cmp);
    sll_push_front(region_array[seg1].dependency_list, &region_array[seg2]);
    return;
  }

  if (sll_contains(region_array[seg1].dependency_list, &region_array[seg2]))
    return;

  sll_push_front(region_array[seg1].dependency_list, &region_array[seg2]);
  if (region_array[seg1].used) {
#ifdef DBG_LOST_REGION
    mark_used(region_array[seg2].start_address, "references", -1);
#else
    mark_used(region_array[seg2].start_address);
#endif /* ifdef DBG_LOST_REGION */
  }
}

/*
 * Print the size of the dependency list of each region (if they have)
 */
void print_groups() {
  int32_t i;

  fprintf(stderr, "Groups:\n");

  for (i = 0; i < region_array_size; i++) {
    if (!region_array[i].dependency_list) {
      fprintf(stderr, "Region %d has 0 dependencies\n", i);
      continue;
    }

    fprintf(stderr, "Region %d has %lu dependencies\n", i, sll_size(region_array[i].dependency_list));
  }
}

/*
 * Resets the used field of all regions and groups
 */
void reset_used() {
  int32_t i;
  for (i = 0 ; i < region_array_size ; i++)
    region_array[i].used = 0;
}

/*
 * Marks the region that contains this obj as used and increases tera_group
 * counter (if it belongs to a tera_group)
 * Arguments: obj: the object that is alive
 */
#ifdef DBG_LOST_REGION
void mark_used(char *obj, char *from, uint gc_number) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);

	assertf(seg >= 0 && seg < region_array_size,
			"Segment index is out of range %lu", seg);
  if (region_array[seg].used == 1)
    return;

  region_array[seg].used = 1;
  
  if (!region_array[seg].dependency_list)
    return;

  SLLIterator it;
  sll_iter_init(region_array[seg].dependency_list, &it);

  while (sll_iter_has_next(&it)) {
    struct region *r = (struct region *)sll_iter_next(&it);
    mark_used(r->start_address, from, gc_number);
  }
}
#else
void mark_used(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);

  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);
  if (region_array[seg].used == 1)
    return;

  region_array[seg].used = 1;

  if (!region_array[seg].dependency_list)
    return;

  SLLIterator it;
  sll_iter_init(region_array[seg].dependency_list, &it);

  while (sll_iter_has_next(&it)) {
    struct region *r = (struct region *)sll_iter_next(&it);
    mark_used(r->start_address);
  }
}
#endif /* ifdef DBG_LOST_REGION */

#if STATISTICS
void print_statistics() {
  uint64_t wasted_space = 0;
  uint64_t total_regions = 0;
  int32_t i;

  for(i = 0 ; i < region_array_size ; i++ ) {
    if (region_array[i % region_array_size].last_allocated_end != region_array[i % region_array_size].start_address ) {
      total_regions++;
      if (region_array[i].last_allocated_end <= region_array[i].start_address + REGION_SIZE) { 
        wasted_space += (region_array[i].start_address + (uint64_t) REGION_SIZE) - region_array[i].last_allocated_end;
      }
    }
  }

  fprintf(stderr, "Total Wasted Space: %zu MBytes\n", wasted_space / (1024 * 1024));
  fprintf(stderr, "Total regions: %zu\n", total_regions);

  if (total_regions)
    fprintf(stderr, "Average wasted space: %zu KBytes\n", wasted_space / (1024 * total_regions));

  fprintf(stderr, "Total dependencies:%d\n", total_deps);
  fprintf(stderr, "Total time spent in allocate_to_region:%f ms\n", alloc_elapsedtime);
  fprintf(stderr, "Total time spent in free_regions:%f ms\n", free_elapsedtime);
}
#endif

/*
 * Frees all unused regions
 */
struct region_list* free_regions() {
#if STATISTICS
  struct timeval t1,t2;
  gettimeofday(&t1, NULL);
#endif
  int32_t i;
  struct region_list *head = NULL;
  reclaimed_regions_count = 0;
  for (i = 0; i < region_array_size; i++) {
    if (region_array[i].used == 0 && region_array[i].last_allocated_end != region_array[i].start_address) {
      sll_destroy(region_array[i].dependency_list);
      region_array[i].dependency_list = NULL;

      if (region_array[i].last_allocated_start >= region_array[i].start_address) {
        struct region_list *new_node = malloc(sizeof(struct region_list));
        new_node->region_start = region_array[i].start_address;
        new_node->last_allocated_start = region_array[i].last_allocated_start;
        new_node->last_allocated_end = region_array[i].last_allocated_end;
        new_node->next = head;
        head = new_node;
      }

      region_array[i].last_allocated_end = region_array[i].start_address;
      region_array[i].last_allocated_start = NULL;
      region_array[i].first_allocated_start = NULL;

#if ANONYMOUS 
      region_array[i].size_mapped = 0;
      struct offset *offset_ptr = region_array[i].offset_list;
      struct offset *temp = offset_ptr;

      while (offset_ptr != NULL) {
        temp = offset_ptr->next;
        offset_ptr->next = offset_list;
        offset_list = offset_ptr;
        offset_ptr = temp;
      }
      region_array[i].offset_list = NULL;
#endif
      if (id_mapping_array[get_id(region_array[i].rdd_id,
                                  region_array[i].part_id)].mapped_region ==
          &region_array[i]) {
        id_mapping_array[get_id(region_array[i].rdd_id,
                                region_array[i].part_id)].mapped_region = NULL;
      }

      region_array[i].rdd_id = _MAX_PARTITIONS * max_rdd_id;

      reclaimed_regions_count++;
    }
  }

#if STATISTICS
  print_statistics();
  gettimeofday(&t2, NULL);
  free_elapsedtime += (t2.tv_sec - t1.tv_sec) * 1000.0;
  free_elapsedtime += (t2.tv_usec - t1.tv_usec) / 1000.0;
#endif

  return head;
}

/*
 * Prints all the allocated regions
 */
void print_regions() {
  int32_t i;
  fprintf(stderr, "Regions:\n");

  for (i = 0; i < region_array_size ; i++) {
    if (region_array[i].last_allocated_end != region_array[i].start_address)
      fprintf(stderr, "Region %d\n",i);
  }
}

/*
 * Prints all the used regions
 */
void print_used_regions() {
  int32_t i;
  fprintf(stderr, "Used Regions:\n");

  for (i = 0 ; i < region_array_size ; i++) {
    if (region_array[i].used == 1)
      fprintf(stderr, "Region %d\n", i);
  }
}

/*
 * Checks if obj is before last object of region
 */
bool is_before_last_object(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return (obj >= region_array[seg].last_allocated_end) ? false : true;
}

/*
 * Returns last object of region
 */
char* get_last_object(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return region_array[seg].last_allocated_end;
}

// Returns true if object is first of its region false otherwise
bool is_region_start(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
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
void start_iterate_regions() {
	_next_region = 0;
}

/*
 * Return the next active region or NULL if we reached the end of the region
 * array.
 */
char* get_next_region() {
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

char *get_first_object(char *addr) {
  uint64_t seg = (addr - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size, "Segment index is out of range %lu", seg);
  return region_array[seg].first_allocated_start;
}

int get_num_of_continuous_regions(char *addr) {
  uint64_t seg = (addr - region_array[0].start_address) / ((uint64_t)REGION_SIZE);

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
uint64_t get_obj_group_id(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
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
uint64_t get_obj_part_id(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);

  return region_array[seg].part_id;
}

/*
 * Check if these two objects belong to the same tera_group
 *
 * obj1: address of the object
 * obj2: address of the object
 *
 * returns: 1 if objects are in the same tera_group, 0 otherwise
 */
int is_in_the_same_group(char *obj1, char *obj2) {
	uint64_t seg1 = (obj1 - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
	uint64_t seg2 = (obj2 - region_array[0].start_address) / ((uint64_t)REGION_SIZE);

	assertf(seg1 < region_array_size && seg2 < region_array_size && seg1 >= 0
         && seg2 >=0, "Segment index is out of range %lu, %lu", seg1, seg2);

	/* Objects belong to the same regions */
	if (seg1 == seg2)
		return 1;

  if (!region_array[seg1].dependency_list)
    return 0;

  return (sll_contains(region_array[seg1].dependency_list, &region_array[seg2]));
}

/*                                                                              
 * Get the total allocated regions                                              
 * Return the total number of allocated regions or zero, otherwise              
 */                                                                             
long total_allocated_regions() {                                                
	int32_t i;
	long counter = 0;

	for (i = 0; i < region_array_size; i++) {                                   
		if (region_array[i].last_allocated_end != region_array[i].start_address)
			counter++;
	}                                                                           

	return counter;
}

/*
 * Get the total number of used regions
 * Return the total number of used regions or zero, otherwise
 */
long total_used_regions() {
	int32_t i;
	long counter = 0;

	for (i = 0 ; i < region_array_size; i++) {
		if (region_array[i].used == 1)
			counter++;
	}
	return counter;
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

#ifdef DBG_PROTECT_FREE_REGIONS
void make_region_inaccessible(char *region_start, uint gc_number) {
  // Remove read/write permissions
  fprintf(stderr, "[%u] Remove permissions for region: %lu\n", gc_number, region_containing_addr(region_start));
  if (mprotect(region_start, REGION_SIZE, PROT_NONE) != 0) {
    fprintf(stderr, "mprotect error\n");
  }
}
#endif /* ifdef DBG_PROTECT_FREE_REGIONS */

uint64_t region_containing_addr(char *addr) {
  return (addr - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
}

int is_used(uint64_t region) {
  return region_array[region].used;
}

struct region *get_region(uint64_t region_index) {
  return &region_array[region_index];
}

#if PR_BUFFER

/*
 * Flush the promotion buffer of a certain region
 *
 * seg: Index of the region in region array
 *
 */
void flush_buffer(uint64_t seg) {
	struct pr_buffer *buf = region_array[seg].pr_buffer;

	if (buf->size == 0)
		return;

	assertf(buf->size <= PR_BUFFER_SIZE, "Sanity check");

	// Write the buffer to TeraHeap
	r_awrite(buf->buffer, buf->first_obj_addr, buf->size / HeapWordSize);

	buf->alloc_ptr = buf->buffer;
	buf->first_obj_addr = NULL;
	buf->size = 0;
}

/*
 * Add an obect to the promotion buffer. We use promotion buffer to avoid write
 * system calls for small sized objects.
 *
 * obj: Object that will be writter in the promotion buffer
 * new_adr: Is used to know where the first object in the promotion buffer will
 *			be move to H2
 * size: Size of the object
 */
void buffer_insert(char* obj, char* new_adr, size_t size) {
	uint64_t seg = (new_adr - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
	struct pr_buffer *buf = region_array[seg].pr_buffer;

  pthread_mutex_lock(&buf->buffer_lock);

	char*  start_adr  = buf->first_obj_addr;
	size_t cur_size   = buf->size;
	size_t free_space = PR_BUFFER_SIZE - cur_size;

	assertf(THRESHOLD < PR_BUFFER_SIZE, "Threshold should be less that promotion buffer size");

	if ((size * HeapWordSize) > THRESHOLD) {
    r_awrite(obj, new_adr, size);
    pthread_mutex_unlock(&buf->buffer_lock);
    return;
	}

	/* Allocate a buffer for the region and set buffer allocation ptr */
	if (buf->buffer == NULL) {
		buf->buffer = malloc(PR_BUFFER_SIZE * sizeof(char));
		buf->alloc_ptr = buf->buffer;
	}

	/* Case1: Buffer is empty */
	if (cur_size == 0) {
		assertf(start_adr == NULL, "Sanity check");

		memcpy(buf->alloc_ptr, obj, size * HeapWordSize);

		buf->first_obj_addr = new_adr;
		buf->alloc_ptr += size * HeapWordSize;
		buf->size = size * HeapWordSize;

    pthread_mutex_unlock(&buf->buffer_lock);

		return;
	}
	
	/* 
	 * Case2: Object size is grater than the available free space in buffer
	 * Case3: Object's new address is not contignious with the other objects - 
	 * object belongs to next addresses in the region
	 * In both cases we flush the buffer and allocate the object in a new
	 * buffer.
	 */
	if (((size * HeapWordSize) > free_space) || ((start_adr + cur_size) != new_adr)) {
		flush_buffer(seg);
		
		memcpy(buf->alloc_ptr, obj, size * HeapWordSize);

		buf->first_obj_addr = new_adr;
		buf->alloc_ptr += size * HeapWordSize;
		buf->size = size * HeapWordSize;

    pthread_mutex_unlock(&buf->buffer_lock);

		return;
	}
	
	memcpy(buf->alloc_ptr, obj, size * HeapWordSize);

	buf->alloc_ptr += size * HeapWordSize;
	buf->size += size * HeapWordSize;

  pthread_mutex_unlock(&buf->buffer_lock);
}

/*
 * Flush all active buffers and free each buffer memory. We need to free their
 * memory to limit waste space.
 */
void free_all_buffers() {
	uint64_t i;
	struct pr_buffer *buf;

  for (i = 0; i < region_array_size; i++) {
		buf = region_array[i].pr_buffer;

    pthread_mutex_lock(&buf->buffer_lock);

		/* Buffer is not empty, so flush it */
		if (buf->size != 0)
			flush_buffer(i);

		/* If the buffer is already flushed, just free bufffer's memory */
		if (buf->buffer != NULL) {
			free(buf->buffer);
			buf->buffer = NULL;
			buf->alloc_ptr = NULL;
			buf->first_obj_addr = NULL;
			buf->size = 0;
		}

    pthread_mutex_unlock(&buf->buffer_lock);
	}
}
#endif

bool object_starts_from_region(char *obj) {
  uint64_t seg = (obj - region_array[0].start_address) / ((uint64_t)REGION_SIZE);
  assertf(seg >= 0 && seg < region_array_size,
          "Segment index is out of range %lu", seg);
  return (region_array[seg].first_allocated_start != region_array[seg].start_address) ? false : true;
}

/* Returns the number of regions that were reclaimed (i.e., released back
to the free pool) during the most recent reclamation cycle. */
uint num_reclaimed_regions(void) {
  return reclaimed_regions_count;
}
