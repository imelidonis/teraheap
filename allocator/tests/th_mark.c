/***************************************************
*
* file: th_mark.c
*
* Test to verify that h2_mark_used() marks every region reachable through
* the region dependencies:
*       - a chain longer than the mark stack kept on the C stack
*       - a cycle
*       - a region with many dependencies
*       - several threads marking at the same time
***************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "h2_allocator.h"
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define GB 1024LU * 1024 * 1024

#define NOBJ      240   // one object per region (each has its own label)
#define CHAIN     200   // obj[0] -> obj[1] -> ... -> obj[CHAIN - 1]
#define CYCLE     CHAIN // obj[CYCLE] -> obj[CYCLE + 1] -> obj[CYCLE + 2] -> obj[CYCLE]
#define FAN       (CYCLE + 3) // obj[FAN] -> obj[FAN + 1 .. NOBJ - 1]
#define NTHREADS  4
#define ROUNDS    100

static char *obj[NOBJ];

static int used(int i) {
  return h2_is_used(h2_region_containing_addr(obj[i]));
}

// Regions [from, to) are used and all others are not
static void check_used_range(int from, int to, const char *what) {
  for (int i = 0; i < NOBJ; i++) {
    int expected = (i >= from && i < to);
    CHECK(used(i) == expected, "%s: region of obj[%d] used = %d, expected %d",
          what, i, used(i), expected);
  }
}

static void *mark_chain_part(void *arg) {
  int start = *(int *) arg;
  h2_mark_used(obj[start]);
  return NULL;
}

int main() {
  // One partition: each label (rdd id) gets its own region
  h2_init(CARD_SIZE * PAGE_SIZE, "/mnt/fmap/", 64 * GB, 1, 1, false);

  for (int i = 0; i < NOBJ; i++)
    obj[i] = h2_allocate(1, i, 0, 0);

  for (int i = 1; i < NOBJ; i++)
    CHECK(h2_region_containing_addr(obj[i]) != h2_region_containing_addr(obj[i - 1]),
          "obj[%d] and obj[%d] share a region", i - 1, i);

  for (int i = 0; i + 1 < CHAIN; i++)
    h2_references(obj[i], obj[i + 1]);

  h2_references(obj[CYCLE], obj[CYCLE + 1]);
  h2_references(obj[CYCLE + 1], obj[CYCLE + 2]);
  h2_references(obj[CYCLE + 2], obj[CYCLE]);

  for (int i = FAN + 1; i < NOBJ; i++)
    h2_references(obj[FAN], obj[i]);

  // Chain longer than the mark stack on the C stack
  h2_reset_used();
  h2_mark_used(obj[0]);
  check_used_range(0, CHAIN, "chain");

  // Marking again changes nothing
  h2_mark_used(obj[0]);
  check_used_range(0, CHAIN, "chain again");

  // Cycle, entered in the middle
  h2_reset_used();
  h2_mark_used(obj[CYCLE + 1]);
  check_used_range(CYCLE, CYCLE + 3, "cycle");

  // Many dependencies of one region
  h2_reset_used();
  h2_mark_used(obj[FAN]);
  check_used_range(FAN, NOBJ, "fan");

  // Threads mark parts of the chain at the same time
  for (int r = 0; r < ROUNDS; r++) {
    pthread_t th[NTHREADS];
    int start[NTHREADS];

    h2_reset_used();
    for (int t = 0; t < NTHREADS; t++) {
      start[t] = t * (CHAIN / NTHREADS);
      CHECK(pthread_create(&th[t], NULL, mark_chain_part, &start[t]) == 0, "pthread_create");
    }
    for (int t = 0; t < NTHREADS; t++)
      pthread_join(th[t], NULL);

    check_used_range(0, CHAIN, "threads");
  }

  printf("--------------------------------------\n");
  printf("TH_mark:\t\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");

  return 0;
}
