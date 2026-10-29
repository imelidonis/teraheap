// Freeing the regions that are not used (h2_free_regions()).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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
  if (mprotect(region_start, region_size, PROT_NONE) != 0) {
    fprintf(stderr, "mprotect error\n");
  }
}

/*
 * Free region r: drop its dependencies, its shadow chunks and its (rdd,
 * partition) mapping, then report it to cb and protect it if it held
 * objects.
 */
static void free_region(struct region *r, h2_region_free_cb cb, void *ctx) {
  dep_set_free(r->deps);
  r->deps = NULL;

  // Report regions that held objects, after they are fully freed
  char *const last_start = r->last_allocated_start;
  char *const last_end = r->last_allocated_end;
  const bool report = last_start >= r->start_address;

  r->last_allocated_end = r->start_address;
  r->last_allocated_start = NULL;
  r->first_allocated_start = NULL;

  struct id_to_reg_mapping *m = &id_mapping_array[get_id(r->rdd_id, r->part_id)];
  if (m->mapped_region == r)
    m->mapped_region = NULL;

  r->rdd_id = _MAX_PARTITIONS - 1;
  r->part_id = _MAX_PARTITIONS - 1;

  // Its shadow chunks are dead: detach them without writing
  shadow_free_range(r->start_address, region_size);

  if (report) {
    if (cb != NULL)
      cb(r->start_address, last_start, last_end, ctx);
    if (protect_regions_on_free)
      make_region_inaccessible(r->start_address);
  }
}

/*
 * Frees all unused regions
 */
void h2_free_regions(h2_region_free_cb cb, void *ctx) {
  reclaimed_regions_count = 0;
  // At a safepoint: no thread reads the tables replaced by growth any more
  dep_free_retired();
  for (uint64_t i = 0; i < region_array_size; i++) {
    struct region *r = &region_array[i];
    if (r->used == 0 && !region_is_empty(r)) {
      free_region(r, cb, ctx);
      reclaimed_regions_count++;
    }
  }
}

/* Returns the number of regions that were reclaimed (i.e., released back
to the free pool) during the most recent reclamation cycle. */
uint h2_num_reclaimed_regions(void) {
  return reclaimed_regions_count;
}

void h2_protect_regions_on_free(int should_protect) {
  protect_regions_on_free = should_protect;
}
