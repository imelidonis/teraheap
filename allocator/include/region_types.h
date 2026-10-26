#ifndef __REGION_TYPES_H__
#define __REGION_TYPES_H__

/*
 * The region array (internal): the types and globals shared by regions.c,
 * region_deps.c, reclaim.c, promo_buffer.c and stats.c.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "h2_config.h"

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
  size_t size;					/* Current size of the buffer */
};
#endif

/*
 * The struct for regions
 */
#define CACHE_LINE_SIZE 64

struct dep_set;   // lock-free set of region dependencies (region_deps.c)

struct region {
    char *start_address;
    char *last_allocated_end;
    char *last_allocated_start;
    char *first_allocated_start;
    // The regions this region references: a lock-free hash set (struct
    // dep_set, region_deps.c)
    struct dep_set *deps;

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
} __attribute__((aligned(CACHE_LINE_SIZE)));   // one region per cache line (see init_regions())

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

/* Defined in regions.c */
extern uint64_t _MAX_PARTITIONS;
extern struct region *region_array;
// Start address of region 0 (the start of H2), used to compute region
// indices. A copy of region_array[0].start_address that is never written
// after init, so it does not share a cache line with region 0's
// allocation fields, which are written on every allocation.
extern char *region_array_base;
extern struct id_to_reg_mapping *id_mapping_array;
extern struct offset *offset_list;

/* Defined in reclaim.c */
extern int protect_regions_on_free;

#if STATISTICS
/* Defined in stats.c */
extern double alloc_elapsedtime;
extern double free_elapsedtime;
#endif

static inline void check_allocation_failure(void *ptr, const char *msg) {
  if (!ptr) {
    perror(msg);
    exit(EXIT_FAILURE);
  }
}

#endif /* __REGION_TYPES_H__ */
