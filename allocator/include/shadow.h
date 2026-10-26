#ifndef __SHADOW_H__
#define __SHADOW_H__

/*
 * SH shadow chunks with deferred write-back.
 *
 * During a mixed GC the SH (H2) addresses that GC workers copy objects to are
 * backed by pre-faulted anonymous DRAM chunks of SHADOW_CHUNK bytes, moved to
 * their final addresses with mremap(). The GC writes at the final addresses,
 * so nothing in the copy path changes. After the pause a writer thread copies
 * each filled chunk into the H2 file with pwrite(); at a later young/mixed GC
 * pause the file mapping is put back over the chunk.
 *
 * Writes made to a chunk after its copy started are found with userfaultfd
 * async write-protect (UFFD_FEATURE_WP_ASYNC) and PAGEMAP_SCAN, and copied
 * again before the switch, so none is lost.
 *
 * Chunk states (per chunk of the H2 address range):
 *   FILE     backed by the H2 file mapping (the default)
 *   OPEN     shadow; its region's allocation top may still be inside it
 *   FULL     shadow; filled up, waits for the writer
 *   WRITING  shadow; the writer copies it to the file
 *   READY    shadow; copied, switched at the next young/mixed pause
 *   DEAD     shadow; its region was freed, detached without writing
 *
 * Rules:
 *  - Only empty chunks are attached (no object allocated in them yet).
 *  - Only the region's owner attaches during a pause (CUSTOM_GROUPING: one
 *    allocating GC worker per region).
 *  - Write-protect is armed before the writer's first copy of a chunk.
 *  - shadow_switch() runs only in young/mixed GC pauses (mutators stopped,
 *    no JNI critical region active).
 *  - A region with attached chunks is not handed out again
 *    (get_cont_regions() checks shadow_range_busy()).
 *  - If the pool has no free chunk, allocation goes to the file mapping as
 *    before; both use the same addresses.
 *
 * Requires Linux >= 6.7 (UFFD_FEATURE_WP_ASYNC, PAGEMAP_SCAN) and the file
 * mapping mode (ANONYMOUS == 0). shadow_init() fails otherwise and the
 * allocator works as before.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHADOW_CHUNK (2 * 1024LU * 1024)   // 2 MB: one transparent huge page

struct shadow_stats {
  // Totals since shadow_init()
  uint64_t attached;          // Chunks attached
  uint64_t attach_failed;     // Chunk entries that went to the file (no free pool chunk)
  uint64_t written_back;      // Chunks copied to the file by the writer
  uint64_t written_bytes;     // Bytes written by the writer (incl. pre-copy)
  uint64_t precopy_pages;     // Pages copied again during pre-copy rounds
  uint64_t refresh_pages;     // Pages of READY chunks copied again by the writer
  uint64_t switched;          // Chunks switched back to the file
  uint64_t switch_skipped;    // READY chunks left for a later pause (delta too big)
  uint64_t delta_pages;       // Pages copied again at switches
  uint64_t dead;              // Chunks of freed regions
  uint64_t dead_in_flight;    // ... of them freed while the writer worked on them
  uint64_t dead_by_state[6];  // ... by state when freed (index: FILE..DEAD order)
  uint64_t empty_detached;    // OPEN chunks returned unused (retired region)
  uint64_t redirected_bytes;  // r_awrite() bytes copied into chunks
  // Current
  uint32_t pool_chunks;       // Populated pool chunks
  uint32_t free_chunks;       // Pool chunks not attached
  uint32_t n_open, n_full, n_writing, n_ready, n_dead;
  // Last shadow_switch()
  double   last_switch_ms;
  uint32_t last_switched;
  uint64_t last_delta_pages;
};

// Set up the pool (up to pool_bytes), userfaultfd and the pagemap fd for the
// H2 file mapping [map_start, map_start + map_size) of file `fd` (file offset
// = address - map_start). h2_start is the 2 MB aligned start of the regions.
// precopy_rounds: max pre-copy rounds per chunk. Returns 0, or -1 with a
// message on stderr (the allocator then works as before).
int  shadow_init(int fd, char *map_start, size_t map_size, char *h2_start,
                 size_t pool_bytes, int precopy_rounds, int n_threads);

// True after a successful shadow_init()
bool shadow_enabled(void);

// Attach chunks to new allocations only while active (a mixed GC copying to
// SH). Inactive: allocations go to whatever backs the address (Full GC).
void shadow_set_active(bool active);

// Called by allocate() for the object [pos, pos + bytes) of GC thread
// `thread_id`: attaches the empty chunks it covers. Cheap when nothing to do.
void shadow_ensure(char *pos, size_t bytes, uint64_t thread_id);

// Attach the empty chunks of [top, top + bytes) ahead of a region's top, up
// to the region's end, outside pauses (the region's owner must not allocate
// concurrently).
// Returns the chunks attached.
int  shadow_preattach(char *top, size_t bytes);

// At the end of a young/mixed GC (after update_allocator_global_state()):
// OPEN chunks that their region's top has passed become FULL; unused OPEN
// chunks of retired regions return to the pool.
void shadow_pause_end(void);

// Writer thread: one unit of work: detach DEAD chunks, write back one FULL
// chunk with its pre-copy rounds, or else refresh the READY chunks (copy
// again the pages written since their last copy, so a switch has little
// left to copy), and refill the pool. Returns the bytes written back (FULL
// chunks), 0 otherwise.
size_t shadow_writeback_step(void);

// At a young/mixed GC pause start only: switch up to max_chunks READY chunks
// (0 = all) back to the file. A chunk whose final delta exceeds
// max_delta_pages stays READY. Returns the chunks switched.
uint32_t shadow_switch(uint32_t max_chunks, long max_delta_pages);

// Called by free_regions() for each freed region: its chunks become DEAD and
// are detached without writing (a chunk under write-back is detached by the
// writer when its write returns).
void shadow_free_range(char *start, size_t len);

// True if [start, start + len) still has attached chunks
bool shadow_range_busy(char *start, size_t len);

// True if addr is in an attached chunk (shadow memory)
bool shadow_is_attached(char *addr);

// r_awrite() helper: copies the parts of [addr, addr + len) that are in
// attached chunks from `data`, and calls write_file(data, addr, len) for the
// other parts.
void shadow_split_write(char *data, char *addr, size_t len,
                        void (*write_file)(char *data, char *addr, size_t len));

void shadow_get_stats(struct shadow_stats *st);

// Start a writer thread that runs shadow_writeback_step() whenever there is
// work; shadow_pause_end() wakes it. Returns 0, or -1.
int  shadow_start_writer(void);

// Debug: at each switch, compare every chunk with the file (pread) after its
// final re-copy and abort on a difference.
void shadow_set_verify(bool on);

#ifdef __cplusplus
}
#endif

#endif // __SHADOW_H__
