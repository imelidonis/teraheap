// Freeing the regions that are not used (h2_free_regions()).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "regions.h"
#include "region_types.h"
#include "sharedDefines.h"
#include "shadow.h"

static uint reclaimed_regions_count;
int         protect_regions_on_free = 0;

/* Remove all access to a freed region (h2_protect_regions_on_free()) */
static void make_region_inaccessible(char *region_start) {
  fprintf(stderr, "Remove permissions for region: %lu\n", h2_region_containing_addr(region_start));
  if (mprotect(region_start, REGION_SIZE, PROT_NONE) != 0) {
    fprintf(stderr, "mprotect error\n");
  }
}

/*
 * Frees all unused regions
 */
void h2_free_regions(h2_region_free_cb cb, void *ctx) {
#if STATISTICS
  struct timeval t1,t2;
  gettimeofday(&t1, NULL);
#endif
  int32_t i;
  reclaimed_regions_count = 0;
  // At a safepoint: no thread reads the tables replaced by growth any more
  dep_free_retired();
  for (i = 0; i < region_array_size; i++) {
    if (region_array[i].used == 0 && region_array[i].last_allocated_end != region_array[i].start_address) {
      dep_set_free(region_array[i].deps);
      region_array[i].deps = NULL;

      // Report regions that held objects, after they are fully freed
      char *const last_start = region_array[i].last_allocated_start;
      char *const last_end = region_array[i].last_allocated_end;
      const bool report = last_start >= region_array[i].start_address;

      region_array[i].last_allocated_end = region_array[i].start_address;
      region_array[i].last_allocated_start = NULL;
      region_array[i].first_allocated_start = NULL;

      if (id_mapping_array[get_id(region_array[i].rdd_id,
                                  region_array[i].part_id)].mapped_region ==
          &region_array[i]) {
        id_mapping_array[get_id(region_array[i].rdd_id,
                                region_array[i].part_id)].mapped_region = NULL;
      }

      region_array[i].rdd_id = _MAX_PARTITIONS -1;
      region_array[i].part_id = _MAX_PARTITIONS -1;

      // Its shadow chunks are dead: detach them without writing
      shadow_free_range(region_array[i].start_address, REGION_SIZE);

      if (report) {
        if (cb != NULL)
          cb(region_array[i].start_address, last_start, last_end, ctx);
        if (protect_regions_on_free)
          make_region_inaccessible(region_array[i].start_address);
      }

      reclaimed_regions_count++;
    }
  }

#if STATISTICS
  print_statistics();
  gettimeofday(&t2, NULL);
  free_elapsedtime += (t2.tv_sec - t1.tv_sec) * 1000.0;
  free_elapsedtime += (t2.tv_usec - t1.tv_usec) / 1000.0;
#endif
}

/* Returns the number of regions that were reclaimed (i.e., released back
to the free pool) during the most recent reclamation cycle. */
uint h2_num_reclaimed_regions(void) {
  return reclaimed_regions_count;
}

void h2_protect_regions_on_free(int should_protect) {
  protect_regions_on_free = should_protect;
}
