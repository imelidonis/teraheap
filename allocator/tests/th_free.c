/***************************************************
*
* file: th_free.c
*
* @Author:   Iacovos G. Kolokasis
* @Author:   Giannos Evdorou
* @Version:  29-11-2021
* @email:    kolokasis@ics.forth.gr
*
* Test to verify:
*       - allocator initialization
*       - object allocation in the correct positions
*       - the search for an empty region wraps around the end of H2
***************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h2_allocator.h"
#include "../src/regions.h"   // white-box: region counts
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define MB (1024LU * 1024)
#define GB 1024LU * 1024 * 1024

//this test needs 2MB region size
int main() {
  char *obj1;
  char *obj2;
  char *obj3;
  char *obj4;
  char *obj5;
  char *obj6;
  char *obj7;
  char *obj8;
  char *obj9;
  // Init allocator
  // One GC thread per label (thread_id = label): the regions are the same
  // whether the objects are grouped by label or by GC thread
  h2_init(CARD_SIZE * PAGE_SIZE, "/mnt/fmap/", 64 * GB, 256 * MB, 4);

  //obj1 should be in region 0
  obj1 = h2_allocate(1, 0, 0, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj1);

  //obj2 should be in region 1 
  obj2 = h2_allocate(200, 1, 1, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj2);

  //obj3 should be in region 0
  obj3 = h2_allocate(12020, 0, 0, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj3);

  //obj4 should be in region 2 
  obj4 = h2_allocate(262140, 2, 2, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj4);

  //obj5 should be in region 1
  obj5 = h2_allocate(4, 1, 1, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj5);

  //obj6 should be in region 0 
  obj6 = h2_allocate(200, 0, 0, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj6);

  //obj7 should be in region 3 
  obj7 = h2_allocate(262140, 1, 1, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj7);

  //obj8 should be in region 4 
  obj8 = h2_allocate(500, 3, 3, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj8);

  //obj9 should be in region 5 
  obj9 = h2_allocate(500, 2, 2, H2_GROUP_LABEL);
  fprintf(stderr, "Allocate: %p\n", obj9);

  //region 0 and region 1 grouped
  h2_references(obj1, obj2);
  //region 2 added to tera_group
  h2_references(obj3, obj4);

  h2_print_groups();

  //nothing should be done, obj4 and obj5 are in the same tera_group
  h2_references(obj4, obj5);
  h2_print_groups();
  //region 3 added to tera_group
  h2_references(obj7, obj6);
  h2_print_groups();
  //new tera_group with region 4 and 5
  h2_references(obj8, obj9);
  h2_print_groups();

  h2_reset_used();
  h2_mark_used(obj1);
  h2_mark_used(obj6);
  h2_mark_used(obj8);

  //nothing should be freed because all regions belong to the same tera_group 
  h2_free_regions(NULL, NULL);
  CHECK(total_allocated_regions() == 4, "Number of allocated regions is incorrect");

  print_regions();
  h2_print_groups();

  h2_reset_used();
  h2_mark_used(obj1);
  h2_mark_used(obj6);
  CHECK(total_used_regions() == 3,
          "Number of used regions is incorrect %lu", total_used_regions());

  h2_free_regions(NULL, NULL);
  CHECK(total_allocated_regions() == 3,
          "Number of allocated regions is incorrect %lu", total_used_regions());
  print_regions();
  h2_print_groups();

  h2_reset_used();
  CHECK(total_used_regions() == 0, "Number of used regions is incorrect");
  //all regions should be freed
  h2_free_regions(NULL, NULL);
  CHECK(total_allocated_regions() == 0,
          "Number of allocated regions is incorrect");
  print_regions();
  h2_print_groups();

  // Fill H2 with objects of one region each: the search for empty regions
  // starts after the last region allocated above and wraps around the end
  uint64_t n = region_array_size;
  char **objs = malloc(n * sizeof(char *));
  char *seen = calloc(n, 1);
  for (uint64_t k = 0; k < n; k++) {
    objs[k] = h2_allocate(256 * MB / 8, k % 4, k % 4, H2_GROUP_LABEL);
    uint64_t r = h2_region_containing_addr(objs[k]);
    CHECK(r < n, "object %lu in region %lu, H2 has %lu regions", k, r, n);
    CHECK(!seen[r], "objects in region %lu twice", r);
    seen[r] = 1;
  }
  CHECK(total_allocated_regions() == n, "%lu regions allocated, expected %lu",
        total_allocated_regions(), n);

  // Free the first 10 regions: the next objects wrap around again to them
  h2_reset_used();
  for (uint64_t k = 0; k < n; k++)
    if (h2_region_containing_addr(objs[k]) >= 10)
      h2_mark_used(objs[k]);
  h2_free_regions(NULL, NULL);
  CHECK(total_allocated_regions() == n - 10, "%lu regions allocated after the free, expected %lu",
        total_allocated_regions(), n - 10);
  for (uint64_t k = 0; k < 10; k++) {
    uint64_t r = h2_region_containing_addr(h2_allocate(256 * MB / 8, 0, 0, H2_GROUP_LABEL));
    CHECK(r < 10, "object in region %lu, expected one of the freed regions 0-9", r);
  }
  free(objs);
  free(seen);

  printf("--------------------------------------\n");
  printf("TH_Free:\t\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");

  return 0;
}
