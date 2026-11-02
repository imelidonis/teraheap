/***************************************************
*
* file: th_promo_buffer.c
*
* White-box test of the promotion buffers (h2_buffer_insert()), as in a Full
* GC: the H2 space of all objects is allocated first, then the objects are
* inserted.
*   - two regions written in turn: each region's buffer is freed right after
*     the object that ends at its allocation top (a small object, or a big
*     one written directly)
*   - objects of a region inserted out of order (H2 was full, two threads
*     shared the region): the buffer is freed after the last object, a new
*     one takes the objects that come later, h2_free_all_buffers() frees it
*   - every object is in the H2 file at the end
*
* Usage: th_promo_buffer.bin [H2 dir (default /mnt/fmap/)]
***************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h2_allocator.h"
#include "../src/region_types.h"   // white-box: the regions' promotion buffers
#include "test_util.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

#define MB (1024LU * 1024)
#define GB (1024LU * MB)

#define N 200                   // small objects per region
#define BIG_WORDS (2 * MB / 8)  // more than THRESHOLD: written directly

struct obj { char *addr; size_t words; };

static uint64_t pattern(const char *a) {
  uint64_t x = (uint64_t) (uintptr_t) a * 0x9E3779B97F4A7C15ULL;
  return x ^ (x >> 29);
}

static uint64_t *src;

static void insert(struct obj o) {
  for (size_t i = 0; i < o.words; i++)
    src[i] = pattern(o.addr + i * 8);
  h2_buffer_insert((char *) src, o.addr, o.words);
}

static int has_buffer(struct obj o) {
  return region_of(o.addr)->pr_buffer->buffer != NULL;
}

static void check_written(struct obj *v, int n, const char *what) {
  for (int k = 0; k < n; k++)
    for (size_t i = 0; i < v[k].words; i++)
      CHECK(((uint64_t *) v[k].addr)[i] == pattern(v[k].addr + i * 8),
            "%s: object %d, word %zu differs in H2", what, k, i);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "/mnt/fmap/";
  h2_init(CARD_SIZE * PAGE_SIZE, dir, 4 * GB, 256 * MB, 2);
  src = malloc(BIG_WORDS * 8);

  // Allocate, as the Full GC prepare phase: labels 0 and 1 of thread 0 in
  // turn. Region A ends with a big object, region B with a small one
  struct obj a[N + 1], b[N];
  uint64_t s = 12345;
  for (int k = 0; k < N; k++) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    a[k].words = 2 + s % 512;
    a[k].addr = h2_allocate(a[k].words, 0, 0, H2_GROUP_LABEL_THREAD);
    b[k].words = 2 + s % 300;
    b[k].addr = h2_allocate(b[k].words, 1, 0, H2_GROUP_LABEL_THREAD);
  }
  a[N].words = BIG_WORDS;
  a[N].addr = h2_allocate(a[N].words, 0, 0, H2_GROUP_LABEL_THREAD);
  CHECK(region_of(a[N].addr) == region_of(a[0].addr), "the big object is not in region A");
  CHECK(region_of(a[0].addr) != region_of(b[0].addr), "labels 0 and 1 share a region");

  // Write, as the compaction phase: in allocation order
  for (int k = 0; k < N; k++) {
    insert(a[k]);
    CHECK(has_buffer(a[0]), "region A: no buffer after object %d of %d", k, N + 1);
    insert(b[k]);
    if (k < N - 1)
      CHECK(has_buffer(b[0]), "region B: no buffer after object %d of %d", k, N);
  }
  CHECK(!has_buffer(b[0]), "region B: buffer not freed after its last object");
  CHECK(has_buffer(a[0]), "region A: no buffer before its last (big) object");
  insert(a[N]);
  CHECK(!has_buffer(a[0]), "region A: buffer not freed after its last (big) object");

  // Out of order: region C's last object first
  struct obj c[N];
  for (int k = 0; k < N; k++) {
    c[k].words = 2 + k % 100;
    c[k].addr = h2_allocate(c[k].words, 2, 1, H2_GROUP_LABEL_THREAD);
  }
  insert(c[N - 1]);
  CHECK(!has_buffer(c[0]), "region C: buffer not freed after its last object");
  for (int k = 0; k < N - 1; k++)
    insert(c[k]);
  CHECK(has_buffer(c[0]), "region C: no buffer for the objects after the last one");

  // End of the Full GC
  h2_free_all_buffers();
  CHECK(!has_buffer(c[0]), "region C: buffer not freed by h2_free_all_buffers()");
  while (!h2_areq_completed())
    ;

  check_written(a, N + 1, "region A");
  check_written(b, N, "region B");
  check_written(c, N, "region C");

  printf("--------------------------------------\n");
  printf("TH_Promo_Buffer:\t\t\033[1;32m[PASS]\033[0m\n");
  printf("--------------------------------------\n");
  return 0;
}
