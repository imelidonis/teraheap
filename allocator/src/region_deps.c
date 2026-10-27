// Region dependencies (lock-free sets of the regions each region references) and region liveness.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "regions.h"
#include "region_types.h"
#include "sharedDefines.h"

/*
 * ---------------------------------------------------------------------------
 * Dependency sets: lock-free hash sets of region indices
 * ---------------------------------------------------------------------------
 *
 * region_array[r].deps holds the regions that region r
 * references. GC threads add entries in parallel (h2_references()), look
 * them up (h2_is_in_the_same_group()) and walk them (h2_mark_used()), without
 * a lock:
 *  - Open addressing, linear probing. A slot holds seg + 1 (DEP_EMPTY = 0
 *    is an empty slot). Entries are never removed one by one; a region's
 *    whole set is freed in h2_free_regions(), at a safepoint.
 *  - Insert: CAS an empty slot from DEP_EMPTY to the key. No duplicates:
 *    with no deletes, the probe path of a key never has a hole before it.
 *  - Growth (above half full): the thread that starts it installs a table
 *    of twice the capacity in 'next' (CAS), freezes every empty slot of the
 *    old table (CAS DEP_EMPTY -> DEP_FROZEN, so no insert can land there any
 *    more), copies every key to the new table, and publishes the new table
 *    in the region (CAS). A thread that meets a frozen slot continues in
 *    'next'. Old tables stay readable (and keep 'next') until
 *    h2_free_regions() frees them.
 *  - Liveness: an insert is a full fence (CAS) before it reads 'used' of the
 *    source region, and h2_mark_used() fences between setting 'used' and
 *    walking the set. So a new edge is either seen by the walk, or the
 *    inserter sees 'used' and marks the target itself.
 */
#define DEP_EMPTY         0u
#define DEP_FROZEN        UINT32_MAX
#define DEP_INITIAL_CAP   16u          /* 64 bytes of slots */

struct dep_set {
  uint32_t capacity;                   /* power of 2 */
  uint32_t count;                      /* entries (atomic add; may run ahead) */
  struct dep_set *next;                /* larger table during and after growth */
  struct dep_set *retired_link;        /* list of tables to free at a safepoint */
  uint32_t slots[];
};

/* Tables replaced by growth, freed in h2_free_regions() (at a safepoint) */
static struct dep_set *retired_dep_sets = NULL;

static struct dep_set *dep_set_alloc(uint32_t capacity) {
  size_t bytes = sizeof(struct dep_set) + (size_t) capacity * sizeof(uint32_t);
  bytes = (bytes + CACHE_LINE_SIZE - 1) & ~((size_t) CACHE_LINE_SIZE - 1);
  struct dep_set *s = aligned_alloc(CACHE_LINE_SIZE, bytes);
  check_allocation_failure(s, "[ERROR] -- Failed to allocate a region dependency set\n");
  memset(s, 0, bytes);
  s->capacity = capacity;
  return s;
}

static inline uint32_t dep_slot(uint32_t key, uint32_t capacity) {
  return (uint32_t) ((key * 0x9E3779B97F4A7C15ULL) >> 32) & (capacity - 1);
}

static inline struct dep_set *dep_load(struct dep_set **p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

/* 1 if 'key' is in the set (any table of the chain), 0 otherwise */
static int dep_set_contains(struct dep_set *s, uint32_t key) {
  while (s != NULL) {
    const uint32_t cap = s->capacity;
    uint32_t i = dep_slot(key, cap);
    int go_next = 0;
    for (uint32_t n = 0; n < cap; n++, i = (i + 1) & (cap - 1)) {
      const uint32_t v = __atomic_load_n(&s->slots[i], __ATOMIC_ACQUIRE);
      if (v == key)
        return 1;
      if (v == DEP_EMPTY)
        return 0;
      if (v == DEP_FROZEN) {
        go_next = 1;
        break;
      }
    }
    if (!go_next)
      return 0;
    s = dep_load(&s->next);
  }
  return 0;
}

static void dep_set_grow(struct dep_set **owner, struct dep_set *s);

/*
 * Insert 'key' into table 's' (or the tables after it).
 * Returns 1 if this call inserted it, 0 if it was already there.
 */
static int dep_table_insert(struct dep_set **owner, struct dep_set *s, uint32_t key) {
  for (;;) {
    const uint32_t cap = s->capacity;
    uint32_t i = dep_slot(key, cap);
    int go_next = 0;
    for (uint32_t n = 0; n < cap; n++, i = (i + 1) & (cap - 1)) {
      uint32_t v = __atomic_load_n(&s->slots[i], __ATOMIC_ACQUIRE);
      if (v == key)
        return 0;
      if (v == DEP_FROZEN) {
        go_next = 1;
        break;
      }
      if (v != DEP_EMPTY)
        continue;                      /* another key: keep probing */

      /* Keep the table at most half full */
      if (2 * (__atomic_load_n(&s->count, __ATOMIC_RELAXED) + 1) > cap) {
        dep_set_grow(owner, s);
        go_next = 1;
        break;
      }

      uint32_t expected = DEP_EMPTY;
      if (__atomic_compare_exchange_n(&s->slots[i], &expected, key, 0,
                                      __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&s->count, 1, __ATOMIC_RELAXED);
        return 1;
      }
      if (expected == key)
        return 0;                      /* inserted by another thread */
      if (expected == DEP_FROZEN) {
        go_next = 1;
        break;
      }
      /* another key took the slot: keep probing */
    }
    if (!go_next)
      dep_set_grow(owner, s);          /* no empty slot left (should not happen) */
    s = dep_load(&s->next);
  }
}

/*
 * Grow table 's' of the set '*owner': install the larger table, freeze and
 * copy the old one, publish the new one. Any number of threads may run
 * this for the same table; each one copies every key it sees (inserts are
 * idempotent), so the new table is complete once any of them returns.
 */
static void dep_set_grow(struct dep_set **owner, struct dep_set *s) {
  struct dep_set *nxt = dep_load(&s->next);
  if (nxt == NULL) {
    struct dep_set *cand = dep_set_alloc(s->capacity * 2);
    struct dep_set *expected = NULL;
    if (__atomic_compare_exchange_n(&s->next, &expected, cand, 0,
                                    __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)) {
      nxt = cand;
    } else {
      free(cand);
      nxt = expected;
    }
  }

  for (uint32_t i = 0; i < s->capacity; i++) {
    uint32_t v = __atomic_load_n(&s->slots[i], __ATOMIC_ACQUIRE);
    while (v == DEP_EMPTY) {
      uint32_t expected = DEP_EMPTY;
      if (__atomic_compare_exchange_n(&s->slots[i], &expected, DEP_FROZEN, 0,
                                      __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)) {
        v = DEP_FROZEN;
      } else {
        v = expected;
      }
    }
    if (v != DEP_FROZEN)
      dep_table_insert(owner, nxt, v);
  }

  /* Publish the new table once (only the CAS winner retires the old one) */
  struct dep_set *expected = s;
  if (__atomic_compare_exchange_n(owner, &expected, nxt, 0,
                                  __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)) {
    struct dep_set *head = __atomic_load_n(&retired_dep_sets, __ATOMIC_RELAXED);
    do {
      s->retired_link = head;
    } while (!__atomic_compare_exchange_n(&retired_dep_sets, &head, s, 1,
                                          __ATOMIC_RELEASE, __ATOMIC_RELAXED));
  }
}

/* Add 'seg2' to the set of 'seg1'. Returns 1 if this call added it. */
static int dep_set_add(int32_t seg1, int32_t seg2) {
  struct dep_set **owner = &region_array[seg1].deps;
  struct dep_set *s = dep_load(owner);
  if (s == NULL) {
    struct dep_set *cand = dep_set_alloc(DEP_INITIAL_CAP);
    struct dep_set *expected = NULL;
    if (__atomic_compare_exchange_n(owner, &expected, cand, 0,
                                    __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)) {
      s = cand;
    } else {
      free(cand);
      s = expected;
    }
  }
  return dep_table_insert(owner, s, (uint32_t) seg2 + 1);
}

/* Number of entries (statistics; the last table of the chain) */
static uint64_t dep_set_count(struct dep_set *s) {
  if (s == NULL)
    return 0;
  while (dep_load(&s->next) != NULL)
    s = dep_load(&s->next);
  return __atomic_load_n(&s->count, __ATOMIC_RELAXED);
}

/* Call 'fn' for every region index in the set (all tables of the chain) */
static void dep_set_for_each(struct dep_set *s, void (*fn)(uint64_t seg)) {
  for (; s != NULL; s = dep_load(&s->next)) {
    for (uint32_t i = 0; i < s->capacity; i++) {
      const uint32_t v = __atomic_load_n(&s->slots[i], __ATOMIC_ACQUIRE);
      if (v != DEP_EMPTY && v != DEP_FROZEN)
        fn((uint64_t) v - 1);
    }
  }
}

/* At a safepoint: free the set of a region (its current table chain) */
void dep_set_free(struct dep_set *s) {
  while (s != NULL) {
    struct dep_set *nxt = s->next;
    free(s);
    s = nxt;
  }
}

/* At a safepoint: free the tables replaced by growth */
void dep_free_retired(void) {
  struct dep_set *s = retired_dep_sets;
  retired_dep_sets = NULL;
  while (s != NULL) {
    struct dep_set *nxt = s->retired_link;
    free(s);
    s = nxt;
  }
}

// Add the dependency seg1 -> seg2; if seg1 is already marked used, mark seg2 too
static void add_dependency_if_not_exist(int32_t seg1, int32_t seg2) {
  // Lock-free; the successful CAS of the insert is a full fence, so the
  // load of 'used' below cannot be ordered before the new entry is
  // visible (see h2_mark_used())
  if (!dep_set_add(seg1, seg2))
    return;

  if (__atomic_load_n(&region_array[seg1].used, __ATOMIC_SEQ_CST)) {
    h2_mark_used(region_array[seg2].start_address);
  }
}

/*
 * function that connects two regions in a tera_group
 * arguments:
 * - obj1: the object that references the other
 * - obj2 the object that is referenced
 * Nothing to do if both objects are in the same region, or one is outside
 * H2. Lock-free: GC threads may call it in parallel.
 */
void h2_references(char *obj1, char *obj2) {
  uint64_t seg1 = region_index(obj1);
  uint64_t seg2 = region_index(obj2);

  if (seg1 >= region_array_size || seg2 >= region_array_size || seg1 == seg2)
    return;

  add_dependency_if_not_exist(seg1, seg2);
}

/*
 * Print the size of the dependency list of each region (if they have)
 */
void h2_print_groups() {
  int32_t i;

  fprintf(stderr, "Groups:\n");

  for (i = 0; i < region_array_size; i++) {
    if (!region_is_empty(&region_array[i])) {
      size_t dep_size = dep_set_count(region_array[i].deps);
      fprintf(stderr, "Region %d has %lu dependencies.\n", i, dep_size);
    }
  }
}

/*
 * Resets the used field of all regions and groups
 */
void h2_reset_used() {
  int32_t i;
  for (i = 0 ; i < region_array_size ; i++)
    region_array[i].used = 0;
}

static void mark_used_region_index(uint64_t seg) {
  h2_mark_used(region_array[seg].start_address);
}

static void mark_live_dependent_regions(uint64_t seg) {
  dep_set_for_each(dep_load(&region_array[seg].deps), mark_used_region_index);
}

/*
 * Marks the region that contains this obj as used and increases tera_group
 * counter (if it belongs to a tera_group)
 * Arguments: obj: the object that is alive
 */
void h2_mark_used(char *obj) {
  uint64_t seg = region_index(obj);

  assertf(seg < region_array_size, "Region index out of range %lu", seg);
  if (__atomic_load_n(&region_array[seg].used, __ATOMIC_RELAXED) == 1)
    return;

  __atomic_store_n(&region_array[seg].used, 1, __ATOMIC_RELAXED);
  // Full fence between setting 'used' and walking the set, so that either
  // this walk sees a dependency added concurrently, or the thread that
  // added it sees 'used' and marks the target (add_dependency_if_not_exist())
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  mark_live_dependent_regions(seg);
}

/*
 * Check if these two objects belong to the same tera_group
 *
 * obj1: address of the object
 * obj2: address of the object
 *
 * returns: 1 if objects are in the same tera_group, 0 otherwise
 */
int h2_is_in_the_same_group(char *obj1, char *obj2) {
	uint64_t seg1 = region_index(obj1);
	uint64_t seg2 = region_index(obj2);

	assertf(seg1 < region_array_size && seg2 < region_array_size,
	        "Region index out of range %lu, %lu", seg1, seg2);

	/* Objects belong to the same regions */
	if (seg1 == seg2)
		return 1;

  return dep_set_contains(dep_load(&region_array[seg1].deps), (uint32_t) seg2 + 1);
}
