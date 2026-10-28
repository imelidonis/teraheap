
/***************************************************
*
* file: th_allocate.c
*
* @Author:   Iacovos G. Kolokasis
* @Author:   Giannos Evdorou
* @Version:  09-03-2021
* @email:    kolokasis@ics.forth.gr
*
* Test to verify:
*       - allocator initialization
*       - object allocation in the correct positions
*       - an object bigger than a region takes whole contiguous regions
*
* Usage: th_allocate_multi_regions.bin [h2_dir] [region size in MB]
* (default 256 MB)
***************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h2_allocator.h"
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define SIZE_30MB (3932160)
#define SIZE_5MB (655360)
#define SIZE_1MB (131072)
#define SIZE_2MB (262144)

#define MB (1024LU * 1024)
#define GB 1024LU * 1024 * 1024

int main(int argc, char **argv) {
  char *obj1, *obj2, *obj3, *obj4, *obj5, *obj6;
  uint64_t region = (argc > 2 ? strtoul(argv[2], NULL, 10) : 256) * MB;

  // Init allocator
  h2_init(CARD_SIZE * PAGE_SIZE, "/mnt/fmap/", 64 * GB, region, 4, 1, false);
  CHECK(h2_region_size() == region, "region size %lu, expected %lu", h2_region_size(), region);
  fprintf(stderr, "Region size: %lu MB\n", region / MB);

  //obj1 should be in region 0
  obj1 = h2_allocate(1, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj1);

  //obj2 should be in region 1 
  obj2 = h2_allocate(200, 1, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj2);

  //obj3 should be in region 0
  obj3 = h2_allocate(SIZE_5MB, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj3);

  // An object bigger than a region starts a run of whole regions
  uint64_t obj3_bytes = SIZE_5MB * 8;
  if (obj3_bytes > region) {
    CHECK((uint64_t) (obj3 - h2_start_addr_mem_pool()) % region == 0,
          "obj3 %p does not start a region", obj3);
    CHECK(h2_get_num_of_continuous_regions(obj3) == (int) ((obj3_bytes + region - 1) / region),
          "obj3 covers %d regions, expected %lu", h2_get_num_of_continuous_regions(obj3),
          (obj3_bytes + region - 1) / region);
  }

  obj4 = h2_allocate(SIZE_1MB, 2, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj4);

  obj5 = h2_allocate(10, 2, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj5);

  obj6 = h2_allocate(10, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj6);
  

  h2_reset_used();
  h2_mark_used(obj3);
  h2_mark_used(obj4);
  h2_mark_used(obj6);
  
  //obj2 should be in region 1 
  obj2 = h2_allocate(64000, 10, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj2);

  h2_free_regions(NULL, NULL);

  obj3 = h2_allocate(SIZE_2MB, 4, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj3);
  
  h2_free_regions(NULL, NULL);
  
  obj3 = h2_allocate(SIZE_1MB, 4, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj3);

  //h2_free_regions(NULL, NULL);

  return 0;
}
