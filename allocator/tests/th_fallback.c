/***************************************************
*
* file: th_fallback.c
*
* Test of the allocation when H2 has no empty region: the object goes to
* the region of another mapping of its label (another GC thread, or the
* label's shared region), and the JVM exits only if none has room.
*   - H2 of 8 regions, 2 GC threads: label slots 0 and 1
*   - the next object of label 0 goes to the other thread's region
*   - an object of label 0 with H2_GROUP_LABEL goes to a thread's region
*   - label 1 (no region with room): exit (in a child process)
*   - two GC threads in the fallback at the same time: no overlaps
*   - after regions are freed, empty regions are used again
*
* Usage: th_fallback.bin [H2 dir (default /mnt/fmap/)]
***************************************************/

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "h2_allocator.h"
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define MB (1024LU * 1024)
#define GB (1024LU * MB)

#define NTHREADS 2
#define REGION (256 * MB)
#define REGION_WORDS (REGION / 8)
#define NREGIONS 8
#define NOBJS 20000           // per thread in the concurrent part

static uint64_t region(char *p) { return h2_region_containing_addr(p); }

static uint64_t fallbacks(void) {
  struct h2_alloc_stats s;
  h2_get_alloc_stats(&s);
  return s.fallback_allocs;
}

struct par_arg { int t; char **objs; size_t *words; };

static void *par_alloc(void *a) {
  struct par_arg *g = a;
  uint64_t s = 0x9E3779B97F4A7C15ULL * (g->t + 1);
  for (int k = 0; k < NOBJS; k++) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    g->words[k] = 2 + s % 64;
    g->objs[k] = h2_allocate(g->words[k], 0, g->t, H2_GROUP_LABEL_THREAD);
  }
  return NULL;
}

struct span { char *start; size_t bytes; };

static int by_start(const void *a, const void *b) {
  const struct span *x = a, *y = b;
  return x->start < y->start ? -1 : x->start > y->start;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "/mnt/fmap/";
  h2_init(CARD_SIZE * PAGE_SIZE, dir, NREGIONS * REGION, REGION, NTHREADS);

  // Label 0: one region per thread
  char *r0 = h2_allocate(1, 0, 0, H2_GROUP_LABEL_THREAD);
  char *r1 = h2_allocate(1, 0, 1, H2_GROUP_LABEL_THREAD);
  CHECK(region(r0) != region(r1), "threads 0 and 1 share region %lu", region(r0));

  // The other regions: objects of one region each (no active region)
  for (int i = 0; i < NREGIONS - 2; i++)
    h2_allocate(REGION_WORDS, 1, 0, H2_GROUP_LABEL_THREAD);

  // Thread 0's region: 10 words left
  char *f = h2_allocate(REGION_WORDS - 11, 0, 0, H2_GROUP_LABEL_THREAD);
  CHECK(region(f) == region(r0), "filler in region %lu, expected %lu", region(f), region(r0));
  CHECK(fallbacks() == 0, "%lu fallback allocations, expected 0", fallbacks());

  // H2 is full: thread 0's next object goes to thread 1's region
  char *x = h2_allocate(100, 0, 0, H2_GROUP_LABEL_THREAD);
  CHECK(x == r1 + 8, "object at %p, expected %p (thread 1's region)", x, r1 + 8);
  CHECK(h2_get_obj_label(x) == 0, "label %lu, expected 0", h2_get_obj_label(x));
  CHECK(fallbacks() == 1, "%lu fallback allocations, expected 1", fallbacks());

  // The shared region of label 0 has none: a thread's region of the label
  char *y = h2_allocate(10, 0, 1, H2_GROUP_LABEL);
  CHECK(y == f + (REGION_WORDS - 11) * 8, "object at %p, expected %p (thread 0's region)",
        y, f + (REGION_WORDS - 11) * 8);
  CHECK(fallbacks() == 2, "%lu fallback allocations, expected 2", fallbacks());

  // No region of label 1 or of the GC threads has room: exit
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    h2_allocate(1, 1, 1, H2_GROUP_LABEL_THREAD);
    _exit(0);   // not reached
  }
  int st;
  waitpid(pid, &st, 0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == EXIT_FAILURE, "label 1: child status %d, expected exit(1)", st);
  pid = fork();
  if (pid == 0) {
    h2_allocate(1, 5, 0, H2_GROUP_THREAD);
    _exit(0);   // not reached
  }
  waitpid(pid, &st, 0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == EXIT_FAILURE, "GC thread: child status %d, expected exit(1)", st);

  // Both threads at the same time: thread 0 through the fallback, thread 1
  // in its own region
  pthread_t th[NTHREADS];
  struct par_arg ga[NTHREADS];
  for (int t = 0; t < NTHREADS; t++) {
    ga[t] = (struct par_arg){t, malloc(NOBJS * sizeof(char *)), malloc(NOBJS * sizeof(size_t))};
    pthread_create(&th[t], NULL, par_alloc, &ga[t]);
  }
  for (int t = 0; t < NTHREADS; t++)
    pthread_join(th[t], NULL);

  struct span *sp = malloc(NTHREADS * NOBJS * sizeof *sp);
  for (int t = 0; t < NTHREADS; t++)
    for (int k = 0; k < NOBJS; k++) {
      sp[t * NOBJS + k] = (struct span){ga[t].objs[k], ga[t].words[k] * 8};
      CHECK(region(ga[t].objs[k]) == region(r1), "thread %d: object in region %lu, expected %lu",
            t, region(ga[t].objs[k]), region(r1));
    }
  qsort(sp, NTHREADS * NOBJS, sizeof *sp, by_start);
  for (int k = 1; k < NTHREADS * NOBJS; k++)
    CHECK(sp[k - 1].start + sp[k - 1].bytes <= sp[k].start, "objects %p and %p overlap",
          sp[k - 1].start, sp[k].start);
  CHECK(fallbacks() == 2 + NOBJS, "%lu fallback allocations, expected %d", fallbacks(), 2 + NOBJS);

  // Free every region but thread 1's: empty regions are used again
  h2_reset_used();
  h2_mark_used(r1);
  h2_free_regions(NULL, NULL);
  CHECK(h2_num_reclaimed_regions() == NREGIONS - 1, "%u regions freed, expected %d",
        h2_num_reclaimed_regions(), NREGIONS - 1);
  char *z = h2_allocate(1, 0, 0, H2_GROUP_LABEL_THREAD);
  CHECK(region(z) != region(r1), "after the free, thread 0's object is in thread 1's region");
  CHECK(fallbacks() == 2 + NOBJS, "%lu fallback allocations after the free, expected %d",
        fallbacks(), 2 + NOBJS);

  printf("--------------------------------------\n");
  printf("TH_Fallback:\t\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");
  return 0;
}
