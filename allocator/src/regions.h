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
extern uint64_t max_rdd_id; //< Total different rdds

/* Defined in regions.c, set by h2_init(): region size (a power of two) and
 * its log2 */
extern uint64_t region_size;
extern unsigned region_shift;

struct region;
struct dep_set;

/* ---- regions.c ---- */

/*
 * Initialize region array, tera_group array and their fields
 */
void init_regions(uint64_t partitions);

/*
 * Returns the address of the allocated object
 * Arguments:
 * - size: the size of the object in Bytes
 * - rdd_id: The id of the rdd which the object belongs
 * - part_id: The id of the partition that the object belongs
 */
char* allocate_to_region(size_t size, uint64_t rdd_id, uint64_t partition_id);

uint64_t get_id(uint64_t rdd_id, uint64_t partition_id);

/*
 * For the shadow chunks (shadow.c, through shadow_init()): the allocation
 * top of the region that contains addr, and whether a (thread, partition) id
 * still allocates in it.
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
