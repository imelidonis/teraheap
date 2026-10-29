// Statistics and prints of the regions.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "regions.h"
#include "region_types.h"

/* A region that continues an object which starts in an earlier region */
static inline bool continues_object(const struct region *r) {
  return r->first_allocated_start != r->start_address;
}

void h2_get_alloc_stats(struct h2_alloc_stats *s) {
  *s = (struct h2_alloc_stats) { .regions_total = region_array_size };

  for (uint64_t i = 0; i < region_array_size; i++) {
    const struct region *r = &region_array[i];
    if (region_is_empty(r))
      continue;
    s->regions_allocated++;

    // Only the last region of an object (or a region of small objects)
    // ends inside the region
    char *end = r->start_address + region_size;
    if (r->last_allocated_end <= end)
      s->tail_bytes += end - r->last_allocated_end;

    uint64_t deps = region_dep_count(r);
    if (continues_object(r)) {
      s->span_links++;
    } else {
      // The first region of an object depends on the regions it continues into
      uint64_t spans = 0;
      while (i + 1 + spans < region_array_size &&
             region_array[i + 1 + spans].first_allocated_start == r->start_address)
        spans++;
      if (spans > 0)
        s->span_objects++;
      deps = (deps > spans) ? deps - spans : 0;
    }

    s->dep_links += deps;
    if (deps > s->dep_max)
      s->dep_max = deps;
    if (deps > 0)
      s->regions_with_deps++;
  }
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
void h2_print_used_regions() {
  int32_t i;
  fprintf(stderr, "Used Regions:\n");

  for (i = 0 ; i < region_array_size ; i++) {
    if (region_array[i].used == 1)
      fprintf(stderr, "Region %d\n", i);
  }
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
