// Statistics and prints of the regions.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../include/regions.h"
#include "../include/region_types.h"

#if STATISTICS
double		  alloc_elapsedtime = 0.0;
double		  free_elapsedtime = 0.0;
#endif

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

  fprintf(stderr, "Total time spent in allocate_to_region:%f ms\n", alloc_elapsedtime);
  fprintf(stderr, "Total time spent in free_regions:%f ms\n", free_elapsedtime);
}
#endif

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
