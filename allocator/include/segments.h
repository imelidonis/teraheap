#ifndef __SEGMENTS_H__
#define __SEGMENTS_H__

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>

#include "h2_allocator.h"

#include "h2_config.h"

/*
 * TODO: restructure the allocator. The code and the API are not well
 * structured any more: region dependencies (dependency sets, liveness
 * marking, freeing), region allocation and the per-thread state are mixed
 * in segments.c/mem_pool.c, and the API mixes object-level calls
 * (h2_references(), add_reference(), h2_is_in_the_same_group()) with region
 * indices and raw region_array access. Split them into clear modules with
 * one documented API before adding more features.
 */

struct region;

/*
 * Initialize region array, tera_group array and their fields
 */
void init_regions(uint64_t partitions);

/*
 * Finds an empty regions and returns its starting address
 * Arguments:
 * - size: the size of the object we want to allocate in
 *         Bytes
 */
char* new_region(size_t size);

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
 * Returns an empty position of the group_array
 */
int new_group();

/*
 * Merges two groups of regions that already exist
 * Arguments:
 * - group1: the id of the first tera_group
 * - group2: the id of the second tera_group
 */
void merge_groups(int group1, int group2);

/*
 * Same as h2_references(), and returns 1 if this call added the dependency
 * (0 if it existed, or both objects are in the same region). Lock-free:
 * GC threads may call it in parallel.
 */
int add_reference(char *obj1, char *obj2);

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

/*
 * For the shadow chunks (shadow.c): the allocation top of the region that
 * contains addr, and whether a (thread, partition) id still allocates in it.
 */
void region_info(char *addr, char **top, bool *active);

/*
 * For the shadow chunks: the protection to map a chunk of a freed region with
 * (PROT_NONE if regions are protected on free and the region is still free).
 */
int region_free_prot(char *addr);

void print_objects_temporary_function(char *obj,const char *string);

/*
 * Get objects 'obj' region start address
 */
char* get_region_start_addr(char *obj, uint64_t rdd_id, uint64_t part_id);

char* top_in_last_region();

struct region *get_region(uint64_t region_index);

// Returns the start_address of a region
char *region_get_start_address(struct region *region);

// Returns the end address of last allocation of a region
char *region_get_last_allocated_end(struct region *region);

#endif
