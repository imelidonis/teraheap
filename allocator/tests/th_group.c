/***************************************************
*
* file: th_group.c
*
* @Author:   Iacovos G. Kolokasis
* @Author:   Giannos Evdorou
* @Version:  29-11-2021
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

//this test needs 2MB region size
int main() {
  char *obj1, *obj2, *obj3; char *obj4; char *obj5; char *obj6;
  char *obj7;
  char *obj8;
  char *obj9;
  // Init allocator
  h2_init(CARD_SIZE * PAGE_SIZE, "/mnt/fmap/", 64 * GB, 4, 1);

  //obj1 should be in region 0
  obj1 = h2_allocate(1, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj1);

  //obj2 should be in region 1 
  obj2 = h2_allocate(200, 1, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj2);

  //obj3 should be in region 0
  obj3 = h2_allocate(12020, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj3);

  //obj4 should be in region 2 
  obj4 = h2_allocate(262140, 2, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj4);

  //obj5 should be in region 1
  obj5 = h2_allocate(4, 1, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj5);

  //obj6 should be in region 0 
  obj6 = h2_allocate(200, 0, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj6);

  //obj7 should be in region 3 
  obj7 = h2_allocate(262140, 1, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj7);

  //obj8 should be in region 4 
  obj8 = h2_allocate(500, 3, 0, 0);
  fprintf(stderr, "Allocate: %p\n", obj8);

  //obj9 should be in region 5 
  obj9 = h2_allocate(500, 2, 0, 0);
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

  printf("--------------------------------------\n");
  printf("TH_Group:\t\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");

  return 0;
}
