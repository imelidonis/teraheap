/***************************************************
*
* file: th_label.c
*
* Test of how h2_allocate() groups objects in regions (enum h2_group):
*   - H2_GROUP_LABEL_THREAD: one region per (label, GC thread)
*   - H2_GROUP_LABEL: one region per label, shared by the GC threads
*   - H2_GROUP_THREAD: one region per GC thread, labels mixed
*   - the modes never share a region
*   - every region of an object larger than a region has its label
*   - labels of up to 32 bits are kept
*   - GC threads allocating at the same time: no overlaps, and with
*     H2_GROUP_LABEL_THREAD each region has objects of one thread only
*
* Usage: th_label.bin [H2 dir (default /mnt/fmap/)]
***************************************************/

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "h2_allocator.h"
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define MB (1024LU * 1024)
#define GB (1024LU * MB)

#define NTHREADS 4
#define REGION (256 * MB)
#define NREGIONS (64 * GB / REGION)
// Label slots: regions / (GC threads + 1), see h2_allocate()
#define LABEL_SLOTS (NREGIONS / (NTHREADS + 1))

#define NOBJS 20000       // per thread in the concurrent part

static uint64_t region(char *p) { return h2_region_containing_addr(p); }

struct par_arg {
  int t;
  enum h2_group group;
  uint64_t label;
  char **objs;
  size_t *words;
};

static void *par_alloc(void *a) {
  struct par_arg *g = a;
  uint64_t s = 0x9E3779B97F4A7C15ULL * (g->t + 1);
  for (int k = 0; k < NOBJS; k++) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    size_t w = 2 + s % 64;
    g->words[k] = w;
    g->objs[k] = h2_allocate(w, g->label, g->t, g->group);
  }
  return NULL;
}

struct span { char *start; size_t bytes; int t; };

static int by_start(const void *a, const void *b) {
  const struct span *x = a, *y = b;
  return x->start < y->start ? -1 : x->start > y->start;
}

// NTHREADS threads allocate NOBJS objects each with `label` and `group` at
// the same time. Returns the objects sorted by address.
static struct span *run_parallel(enum h2_group group, uint64_t label) {
  pthread_t th[NTHREADS];
  struct par_arg ga[NTHREADS];
  for (int t = 0; t < NTHREADS; t++) {
    ga[t] = (struct par_arg){t, group, label, malloc(NOBJS * sizeof(char *)), malloc(NOBJS * sizeof(size_t))};
    pthread_create(&th[t], NULL, par_alloc, &ga[t]);
  }
  for (int t = 0; t < NTHREADS; t++)
    pthread_join(th[t], NULL);

  struct span *sp = malloc(NTHREADS * NOBJS * sizeof *sp);
  for (int t = 0; t < NTHREADS; t++) {
    for (int k = 0; k < NOBJS; k++)
      sp[t * NOBJS + k] = (struct span){ga[t].objs[k], ga[t].words[k] * 8, t};
    free(ga[t].objs);
    free(ga[t].words);
  }
  qsort(sp, NTHREADS * NOBJS, sizeof *sp, by_start);

  for (int k = 1; k < NTHREADS * NOBJS; k++)
    CHECK(sp[k - 1].start + sp[k - 1].bytes <= sp[k].start, "objects %p and %p overlap",
          sp[k - 1].start, sp[k].start);
  for (int k = 0; k < NTHREADS * NOBJS; k++)
    CHECK(h2_get_obj_label(sp[k].start) == label, "object %p has label %lu, expected %lu",
          sp[k].start, h2_get_obj_label(sp[k].start), label);
  return sp;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "/mnt/fmap/";
  h2_init(CARD_SIZE * PAGE_SIZE, dir, 64 * GB, REGION, NTHREADS);

  // (label, GC thread): one region per pair
  char *a = h2_allocate(1, 3, 0, H2_GROUP_LABEL_THREAD);
  char *b = h2_allocate(1, 3, 1, H2_GROUP_LABEL_THREAD);
  char *c = h2_allocate(1, 3, 0, H2_GROUP_LABEL_THREAD);
  char *d = h2_allocate(1, 4, 0, H2_GROUP_LABEL_THREAD);
  CHECK(region(a) != region(b), "label 3: threads 0 and 1 share region %lu", region(a));
  CHECK(c == a + 8, "label 3, thread 0: second object at %p, expected %p", c, a + 8);
  CHECK(region(d) != region(a), "thread 0: labels 3 and 4 share region %lu", region(a));
  CHECK(h2_get_obj_label(a) == 3 && h2_get_obj_label(b) == 3 && h2_get_obj_label(d) == 4,
        "labels %lu %lu %lu, expected 3 3 4", h2_get_obj_label(a), h2_get_obj_label(b), h2_get_obj_label(d));

  // Labels in the same slot share the regions of a thread
  char *a2 = h2_allocate(1, 3 + LABEL_SLOTS, 0, H2_GROUP_LABEL_THREAD);
  CHECK(a2 == c + 8, "labels 3 and %lu (same slot), thread 0: object at %p, expected %p",
        3 + LABEL_SLOTS, a2, c + 8);

  // Label: the GC threads share the region of a label
  char *e = h2_allocate(1, 5, 0, H2_GROUP_LABEL);
  char *f = h2_allocate(1, 5, 2, H2_GROUP_LABEL);
  CHECK(f == e + 8, "label 5: thread 2's object at %p, expected %p", f, e + 8);
  CHECK(h2_get_obj_label(e) == 5, "label %lu, expected 5", h2_get_obj_label(e));

  // GC thread: the labels share the region of a thread; the region has the
  // label of its first object
  char *g = h2_allocate(1, 6, 1, H2_GROUP_THREAD);
  char *h = h2_allocate(1, 7, 1, H2_GROUP_THREAD);
  char *i = h2_allocate(1, 6, 2, H2_GROUP_THREAD);
  CHECK(h == g + 8, "thread 1: label 7's object at %p, expected %p", h, g + 8);
  CHECK(region(i) != region(g), "label 6: threads 1 and 2 share region %lu", region(g));
  CHECK(h2_get_obj_label(h) == 6, "thread 1's region has label %lu, expected 6", h2_get_obj_label(h));

  // The modes never share a region
  char *j = h2_allocate(1, 5, 0, H2_GROUP_LABEL_THREAD);
  char *k = h2_allocate(1, 3, 0, H2_GROUP_LABEL);
  CHECK(region(j) != region(e), "label 5: H2_GROUP_LABEL_THREAD and H2_GROUP_LABEL share a region");
  CHECK(region(k) != region(a), "label 3: H2_GROUP_LABEL and H2_GROUP_LABEL_THREAD share a region");
  CHECK(region(g) != region(a) && region(g) != region(e), "H2_GROUP_THREAD shares a region");

  // An object larger than a region: each of its regions has its label
  size_t big = (2 * REGION + 100 * MB) / 8;
  char *l = h2_allocate(big, 9, 3, H2_GROUP_LABEL_THREAD);
  for (int r = 0; r < 3; r++)
    CHECK(h2_get_obj_label(l + r * REGION) == 9, "region %d of the big object has label %lu, expected 9",
          r, h2_get_obj_label(l + r * REGION));
  char *m = h2_allocate(1, 9, 3, H2_GROUP_LABEL_THREAD);
  CHECK(region(m) < region(l) || region(m) > region(l + big * 8 - 1),
        "an object was allocated after the big object in region %lu", region(m));

  // Labels of 24 and 32 bits (in label slots not used above: 40 and 0)
  char *n = h2_allocate(1, 0xABCE14, 0, H2_GROUP_LABEL);
  char *o = h2_allocate(1, 0xFFFFFFFFULL, 1, H2_GROUP_LABEL_THREAD);
  CHECK(h2_get_obj_label(n) == 0xABCE14, "label %lx, expected abce14", h2_get_obj_label(n));
  CHECK(h2_get_obj_label(o) == 0xFFFFFFFFULL, "label %lx, expected ffffffff", h2_get_obj_label(o));

  // GC threads at the same time, one shared label region
  free(run_parallel(H2_GROUP_LABEL, 20));

  // GC threads at the same time, (label, GC thread): one thread per region
  struct span *sp = run_parallel(H2_GROUP_LABEL_THREAD, 21);
  for (int q = 1; q < NTHREADS * NOBJS; q++)
    if (region(sp[q].start) == region(sp[q - 1].start))
      CHECK(sp[q].t == sp[q - 1].t, "threads %d and %d allocated in region %lu",
            sp[q - 1].t, sp[q].t, region(sp[q].start));
  free(sp);

  printf("--------------------------------------\n");
  printf("TH_Label:\t\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");
  return 0;
}
