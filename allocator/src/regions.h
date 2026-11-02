#ifndef __REGIONS_H__
#define __REGIONS_H__

/*
 * The H2 regions (internal). The region array and allocation are in
 * regions.c, the region dependencies and liveness in region_deps.c, freeing
 * in reclaim.c, the promotion buffers in promo_buffer.c and the statistics
 * and prints in stats.c. The types are in region_types.h.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>

#include "h2_allocator.h"
#include "h2_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in mem_pool.c */
extern uint64_t region_array_size;
extern uint64_t max_labels; //< Label slots in the id mapping array (h2_init())

/* Defined in regions.c, set by h2_init(): region size (a power of two) and
 * its log2 */
extern uint64_t region_size;
extern unsigned region_shift;

struct region;
struct dep_set;

/* ---- regions.c ---- */

/*
 * Initialize the region array and the mapping array (max_labels label
 * slots, for n_threads GC threads)
 */
void init_regions(uint64_t n_threads);

/*
 * Returns the address of the allocated object
 * Arguments:
 * - size: the size of the object in Bytes
 * - label: The label of the object (from the application)
 * - map_id: The mapping to allocate with (map_id_of())
 */
char* allocate_to_region(size_t size, uint64_t label, uint64_t map_id);

/* At a safepoint, after regions were freed: new_region() searches again */
void regions_freed(void);

/* Objects placed in another mapping's region because H2 was full */
uint64_t fallback_alloc_count(void);

/* Index in the mapping array of the region key of `group` */
uint64_t map_id_of(enum h2_group group, uint64_t label, uint64_t thread_id);

/*
 * For the shadow chunks (shadow.c, through shadow_init()): the allocation
 * top of the region that contains addr, and whether a mapping still
 * allocates in it.
 */
void region_info(char *addr, char **top, bool *active);

/*
 * For the shadow chunks (shadow.c, through shadow_init()): the protection to
 * map a chunk of a freed region with (PROT_NONE if regions are protected on
 * free and the region is still free).
 */
int region_free_prot(char *addr);

char* top_in_last_region();

/* ---- region_deps.c ---- */

/* At a safepoint (h2_free_regions()): free the set of a region */
void dep_set_free(struct dep_set *s);

/* At a safepoint (h2_free_regions()): free the tables replaced by growth */
void dep_free_retired(void);

/* At a safepoint: number of regions in the dependency set of a region */
uint64_t region_dep_count(const struct region *r);

/* ---- stats.c ---- */

/**
 * Get the total number of allocated regions
 * Return the total number of allocated regions or zero, otherwise
 */
long total_allocated_regions();

/*
 * Prints all the allocated regions
 */
void print_regions();

/*
 * Get the total number of used regions
 * Return the number of used regions or zero, otherwise
 */
long total_used_regions();

#ifdef __cplusplus
}
#endif

#endif /* __REGIONS_H__ */
