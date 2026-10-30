// SH shadow chunks with deferred write-back. See include/shadow.h.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "shadow.h"
#include "sharedDefines.h"
#include "h2_config.h"

#define CHUNK SHADOW_CHUNK
#define PAGE 4096UL
#define PRECOPY_MIN_PAGES 64      // stop pre-copy when a round finds fewer pages
#define REFILL_PER_STEP 8         // pool chunks populated per writer step

enum { CS_FILE = 0, CS_OPEN, CS_FULL, CS_WRITING, CS_READY, CS_DEAD };

// One record per pool slot; a slot holds one chunk, attached or free
struct rec {
  uint64_t idx;        // chunk index in the H2 range (when attached)
  uint64_t seq;        // order in which chunks became FULL (writer FIFO)
  uint8_t  used;       // attached
  uint8_t  armed;      // registered with userfaultfd and write-protected
  uint8_t  in_flight;  // the writer works on it without the lock
};

static bool s_on;
static volatile bool s_active;
static int s_fd = -1, s_uffd = -1, s_pagemap = -1;
static char *s_map_start, *s_h2_start, *s_h2_end;
static int s_precopy_rounds;

static uint8_t *s_state;          // per chunk of [s_h2_start, s_h2_end)
static uint64_t s_nchunks;
static uint16_t *s_region_busy;   // attached chunks per allocator region
static uint64_t s_nregions;
static uint64_t s_region_size;   // allocator region size (shadow_init())
static unsigned s_region_shift;   // log2(s_region_size)

static char *s_slots;             // reserved, 2 MB aligned pool area
static uint32_t s_pool_cap, s_pool_n, s_nfree;
static uint32_t *s_free;          // free slot stack
static struct rec *s_recs;        // per slot
static int64_t *s_skip;           // per GC thread: last chunk left to the file
static int s_nthreads;
static uint64_t s_seq;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static struct h2_shadow_stats s_st;

// Writer thread
static pthread_t s_writer;
static pthread_mutex_t s_wake_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_wake = PTHREAD_COND_INITIALIZER;
static bool s_wake_pending;

static bool s_verify;
static char *s_verify_buf;

// Region queries (see shadow_init())
static void (*s_region_info)(char *addr, char **top, bool *active);
static int (*s_region_free_prot)(char *addr);

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static inline uint64_t chunk_idx(const char *a) { return (uint64_t) (a - s_h2_start) / CHUNK; }
static inline char *chunk_addr(uint64_t i) { return s_h2_start + i * CHUNK; }
static inline char *slot_addr(uint32_t s) { return s_slots + (uint64_t) s * CHUNK; }
static inline off_t file_off(const char *a) { return (off_t) (a - s_map_start); }
static inline uint64_t region_of(uint64_t i) { return (i * CHUNK) >> s_region_shift; }
static inline uint8_t get_state(uint64_t i) { return __atomic_load_n(&s_state[i], __ATOMIC_ACQUIRE); }
static inline void set_state(uint64_t i, uint8_t v) { __atomic_store_n(&s_state[i], v, __ATOMIC_RELEASE); }

static size_t refresh_ready(void);

static void fatal(const char *what) {
  fprintf(stderr, "[SHADOW] %s failed: %s\n", what, strerror(errno));
  abort();
}

static void xpwrite(const char *b, size_t n, off_t off) {
  while (n) {
    ssize_t r = pwrite(s_fd, b, n, off);
    if (r < 0) {
      if (errno == EINTR) continue;
      fatal("pwrite");
    }
    b += r; n -= r; off += r;
  }
}

// ---- pool ----

// Populate slot `s` with pre-faulted (huge page) anonymous memory
static void populate_slot(uint32_t s) {
  char *a = slot_addr(s);
  if (mmap(a, CHUNK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
    fatal("mmap pool chunk");
  madvise(a, CHUNK, MADV_HUGEPAGE);
  for (size_t o = 0; o < CHUNK; o += PAGE) ((volatile char *) a)[o] = 0;
}

static void refill(void) {
  for (int k = 0; k < REFILL_PER_STEP; k++) {
    pthread_mutex_lock(&s_lock);
    if (s_pool_n >= s_pool_cap) { pthread_mutex_unlock(&s_lock); return; }
    uint32_t s = s_pool_n++;   // reserve the slot number
    pthread_mutex_unlock(&s_lock);
    populate_slot(s);
    pthread_mutex_lock(&s_lock);
    s_free[s_nfree++] = s;
    pthread_mutex_unlock(&s_lock);
  }
}

// ---- attach / detach (s_lock held) ----

static bool attach_locked(uint64_t i) {
  if (s_nfree == 0) { s_st.attach_failed++; return false; }
  uint32_t s = s_free[--s_nfree];
  // MREMAP_DONTUNMAP leaves the slot mapped (empty), so no hole appears that
  // another mmap could take
  if (mremap(slot_addr(s), CHUNK, CHUNK, MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP, chunk_addr(i)) == MAP_FAILED)
    fatal("mremap attach");
  s_recs[s] = (struct rec){.idx = i, .used = 1};
  s_region_busy[region_of(i)]++;
  set_state(i, CS_OPEN);
  s_st.attached++;
  return true;
}

// Move the chunk's pages back to its slot. The chunk range stays mapped as
// empty anonymous memory until map_file() puts the file back over it.
static void detach_pages_locked(uint32_t s) {
  struct rec *r = &s_recs[s];
  char *a = chunk_addr(r->idx);
  if (r->armed) {
    struct uffdio_range ur = {(uintptr_t) a, CHUNK};
    if (ioctl(s_uffd, UFFDIO_UNREGISTER, &ur)) fatal("UFFDIO_UNREGISTER");
  }
  if (mremap(a, CHUNK, CHUNK, MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP, slot_addr(s)) == MAP_FAILED)
    fatal("mremap detach");
  s_region_busy[region_of(r->idx)]--;
  set_state(r->idx, CS_FILE);
  r->used = 0; r->armed = 0; r->in_flight = 0;
  s_free[s_nfree++] = s;
}

static void map_file(uint64_t i, uint64_t n, int prot) {
  char *a = chunk_addr(i);
  if (mmap(a, n * CHUNK, prot, MAP_SHARED | MAP_FIXED, s_fd, file_off(a)) == MAP_FAILED)
    fatal("mmap file");
}

// Detach a chunk whose region was freed or whose space was never used
static void detach_unused_locked(uint32_t s) {
  uint64_t i = s_recs[s].idx;
  detach_pages_locked(s);
  map_file(i, 1, s_region_free_prot(chunk_addr(i)));
}

// ---- write tracking ----

// Written pages of chunk `a` since it was write-protected. With `rearm` they
// are write-protected again. Writes them to the file unless there are more
// than max_pages (-1: no limit). Returns the pages found.
static long rewrite_written(char *a, int rearm, long max_pages, bool *written) {
  static __thread struct page_region vec[CHUNK / PAGE];
  struct pm_scan_arg arg = {.size = sizeof arg,
                            .flags = PM_SCAN_CHECK_WPASYNC | (rearm ? PM_SCAN_WP_MATCHING : 0),
                            .start = (uintptr_t) a, .end = (uintptr_t) (a + CHUNK),
                            .vec = (uintptr_t) vec, .vec_len = CHUNK / PAGE,
                            .category_mask = PAGE_IS_WRITTEN, .return_mask = PAGE_IS_WRITTEN};
  int nr = ioctl(s_pagemap, PAGEMAP_SCAN, &arg);
  if (nr < 0) fatal("PAGEMAP_SCAN");
  long pages = 0;
  for (int k = 0; k < nr; k++) pages += (vec[k].end - vec[k].start) / PAGE;
  *written = false;
  if (max_pages >= 0 && pages > max_pages) return pages;
  for (int k = 0; k < nr; k++)
    xpwrite((char *) vec[k].start, vec[k].end - vec[k].start, file_off((char *) vec[k].start));
  *written = true;
  return pages;
}

// ---- API ----

int shadow_init(int fd, char *map_start, size_t map_size, char *h2_start,
                uint64_t region_size, size_t pool_bytes, int precopy_rounds, int n_threads,
                void (*region_info)(char *addr, char **top, bool *active),
                int (*region_free_prot)(char *addr)) {
  if (s_on) return 0;
  if (((uintptr_t) h2_start % CHUNK) != 0 || pool_bytes < CHUNK || n_threads <= 0 ||
      region_size < CHUNK || (region_size & (region_size - 1)) != 0) {
    fprintf(stderr, "[SHADOW] bad arguments (H2 start %p must be 2 MB aligned, region size %lu "
            "a power of two of at least 2 MB)\n", h2_start, (unsigned long) region_size);
    return -1;
  }
  int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
  if (uffd < 0) { fprintf(stderr, "[SHADOW] userfaultfd: %s\n", strerror(errno)); return -1; }
  struct uffdio_api api = {.api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC};
  if (ioctl(uffd, UFFDIO_API, &api)) {
    fprintf(stderr, "[SHADOW] UFFD_FEATURE_WP_ASYNC: %s (needs Linux >= 6.7)\n", strerror(errno));
    close(uffd);
    return -1;
  }
  int pm = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
  if (pm < 0) { fprintf(stderr, "[SHADOW] /proc/self/pagemap: %s\n", strerror(errno)); close(uffd); return -1; }

  s_fd = fd; s_uffd = uffd; s_pagemap = pm;
  s_region_info = region_info; s_region_free_prot = region_free_prot;
  s_map_start = map_start; s_h2_start = h2_start;
  s_region_size = region_size; s_region_shift = (unsigned) __builtin_ctzll(region_size);
  s_h2_end = h2_start + ((map_start + map_size - h2_start) / CHUNK) * CHUNK;
  s_nchunks = (s_h2_end - s_h2_start) / CHUNK;
  s_nregions = ((uint64_t) (s_h2_end - s_h2_start) + s_region_size - 1) >> s_region_shift;
  s_precopy_rounds = precopy_rounds < 0 ? 0 : precopy_rounds;
  s_state = calloc(s_nchunks, 1);
  s_region_busy = calloc(s_nregions, sizeof *s_region_busy);
  s_pool_cap = pool_bytes / CHUNK;
  s_free = calloc(s_pool_cap, sizeof *s_free);
  s_recs = calloc(s_pool_cap, sizeof *s_recs);
  s_nthreads = n_threads;
  s_skip = malloc(n_threads * sizeof *s_skip);
  for (int t = 0; t < n_threads; t++) s_skip[t] = -1;
  char *r = mmap(NULL, (uint64_t) (s_pool_cap + 1) * CHUNK, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (!s_state || !s_region_busy || !s_free || !s_recs || !s_skip || r == MAP_FAILED) {
    fprintf(stderr, "[SHADOW] out of memory\n");
    return -1;
  }
  s_slots = (char *) (((uintptr_t) r + CHUNK - 1) & ~(CHUNK - 1));
  s_on = true;
  refill();   // a first part of the pool; the writer grows it to the full size
  return 0;
}


void h2_shadow_set_active(bool active) {
  if (!s_on) return;
  if (active) for (int t = 0; t < s_nthreads; t++) s_skip[t] = -1;
  s_active = active;
}

void shadow_ensure(char *pos, size_t bytes, uint64_t thread_id) {
  if (!s_active || pos < s_h2_start || pos >= s_h2_end || bytes == 0) return;
  uint64_t i0 = chunk_idx(pos), i1 = chunk_idx(pos + bytes - 1);
  if (i1 >= s_nchunks) i1 = s_nchunks - 1;
  int64_t skip = s_skip[thread_id];
  uint64_t i;
  for (i = i0; i <= i1; i++)
    if (get_state(i) == CS_FILE && (int64_t) i != skip) break;
  if (i > i1) return;   // fast path: all attached or known to stay in the file

  pthread_mutex_lock(&s_lock);
  for (i = i0; i <= i1; i++) {
    if (get_state(i) != CS_FILE) continue;
    // Empty only if nothing was allocated in it before this object
    if (chunk_addr(i) < pos || !attach_locked(i)) s_skip[thread_id] = (int64_t) i;
  }
  pthread_mutex_unlock(&s_lock);
}

int shadow_preattach(char *top, size_t bytes) {
  if (!s_on || top < s_h2_start || top >= s_h2_end) return 0;
  uint64_t i = (uint64_t) (top - s_h2_start + CHUNK - 1) / CHUNK;   // first chunk at or above top
  // Stay inside top's region: the next region belongs to other objects
  char *region_end = s_h2_start + (((uint64_t) (top - s_h2_start) >> s_region_shift) + 1) * s_region_size;
  char *last = top + bytes - 1 < region_end - 1 ? top + bytes - 1 : region_end - 1;
  if (last >= s_h2_end) last = s_h2_end - 1;
  uint64_t end = chunk_idx(last);
  int n = 0;
  pthread_mutex_lock(&s_lock);
  for (; i <= end && i < s_nchunks; i++)
    if (get_state(i) == CS_FILE && attach_locked(i)) n++;
  pthread_mutex_unlock(&s_lock);
  return n;
}

void h2_shadow_pause_end(void) {
  if (!s_on) return;
  pthread_mutex_lock(&s_lock);
  for (uint32_t s = 0; s < s_pool_n; s++) {
    struct rec *r = &s_recs[s];
    if (!r->used || get_state(r->idx) != CS_OPEN) continue;
    char *c = chunk_addr(r->idx), *top; bool active;
    s_region_info(c, &top, &active);
    if (top >= c + CHUNK) {
      set_state(r->idx, CS_FULL); r->seq = ++s_seq;
    } else if (!active) {   // retired region: no more allocation will come
      if (top <= c) { detach_unused_locked(s); s_st.empty_detached++; }
      else { set_state(r->idx, CS_FULL); r->seq = ++s_seq; }
    }
  }
  pthread_mutex_unlock(&s_lock);

  pthread_mutex_lock(&s_wake_lock);
  s_wake_pending = true;
  pthread_cond_signal(&s_wake);
  pthread_mutex_unlock(&s_wake_lock);
}

size_t shadow_writeback_step(void) {
  if (!s_on) return 0;
  pthread_mutex_lock(&s_lock);
  // DEAD chunks the writer finished with
  for (uint32_t s = 0; s < s_pool_n; s++)
    if (s_recs[s].used && !s_recs[s].in_flight && get_state(s_recs[s].idx) == CS_DEAD) detach_unused_locked(s);
  // Oldest FULL chunk
  int64_t pick = -1;
  for (uint32_t s = 0; s < s_pool_n; s++)
    if (s_recs[s].used && get_state(s_recs[s].idx) == CS_FULL && (pick < 0 || s_recs[s].seq < s_recs[pick].seq)) pick = s;
  if (pick < 0) {
    pthread_mutex_unlock(&s_lock);
    // Not counted in the result: the writer then waits a little before the
    // next refresh instead of spinning while mutators write
    refresh_ready();
    refill();
    return 0;
  }
  struct rec *r = &s_recs[pick];
  set_state(r->idx, CS_WRITING); r->in_flight = 1;
  char *a = chunk_addr(r->idx);
  pthread_mutex_unlock(&s_lock);

  // Arm write tracking before the first copy
  struct uffdio_register reg = {.range = {(uintptr_t) a, CHUNK}, .mode = UFFDIO_REGISTER_MODE_WP};
  if (ioctl(s_uffd, UFFDIO_REGISTER, &reg)) fatal("UFFDIO_REGISTER");
  struct uffdio_writeprotect wp = {.range = {(uintptr_t) a, CHUNK}, .mode = UFFDIO_WRITEPROTECT_MODE_WP};
  if (ioctl(s_uffd, UFFDIO_WRITEPROTECT, &wp)) fatal("UFFDIO_WRITEPROTECT");
  r->armed = 1;   // only the writer touches an in-flight record
  xpwrite(a, CHUNK, file_off(a));
  size_t bytes = CHUNK; long pc = 0;
  for (int k = 0; k < s_precopy_rounds; k++) {
    bool w; long n = rewrite_written(a, 1, -1, &w);
    pc += n; bytes += n * PAGE;
    if (n < PRECOPY_MIN_PAGES) break;
  }

  pthread_mutex_lock(&s_lock);
  r->in_flight = 0;
  if (get_state(r->idx) == CS_DEAD) detach_unused_locked(pick);   // region freed meanwhile
  else set_state(r->idx, CS_READY);
  s_st.written_back++; s_st.written_bytes += bytes; s_st.precopy_pages += pc;
  pthread_mutex_unlock(&s_lock);
  refill();
  return bytes;
}

static int cmp_idx(const void *x, const void *y);

// Copy again the pages written in READY chunks since their last copy and
// write-protect them again. A switch then only copies the pages written
// since this refresh.
static size_t refresh_ready(void) {
  size_t bytes = 0;
  for (uint32_t s = 0; s < s_pool_n; s++) {
    pthread_mutex_lock(&s_lock);
    struct rec *r = &s_recs[s];
    if (!r->used || r->in_flight || get_state(r->idx) != CS_READY) { pthread_mutex_unlock(&s_lock); continue; }
    r->in_flight = 1;   // the switch leaves it alone meanwhile
    char *a = chunk_addr(r->idx);
    pthread_mutex_unlock(&s_lock);

    bool w; long n = rewrite_written(a, 1, -1, &w);

    pthread_mutex_lock(&s_lock);
    r->in_flight = 0;
    if (get_state(r->idx) == CS_DEAD) detach_unused_locked(s);   // region freed meanwhile
    s_st.refresh_pages += n;
    pthread_mutex_unlock(&s_lock);
    bytes += n * PAGE;
  }
  return bytes;
}

static int cmp_idx(const void *x, const void *y) {
  uint64_t a = s_recs[*(const uint32_t *) x].idx, b = s_recs[*(const uint32_t *) y].idx;
  return a < b ? -1 : a > b;
}

uint32_t h2_shadow_switch(uint32_t max_chunks, long max_delta_pages) {
  if (!s_on) return 0;
  double t0 = now_ms();
  pthread_mutex_lock(&s_lock);
  uint32_t *ready = malloc(s_pool_n * sizeof *ready), nr = 0;
  for (uint32_t s = 0; s < s_pool_n; s++)
    if (s_recs[s].used && !s_recs[s].in_flight && get_state(s_recs[s].idx) == CS_READY) ready[nr++] = s;
  qsort(ready, nr, sizeof *ready, cmp_idx);
  uint64_t *done = malloc((nr + 1) * sizeof *done); uint32_t nd = 0;
  uint64_t delta = 0;
  for (uint32_t k = 0; k < nr && (max_chunks == 0 || nd < max_chunks); k++) {
    uint32_t s = ready[k];
    bool w; long n = rewrite_written(chunk_addr(s_recs[s].idx), 0, max_delta_pages, &w);
    if (!w) { s_st.switch_skipped++; continue; }
    delta += n;
    if (s_verify) {
      char *a = chunk_addr(s_recs[s].idx);
      if (pread(s_fd, s_verify_buf, CHUNK, file_off(a)) != (ssize_t) CHUNK) fatal("verify pread");
      if (memcmp(a, s_verify_buf, CHUNK) != 0) {
        size_t o = 0;
        while (a[o] == s_verify_buf[o]) o++;
        fprintf(stderr, "[SHADOW] verify: chunk %p differs from the file at offset %zu\n", (void *) a, o);
        abort();
      }
    }
    done[nd++] = s_recs[s].idx;
    detach_pages_locked(s);
  }
  // Put the file back, one mmap per run of adjacent chunks
  for (uint32_t k = 0; k < nd;) {
    uint32_t j = k + 1;
    while (j < nd && done[j] == done[j - 1] + 1) j++;
    map_file(done[k], j - k, PROT_READ | PROT_WRITE);
    k = j;
  }
  s_st.switched += nd; s_st.delta_pages += delta;
  s_st.last_switched = nd; s_st.last_delta_pages = delta; s_st.last_switch_ms = now_ms() - t0;
  pthread_mutex_unlock(&s_lock);
  free(ready); free(done);
  return nd;
}

void shadow_free_range(char *start, size_t len) {
  if (!s_on) return;
  char *end = start + len;
  pthread_mutex_lock(&s_lock);
  for (uint32_t s = 0; s < s_pool_n; s++) {
    struct rec *r = &s_recs[s];
    if (!r->used) continue;
    char *c = chunk_addr(r->idx);
    if (c < start || c >= end || get_state(r->idx) == CS_DEAD) continue;
    s_st.dead++;
    s_st.dead_by_state[get_state(r->idx)]++;
    if (r->in_flight) { set_state(r->idx, CS_DEAD); s_st.dead_in_flight++; }   // the writer detaches it
    else detach_unused_locked(s);
  }
  pthread_mutex_unlock(&s_lock);
}

bool shadow_range_busy(char *start, size_t len) {
  if (!s_on || start < s_h2_start || start >= s_h2_end) return false;
  uint64_t r0 = region_of(chunk_idx(start)), r1 = region_of(chunk_idx(start + len - 1));
  for (uint64_t r = r0; r <= r1 && r < s_nregions; r++)
    if (__atomic_load_n(&s_region_busy[r], __ATOMIC_ACQUIRE)) return true;
  return false;
}

bool h2_shadow_is_attached(char *addr) {
  return s_on && addr >= s_h2_start && addr < s_h2_end && get_state(chunk_idx(addr)) != CS_FILE;
}

void shadow_split_write(char *data, char *addr, size_t len,
                        void (*write_file)(char *data, char *addr, size_t len)) {
  if (!s_on) { write_file(data, addr, len); return; }
  size_t done = 0, file_from = 0, file_len = 0;
  while (done < len) {
    char *a = addr + done;
    size_t to_chunk_end = CHUNK - (size_t) ((uintptr_t) (a - s_h2_start) % CHUNK);
    size_t n = len - done < to_chunk_end ? len - done : to_chunk_end;
    if (h2_shadow_is_attached(a)) {
      if (file_len) { write_file(data + file_from, addr + file_from, file_len); file_len = 0; }
      memcpy(a, data + done, n);
      __atomic_fetch_add(&s_st.redirected_bytes, n, __ATOMIC_RELAXED);
    } else {
      if (!file_len) file_from = done;
      file_len += n;
    }
    done += n;
  }
  if (file_len) write_file(data + file_from, addr + file_from, file_len);
}

static void *writer_main(void *arg) {
  (void) arg;
  pthread_setname_np(pthread_self(), "SH writer");
  for (;;) {
    if (shadow_writeback_step() > 0) continue;
    // Nothing to do: wait for a pause end, or poll for DEAD chunks / refill
    pthread_mutex_lock(&s_wake_lock);
    if (!s_wake_pending) {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += 5 * 1000 * 1000;
      if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
      pthread_cond_timedwait(&s_wake, &s_wake_lock, &ts);
    }
    s_wake_pending = false;
    pthread_mutex_unlock(&s_wake_lock);
  }
  return NULL;
}

int h2_shadow_start_writer(void) {
  if (!s_on) return -1;
  if (pthread_create(&s_writer, NULL, writer_main, NULL)) {
    fprintf(stderr, "[SHADOW] cannot start the writer thread\n");
    return -1;
  }
  return 0;
}

void h2_shadow_set_verify(bool on) {
  if (on && !s_verify_buf) s_verify_buf = malloc(CHUNK);
  s_verify = on && s_verify_buf;
}

void h2_shadow_get_stats(struct h2_shadow_stats *st) {
  pthread_mutex_lock(&s_lock);
  *st = s_st;
  st->pool_chunks = s_pool_n; st->free_chunks = s_nfree;
  st->n_open = st->n_full = st->n_writing = st->n_ready = st->n_dead = 0;
  for (uint32_t s = 0; s < s_pool_n; s++) {
    if (!s_recs[s].used) continue;
    switch (get_state(s_recs[s].idx)) {
      case CS_OPEN: st->n_open++; break;
      case CS_FULL: st->n_full++; break;
      case CS_WRITING: st->n_writing++; break;
      case CS_READY: st->n_ready++; break;
      case CS_DEAD: st->n_dead++; break;
    }
  }
  pthread_mutex_unlock(&s_lock);
}
