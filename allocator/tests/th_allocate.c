
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
#include <stdlib.h>
#include <string.h>
#include "h2_allocator.h"
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))
#define GB 1024LU * 1024 * 1024

//this test needs 256MB region size
int main() {
  char *obj1, *obj2, *obj3, *obj4, *obj5, *obj6, *obj7, *obj8, *obj9;

  // Init allocator
  // One GC thread per group (thread_id = rdd_id): the regions are the same
  // whether the allocator groups by GC thread or by rdd
  h2_init(CARD_SIZE * PAGE_SIZE, "/mnt/fmap/", 64 * GB, 4, 4, false);

  //obj1 should be in region 0
  obj1 = h2_allocate(1, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj1);
  CHECK((obj1 - h2_start_addr_mem_pool()) == 0, "Object start position");

  //obj2 should be in region 1 
  obj2 = h2_allocate(200, 1, 0, 1);
  fprintf(stderr, "Allocate: %p\n", obj2);
  CHECK((obj2 - obj1)/8 == 33554432, "Object start position %zu", (obj2 - obj1) / 8); 

  //obj3 should be in region 0
  obj3 = h2_allocate(12020, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj3);
  CHECK((obj3 - obj1)/8 == 1, "Object start position");

  //obj4 should be in region 2 
  obj4 = h2_allocate(262140, 2, 0, 2);
  fprintf(stderr, "Allocate: %p\n", obj4);
  CHECK((obj4 - obj2)/8 == 33554432, "Object start position %zu", (obj4 - obj2) / 8);

  //obj5 should be in region 1
  obj5 = h2_allocate(4, 1, 0, 1);
  fprintf(stderr, "Allocate: %p\n", obj5);
  CHECK((obj5 - obj2)/8 == 200, "Object start position");

  //obj6 should be in region 0 
  obj6 = h2_allocate(200, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj6);
  CHECK((obj6 - obj3)/8 == 12020, "Object start position");

  //obj7 should be in region 3 
  obj7 = h2_allocate(262140, 1, 0, 1);
  fprintf(stderr, "Allocate: %p\n", obj7);
  CHECK((obj7 - obj5)/8 == 4, "Object start position %zu", (obj7 - obj5) / 8);

  //obj8 should be in region 4 
  obj8 = h2_allocate(500, 3, 0, 3);
  fprintf(stderr, "Allocate: %p\n", obj8);
  CHECK((obj8 - obj4)/8 == 33554432, "Object start position");

  //obj9 should be in region 5 
  obj9 = h2_allocate(500, 2, 0, 2);
  fprintf(stderr, "Allocate: %p\n", obj9);
  CHECK((obj9 - obj4)/8 == 262140, "Object start position");

  printf("--------------------------------------\n");
  printf("TH_Allocate:\t\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");

  return 0;
}
