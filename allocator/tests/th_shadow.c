/***************************************************
*
* file: th_shadow.c
*
* Test of the SH shadow chunks (include/shadow.h) through the allocator:
*   - "mixed GCs": GC threads allocate objects with the shadow active and
*     write them (like the GC copy); then pause end
*   - between pauses: a mutator writes words of live objects (1/16 of them
*     with read() from a pipe, i.e. kernel-mode writes) while the writer
*     thread writes chunks back
*   - each pause starts with h2_shadow_switch() (mutator stopped)
*   - "Full GCs": objects written through the promotion buffers / r_awrite
*     with the shadow inactive (redirect into OPEN chunks)
*   - "remark": regions freed with h2_free_regions() while their chunks are in
*     any state
* At the end every word of every live object is checked in memory and, for
* objects in switched chunks, through the file with pread().
*
* Usage: th_shadow.bin [H2 dir (default /mnt/fmap/)] [cycles (default 40)]
***************************************************/
#define _GNU_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../include/sharedDefines.h"
#include "../include/regions.h"
#include "../include/shadow.h"

#define MB (1024LU * 1024)
#define GB (1024LU * MB)
#define NTHREADS 4
#define NPARTS 2
#define POOL (64 * MB)               // small, so the fallback to the file is used too
#define PER_PAUSE (24 * MB)          // bytes per GC thread per mixed GC
#define MAX_OBJS (1 << 20)

extern int fd;                       // mem_pool.c: the H2 file

struct obj {
  char *addr;
  size_t words;
  uint64_t gen;                      // pattern seed
  uint64_t mut_val;                  // last value written by the mutator at word words/2
  int mutated;
  int live;
};

static struct obj *objs;
static volatile long nobjs;
static pthread_mutex_t objs_lock = PTHREAD_MUTEX_INITIALIZER;

static inline uint64_t pattern(const char *a, uint64_t gen) {
  uint64_t x = (uint64_t) (uintptr_t) a * 0x9E3779B97F4A7C15ULL ^ gen;
  x ^= x >> 29;
  return x;
}

static uint64_t rnd(uint64_t *s) {
  *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17;
  return *s;
}

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void add_obj(char *a, size_t words, uint64_t gen) {
  pthread_mutex_lock(&objs_lock);
  if (nobjs >= MAX_OBJS) { fprintf(stderr, "too many objects\n"); exit(1); }
  objs[nobjs] = (struct obj){.addr = a, .words = words, .gen = gen, .live = 1};
  nobjs++;
  pthread_mutex_unlock(&objs_lock);
}

static size_t obj_words(uint64_t *s) {
  uint64_t r = rnd(s) % 1000;
  if (r < 900) return 2 + rnd(s) % 512;          // up to 4 KB
  if (r < 995) return 512 + rnd(s) % 8192;       // up to 64 KB
  return (1 * MB + rnd(s) % (3 * MB)) / 8;       // 1-4 MB: spans chunks
}

// ---- one "mixed GC" ----
struct gc_arg { int t; uint64_t gen; };
static void *gc_thread(void *a) {
  struct gc_arg *g = a;
  uint64_t s = 0x1234567ULL * (g->t + 1) + g->gen;
  for (size_t done = 0; done < PER_PAUSE;) {
    size_t w = obj_words(&s);
    char *p = h2_allocate(w, g->t, rnd(&s) % NPARTS, g->t);
    uint64_t *q = (uint64_t *) p;
    for (size_t i = 0; i < w; i++) q[i] = pattern(p + i * 8, g->gen);
    add_obj(p, w, g->gen);
    done += w * 8;
  }
  return NULL;
}

// ---- writer and mutator ----
static volatile int stop_writer, mut_run, mut_stop, mut_acked;
static void *writer(void *a) {
  (void) a;
  while (!stop_writer)
    if (shadow_writeback_step() == 0) usleep(500);
  return NULL;
}

static long mut_writes, mut_kwrites;
static void *mutator(void *a) {
  (void) a;
  int p[2];
  if (pipe(p)) { perror("pipe"); exit(1); }
  uint64_t s = 99991;
  while (!mut_stop) {
    if (!mut_run) { mut_acked = 1; usleep(100); continue; }
    mut_acked = 0;
    for (int k = 0; k < 64 && mut_run; k++) {
      long n = nobjs;
      if (n == 0) break;
      struct obj *o = &objs[rnd(&s) % n];
      if (!o->live || o->words < 2) continue;
      uint64_t v = rnd(&s) | 1;
      uint64_t *slot = (uint64_t *) o->addr + o->words / 2;
      if ((mut_writes & 15) == 0) {       // kernel-mode write
        if (write(p[1], &v, 8) != 8 || read(p[0], slot, 8) != 8) { perror("pipe io"); exit(1); }
        mut_kwrites++;
      } else {
        *(volatile uint64_t *) slot = v;
      }
      o->mut_val = v; o->mutated = 1; mut_writes++;
    }
    usleep(50);
  }
  close(p[0]); close(p[1]);
  return NULL;
}

static void stop_mutator(void) {   // "safepoint"
  mut_run = 0;
  while (!mut_acked) usleep(50);
}

// ---- checks ----
// h2_free_regions() callback: count the freed regions
static void count_freed(char *start, char *last_start, char *last_end, void *ctx) {
  (void) start; (void) last_start; (void) last_end;
  (*(int *) ctx)++;
}

static uint64_t expected(struct obj *o, size_t i) {
  return (o->mutated && i == o->words / 2) ? o->mut_val : pattern(o->addr + i * 8, o->gen);
}

// File offset of addr from /proc/self/maps (the VMA that maps it)
static long file_offset(char *addr) {
  FILE *f = fopen("/proc/self/maps", "r");
  char l[512]; unsigned long lo, hi, off; long res = -1;
  while (fgets(l, sizeof l, f))
    if (sscanf(l, "%lx-%lx %*s %lx", &lo, &hi, &off) == 3 && (unsigned long) addr >= lo && (unsigned long) addr < hi) {
      res = (long) (off + ((unsigned long) addr - lo));
      break;
    }
  fclose(f);
  return res;
}

static long verify(int use_file, long *checked_file) {
  long bad = 0; *checked_file = 0;
  size_t maxw = 0;
  for (long k = 0; k < nobjs; k++) if (objs[k].live && objs[k].words > maxw) maxw = objs[k].words;
  uint64_t *buf = malloc(maxw * 8 + 16);
  for (long k = 0; k < nobjs; k++) {
    struct obj *o = &objs[k];
    if (!o->live) continue;
    uint64_t *q = (uint64_t *) o->addr;
    for (size_t i = 0; i < o->words; i++)
      if (q[i] != expected(o, i)) {
        if (bad < 5) fprintf(stderr, "MEM mismatch obj %ld word %zu at %p: %" PRIx64 " != %" PRIx64 "\n",
                             k, i, (void *) (o->addr + i * 8), q[i], expected(o, i));
        bad++;
      }
    if (!use_file) continue;
    // Through the file, for objects entirely in file-backed chunks
    int in_file = 1;
    for (size_t b = 0; b < o->words * 8; b += SHADOW_CHUNK / 2) if (h2_shadow_is_attached(o->addr + b)) in_file = 0;
    if (h2_shadow_is_attached(o->addr + o->words * 8 - 1)) in_file = 0;
    if (!in_file) continue;
    long off = file_offset(o->addr);
    if (off < 0 || pread(fd, buf, o->words * 8, off) != (ssize_t) (o->words * 8)) {
      fprintf(stderr, "pread failed for obj %ld\n", k); bad++; continue;
    }
    for (size_t i = 0; i < o->words; i++)
      if (buf[i] != expected(o, i)) {
        if (bad < 5) fprintf(stderr, "FILE mismatch obj %ld word %zu at %p\n", k, i, (void *) (o->addr + i * 8));
        bad++;
      }
    (*checked_file)++;
  }
  free(buf);
  return bad;
}

// ---- "Full GC": objects written through the promotion buffers ----
static void full_gc_writes(uint64_t gen) {
  uint64_t s = gen * 7 + 3;
  uint64_t *src = malloc(64 * 1024 * 8);
  for (int k = 0; k < 64; k++) {
    int t = rnd(&s) % NTHREADS;
    size_t w = 2 + rnd(&s) % 4096;
    char *p = h2_allocate(w, t, rnd(&s) % NPARTS, t);
    for (size_t i = 0; i < w; i++) src[i] = pattern(p + i * 8, gen);
    h2_buffer_insert((char *) src, p, w);   // copies src into the buffer
    add_obj(p, w, gen);
  }
  h2_free_all_buffers();
  while (!h2_areq_completed());
  h2_update_allocator_global_state();
  free(src);
}

// ---- "remark": free one region that has live test objects ----
// shadow_only: pick a region with attached chunks
static int free_one_region(uint64_t *s, int shadow_only) {
  long n = nobjs;
  if (n == 0) return 0;
  char *victim = NULL;
  for (int tries = 0; tries < 1000 && !victim; tries++) {
    struct obj *o = &objs[rnd(s) % n];
    if (o->live && (!shadow_only || h2_shadow_is_attached(o->addr))) victim = o->addr;
  }
  if (!victim) return 0;
  char *rstart = h2_start_addr_mem_pool() + ((victim - h2_start_addr_mem_pool()) / REGION_SIZE) * REGION_SIZE;
  char *rend = rstart + REGION_SIZE;
  h2_reset_used();
  int dropped = 0;
  for (long k = 0; k < n; k++) {
    struct obj *o = &objs[k];
    if (!o->live) continue;
    // An object that starts in another region but reaches into the victim
    // keeps it (multi-region objects)
    if (o->addr >= rstart && o->addr < rend) { o->live = 0; dropped++; }
    else if (o->addr < rstart && o->addr + o->words * 8 > rstart) { h2_reset_used(); return 0; }
  }
  for (long k = 0; k < n; k++) if (objs[k].live) h2_mark_used(objs[k].addr);
  int freed = 0;
  h2_free_regions(count_freed, &freed);
  return freed ? dropped : 0;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "/mnt/fmap/";
  int cycles = argc > 2 ? atoi(argv[2]) : 40;
  objs = calloc(MAX_OBJS, sizeof *objs);

  h2_init(SHADOW_CHUNK, dir, 8 * GB, NPARTS, NTHREADS);
  if (h2_shadow_init(POOL, 3)) { fprintf(stderr, "FAIL: shadow_init\n"); return 1; }

  pthread_t wt, mt;
  pthread_create(&wt, NULL, writer, NULL);
  pthread_create(&mt, NULL, mutator, NULL);

  uint64_t s = 424242;
  double sw_ms = 0; long frees = 0, dropped = 0, fulls = 0, preattached = 0;
  for (int c = 0; c < cycles; c++) {
    stop_mutator();
    uint32_t sw = h2_shadow_switch(0, 512);
    struct h2_shadow_stats st; h2_shadow_get_stats(&st);
    sw_ms += st.last_switch_ms;

    if (c % 7 == 6) { full_gc_writes(1000 + c); fulls++; }      // Full GC
    if (c % 5 == 4) { int d = free_one_region(&s, 0); if (d) { frees++; dropped += d; } }   // remark

    h2_shadow_set_active(true);
    pthread_t th[NTHREADS]; struct gc_arg ga[NTHREADS];
    for (int t = 0; t < NTHREADS; t++) { ga[t] = (struct gc_arg){t, (uint64_t) c + 1}; pthread_create(&th[t], NULL, gc_thread, &ga[t]); }
    for (int t = 0; t < NTHREADS; t++) pthread_join(th[t], NULL);
    h2_update_allocator_global_state();
    h2_shadow_set_active(false);
    h2_shadow_pause_end();

    // "remark" while the writer works on the new FULL chunks
    if (c % 3 == 2) { usleep((c % 4) * 1000); int d = free_one_region(&s, 1); if (d) { frees++; dropped += d; } }
    // Pre-attach ahead of each thread's last allocation (outside the pause)
    if (c % 2 == 1)
      for (int t = 0; t < NTHREADS; t++)
        for (long k = nobjs - 1; k >= 0; k--)
          if (objs[k].live && objs[k].gen == (uint64_t) c + 1 && (objs[k].addr - h2_start_addr_mem_pool()) / REGION_SIZE % NTHREADS == (uint64_t) t) {
            preattached += shadow_preattach(objs[k].addr + objs[k].words * 8, 4 * MB);
            break;
          }

    h2_shadow_get_stats(&st);
    printf("cycle %2d switched=%u (%.1f ms, delta %" PRIu64 ") open=%u full=%u writing=%u ready=%u dead=%u free=%u/%u objs=%ld\n",
           c, sw, st.last_switch_ms, st.last_delta_pages, st.n_open, st.n_full, st.n_writing, st.n_ready, st.n_dead,
           st.free_chunks, st.pool_chunks, nobjs);
    mut_run = 1;
    usleep(150 * 1000);   // mutator phase
  }

  // Final: stop the mutator, let the writer finish, switch everything
  stop_mutator();
  struct h2_shadow_stats st;

  // A region retired with pre-attached chunks it never used: they must
  // return to the pool at pause end. Thread 0, partition 1.
  long empty_before; h2_shadow_get_stats(&st); empty_before = st.empty_detached;
  while (h2_shadow_switch(0, -1) == 0 && st.free_chunks < 8) { usleep(2000); h2_shadow_get_stats(&st); }
  {
    char *p = h2_allocate(2, 0, 1, 0);
    ((uint64_t *) p)[0] = pattern(p, 7777); ((uint64_t *) p)[1] = pattern(p + 8, 7777);
    add_obj(p, 2, 7777);
    char *top = p + 16;
    char *rend = h2_start_addr_mem_pool() + ((top - h2_start_addr_mem_pool()) / REGION_SIZE + 1) * REGION_SIZE;
    int pre = shadow_preattach(top, 6 * MB);
    size_t w = (rend - top) / 8 + 1;   // does not fit: the region retires
    char *q = h2_allocate(w, 0, 1, 0);
    for (size_t i = 0; i < w; i++) ((uint64_t *) q)[i] = pattern(q + i * 8, 7777);
    add_obj(q, w, 7777);
    h2_update_allocator_global_state();
    h2_shadow_pause_end();
    h2_shadow_get_stats(&st);
    printf("retired-region check: preattached %d, returned %ld, new object in another region: %s\n",
           pre, st.empty_detached - empty_before, (q - h2_start_addr_mem_pool()) / REGION_SIZE != (top - h2_start_addr_mem_pool()) / REGION_SIZE ? "yes" : "no");
    // Other retired regions may return chunks at the same pause end, so
    // check the pre-attached chunks themselves
    int still = 0;
    // (the chunk that holds top may be an OPEN chunk from a mixed GC: it
    // stays attached, now FULL)
    char *first = h2_start_addr_mem_pool() + ((top - h2_start_addr_mem_pool() + SHADOW_CHUNK - 1) / SHADOW_CHUNK) * SHADOW_CHUNK;
    for (char *c = first; c < top + 6 * MB && c < rend; c += SHADOW_CHUNK) if (h2_shadow_is_attached(c)) still++;
    if (pre == 0 || still || st.empty_detached - empty_before < pre) { fprintf(stderr, "FAIL: unused chunks of a retired region not returned (%d still attached)\n", still); return 1; }
  }
  double t0 = now_ms();
  for (;;) {
    h2_shadow_get_stats(&st);
    if (st.n_full == 0 && st.n_writing == 0 && st.n_dead == 0) break;
    if (now_ms() - t0 > 60000) { fprintf(stderr, "FAIL: writer did not finish\n"); return 1; }
    usleep(1000);
  }
  h2_shadow_switch(0, -1);
  mut_stop = 1; pthread_join(mt, NULL);
  stop_writer = 1; pthread_join(wt, NULL);

  long checked_file, bad = verify(1, &checked_file);
  h2_shadow_get_stats(&st);
  long live = 0; for (long k = 0; k < nobjs; k++) live += objs[k].live;
  printf("objects %ld (live %ld, %ld checked through the file), mutator writes %ld (kernel %ld)\n",
         nobjs, live, checked_file, mut_writes, mut_kwrites);
  printf("attached %" PRIu64 " (failed %" PRIu64 "), written back %" PRIu64 ", precopy pages %" PRIu64 ", switched %" PRIu64
         " (skipped %" PRIu64 ", delta pages %" PRIu64 "), dead %" PRIu64 ", empty detached %" PRIu64 ", redirected %" PRIu64 " KB\n",
         st.attached, st.attach_failed, st.written_back, st.precopy_pages, st.switched, st.switch_skipped,
         st.delta_pages, st.dead, st.empty_detached, st.redirected_bytes / 1024);
  printf("full gcs %ld, freed regions %ld (%ld objects dropped), avg switch %.2f ms, still open %u, preattached %ld\n",
         fulls, frees, dropped, sw_ms / cycles, st.n_open, preattached);
  printf("dead chunks by state when freed: open %" PRIu64 " full %" PRIu64 " writing %" PRIu64 " ready %" PRIu64 " (in flight %" PRIu64 ")\n",
         st.dead_by_state[1], st.dead_by_state[2], st.dead_by_state[3], st.dead_by_state[4], st.dead_in_flight);
  int ok = bad == 0 && checked_file > 0 && st.dead > 0 && st.redirected_bytes > 0 && st.switched > 0;
  printf("%s (%ld mismatches)\n", ok ? "PASS" : "FAIL", bad);
  return ok ? 0 : 1;
}
