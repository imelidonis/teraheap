
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
***************************************************/

#include <stdint.h>
#include <stdio.h>
#include "../include/sharedDefines.h"
#include "../include/regions.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define SIZE_30MB (3932160)
#define SIZE_5MB (655360)
#define SIZE_1MB (131072)
#define SIZE_2MB (262144)

#define GB 1024LU * 1024 * 1024

//this test needs 256MB region size
int main() {
  char *obj1, *obj2, *obj3, *obj4, *obj5, *obj6;

  // Init allocator
  h2_init(CARD_SIZE * PAGE_SIZE, "/mnt/fmap/", 64 * GB, 4, 1);

  //obj1 should be in region 0
  obj1 = h2_allocate(1, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj1);

  //obj2 should be in region 1 
  obj2 = h2_allocate(200, 1, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj2);

  //obj3 should be in region 0
  obj3 = h2_allocate(SIZE_5MB, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj3);

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
