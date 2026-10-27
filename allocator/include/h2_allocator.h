#ifndef H2_ALLOCATOR_H
#define H2_ALLOCATOR_H

/*
 * Public API of the H2 (SH) allocator.
 *
 * This is the only allocator header that TeraHeap includes. Every function
 * here starts with h2_; everything else in the allocator is internal.
 *
 * Sizes passed to h2_allocate(), h2_write(), h2_awrite() and
 * h2_buffer_insert() are in HeapWords (8 bytes).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The library is built with -fvisibility=hidden: only H2_API functions are
 * exported */
#define H2_API __attribute__((visibility("default")))

/* ------------------------------------------------------------------ */
/* Pool and lifecycle                                                  */
/* ------------------------------------------------------------------ */

/*
 * Create the H2 file `h2_file_size` bytes big in directory `h2_file_path`,
 * map it with its start aligned to `alignment`, and set up the regions.
 * `partitions`: maximum partitions per group (sizes the id mapping array).
 * `total_gc_threads`: number of GC threads that call h2_allocate().
 * `group_by_gc_thread`: group regions by the allocating GC thread
 * (h2_allocate()'s thread_id) instead of by rdd_id; then each region has
 * one allocating thread. h2_shadow_init() needs it.
 */
H2_API void h2_init(uint64_t alignment, const char *h2_file_path, uint64_t h2_file_size,
             uint64_t partitions, int total_gc_threads, bool group_by_gc_thread);

/* Start (aligned) and end of the H2 address range */
H2_API char *h2_start_addr_mem_pool(void);
H2_API char *h2_stop_addr_mem_pool(void);

/* Current allocation top (updated by h2_update_allocator_global_state()) */
H2_API char *h2_cur_alloc_ptr(void);

/* Size of the H2 file in bytes */
H2_API size_t h2_mem_pool_size(void);

/* 1 if nothing was allocated yet, 0 otherwise */
H2_API int h2_is_empty(void);

/* Unmap the H2 file */
H2_API void h2_shutdown(void);

/* 1 if the allocation top is at or after the top of the last region */
H2_API int h2_verify_top(void);

/* ------------------------------------------------------------------ */
/* Allocation                                                          */
/* ------------------------------------------------------------------ */

/*
 * Allocate `size` HeapWords for an object of group `rdd_id` and partition
 * `partition_id`, called by GC thread `thread_id` (< total_gc_threads).
 * Returns the address; exits if H2 is full.
 */
H2_API char *h2_allocate(size_t size, uint64_t rdd_id, uint64_t partition_id, uint64_t thread_id);

/* After a GC: fold the per-thread allocation counters into the pool state */
H2_API void h2_update_allocator_global_state(void);

/* ------------------------------------------------------------------ */
/* Region dependencies and liveness                                    */
/* ------------------------------------------------------------------ */

/*
 * Record that the region of obj1 references the region of obj2, so that
 * obj2's region stays alive while obj1's does. Lock-free: GC threads may
 * call it in parallel.
 */
H2_API void h2_references(char *obj1, char *obj2);

/* 1 if obj2's region is already a dependency of obj1's region (or the same) */
H2_API int h2_is_in_the_same_group(char *obj1, char *obj2);

/* Mark the region of obj, and every region it depends on, as used */
H2_API void h2_mark_used(char *obj);

/* Clear the used mark of every region (before marking) */
H2_API void h2_reset_used(void);

/* 1 if the region with index `region` is marked used */
H2_API int h2_is_used(uint64_t region);

/* ------------------------------------------------------------------ */
/* Reclamation                                                         */
/* ------------------------------------------------------------------ */

/*
 * Called by h2_free_regions() for each freed region that held objects:
 * region_start, the start of its last allocated object, and the end of its
 * last allocated object (exclusive). The region is already free (dependencies,
 * allocation pointers and shadow chunks released); it is protected after the
 * callback returns if h2_protect_regions_on_free(1) was set. The callback runs
 * at a safepoint on one thread and must not call the allocator.
 */
typedef void (*h2_region_free_cb)(char *region_start, char *last_allocated_start,
                                  char *last_allocated_end, void *ctx);

/*
 * Free every allocated region that is not marked used, and call cb(..., ctx)
 * for each one that held objects (cb may be NULL).
 */
H2_API void h2_free_regions(h2_region_free_cb cb, void *ctx);

/* Number of regions freed by the last h2_free_regions() */
H2_API unsigned int h2_num_reclaimed_regions(void);

/* 1: freed regions are protected (PROT_NONE); their protection is removed on reuse */
H2_API void h2_protect_regions_on_free(int should_protect);

/* ------------------------------------------------------------------ */
/* Object and region queries                                           */
/* ------------------------------------------------------------------ */

/* First object of the region that contains addr */
H2_API char *h2_get_first_object(char *addr);

/* End of the last object of the region that contains obj */
H2_API char *h2_get_last_object(char *obj);

/* True if obj is before the end of the last object of its region */
H2_API bool h2_is_before_last_object(char *obj);

/* True if obj is the first object of its region */
H2_API bool h2_is_region_start(char *obj);

/* True if the region of obj starts with an object (not a continuation) */
H2_API bool h2_object_starts_from_region(char *obj);

/* Number of contiguous regions of the allocation that starts in addr's region */
H2_API int h2_get_num_of_continuous_regions(char *addr);

/* Group (rdd) and partition id of the region that contains obj */
H2_API uint64_t h2_get_obj_group_id(char *obj);
H2_API uint64_t h2_get_obj_part_id(char *obj);

/* Index of the region that contains addr */
H2_API uint64_t h2_region_containing_addr(char *addr);

/* ------------------------------------------------------------------ */
/* Region iteration                                                    */
/* ------------------------------------------------------------------ */

/* Start iterating over the allocated regions (single iterator, not reentrant) */
H2_API void h2_start_iterate_regions(void);

/* Start address of the next allocated region, or NULL at the end */
H2_API char *h2_get_next_region(void);

/* ------------------------------------------------------------------ */
/* Promotion buffers                                                   */
/* ------------------------------------------------------------------ */

/*
 * Copy the object `obj` of `size` HeapWords that moves to `new_adr` into the
 * promotion buffer of new_adr's region. Big objects are written directly.
 */
H2_API void h2_buffer_insert(char *obj, char *new_adr, size_t size);

/* Flush every promotion buffer and free its memory */
H2_API void h2_free_all_buffers(void);

/* ------------------------------------------------------------------ */
/* I/O and kernel hints                                                */
/* ------------------------------------------------------------------ */

/* Write `size` HeapWords from data to the H2 address `offset` (pwrite) */
H2_API void h2_write(char *data, char *offset, size_t size);

/* Same with asynchronous I/O; do not mix with h2_write() */
H2_API void h2_awrite(char *data, char *offset, size_t size);

/* 1 if every asynchronous write has completed, 0 otherwise */
H2_API int h2_areq_completed(void);

/* fsync() the H2 file */
H2_API void h2_fsync(void);

/* madvise() the H2 range: sequential or random access */
H2_API void h2_enable_seq(void);
H2_API void h2_enable_rand(void);

/* FastMap hybrid version: serve faults with regular or huge pages */
H2_API void h2_enable_regular_flts(void);
H2_API void h2_enable_huge_flts(void);

/* ------------------------------------------------------------------ */
/* Shadow chunks (SH writes in mixed GCs, see src/shadow.c)            */
/* ------------------------------------------------------------------ */

#define H2_SHADOW_CHUNK (2 * 1024LU * 1024)   /* 2 MB: one transparent huge page */

struct h2_shadow_stats {
  /* Totals since h2_shadow_init() */
  uint64_t attached;          /* Chunks attached */
  uint64_t attach_failed;     /* Chunk entries that went to the file (no free pool chunk) */
  uint64_t written_back;      /* Chunks copied to the file by the writer */
  uint64_t written_bytes;     /* Bytes written by the writer (incl. pre-copy) */
  uint64_t precopy_pages;     /* Pages copied again during pre-copy rounds */
  uint64_t refresh_pages;     /* Pages of READY chunks copied again by the writer */
  uint64_t switched;          /* Chunks switched back to the file */
  uint64_t switch_skipped;    /* READY chunks left for a later pause (delta too big) */
  uint64_t delta_pages;       /* Pages copied again at switches */
  uint64_t dead;              /* Chunks of freed regions */
  uint64_t dead_in_flight;    /* ... of them freed while the writer worked on them */
  uint64_t dead_by_state[6];  /* ... by state when freed (index: FILE..DEAD order) */
  uint64_t empty_detached;    /* OPEN chunks returned unused (retired region) */
  uint64_t redirected_bytes;  /* h2_awrite() bytes copied into chunks */
  /* Current */
  uint32_t pool_chunks;       /* Populated pool chunks */
  uint32_t free_chunks;       /* Pool chunks not attached */
  uint32_t n_open, n_full, n_writing, n_ready, n_dead;
  /* Last h2_shadow_switch() */
  double   last_switch_ms;
  uint32_t last_switched;
  uint64_t last_delta_pages;
};

/*
 * Enable shadow chunks for the H2 file mapping, with a pool of up to
 * pool_bytes and up to precopy_rounds pre-copy rounds per chunk.
 * Returns 0, or -1 if not available (the allocator then works as before),
 * also when h2_init() did not group regions by GC thread.
 */
H2_API int h2_shadow_init(size_t pool_bytes, int precopy_rounds);

/* Start the writer thread. Returns 0, or -1 */
H2_API int h2_shadow_start_writer(void);

/* Debug: compare every chunk with the file at each switch, abort on a difference */
H2_API void h2_shadow_set_verify(bool on);

/* Attach chunks to new allocations only while active (mixed GC copying to SH) */
H2_API void h2_shadow_set_active(bool active);

/* At the end of a young/mixed GC, after h2_update_allocator_global_state() */
H2_API void h2_shadow_pause_end(void);

/*
 * At a young/mixed GC pause start only: switch up to max_chunks READY chunks
 * (0 = all) back to the file; a chunk whose final delta exceeds
 * max_delta_pages stays READY. Returns the chunks switched.
 */
H2_API uint32_t h2_shadow_switch(uint32_t max_chunks, long max_delta_pages);

H2_API void h2_shadow_get_stats(struct h2_shadow_stats *st);

/* True if addr is in an attached chunk (shadow memory) */
H2_API bool h2_shadow_is_attached(char *addr);

/* ------------------------------------------------------------------ */
/* Debug                                                               */
/* ------------------------------------------------------------------ */

/* Print the dependency count of every allocated region (stderr) */
H2_API void h2_print_groups(void);

/* Print the used regions (stderr) */
H2_API void h2_print_used_regions(void);

#ifdef __cplusplus
}
#endif

#endif /* H2_ALLOCATOR_H */
