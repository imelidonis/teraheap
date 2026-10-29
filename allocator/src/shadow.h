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
 *  - Only the region's owner attaches during a pause (regions grouped by GC
 *    thread, h2_init(): one allocating GC worker per region).
 *  - Write-protect is armed before the writer's first copy of a chunk.
 *  - h2_shadow_switch() runs only in young/mixed GC pauses (mutators stopped,
 *    no JNI critical region active).
 *  - A region with attached chunks is not handed out again
 *    (get_cont_regions() checks shadow_range_busy()).
 *  - If the pool has no free chunk, allocation goes to the file mapping as
 *    before; both use the same addresses.
 *
 * Requires Linux >= 6.7 (UFFD_FEATURE_WP_ASYNC, PAGEMAP_SCAN). shadow_init()
 * fails otherwise and the allocator works as before.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "h2_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHADOW_CHUNK H2_SHADOW_CHUNK   // struct h2_shadow_stats is in h2_allocator.h

// Set up the pool (up to pool_bytes), userfaultfd and the pagemap fd for the
// H2 file mapping [map_start, map_start + map_size) of file `fd` (file offset
// = address - map_start). h2_start is the 2 MB aligned start of the regions,
// region_size the allocator's region size (a power of two, >= 2 MB).
// precopy_rounds: max pre-copy rounds per chunk. Returns 0, or -1 with a
// message on stderr (the allocator then works as before).
// The region queries come from the region code (regions.c):
//  - region_info(addr, &top, &active): the allocation top of the region that
//    contains addr, and whether a (thread, partition) id still allocates in it;
//  - region_free_prot(addr): the protection to map a chunk of a freed region
//    with.
int  shadow_init(int fd, char *map_start, size_t map_size, char *h2_start,
                 uint64_t region_size, size_t pool_bytes, int precopy_rounds, int n_threads,
                 void (*region_info)(char *addr, char **top, bool *active),
                 int (*region_free_prot)(char *addr));

// Called by h2_allocate() for the object [pos, pos + bytes) of GC thread
// `thread_id`: attaches the empty chunks it covers. Cheap when nothing to do.
void shadow_ensure(char *pos, size_t bytes, uint64_t thread_id);

// Attach the empty chunks of [top, top + bytes) ahead of a region's top, up
// to the region's end, outside pauses (the region's owner must not allocate
// concurrently).
// Returns the chunks attached.
int  shadow_preattach(char *top, size_t bytes);

// Writer thread: one unit of work: detach DEAD chunks, write back one FULL
// chunk with its pre-copy rounds, or else refresh the READY chunks (copy
// again the pages written since their last copy, so a switch has little
// left to copy), and refill the pool. Returns the bytes written back (FULL
// chunks), 0 otherwise.
size_t shadow_writeback_step(void);

// Called by h2_free_regions() for each freed region: its chunks become DEAD and
// are detached without writing (a chunk under write-back is detached by the
// writer when its write returns).
void shadow_free_range(char *start, size_t len);

// True if [start, start + len) still has attached chunks
bool shadow_range_busy(char *start, size_t len);

// h2_awrite() helper: copies the parts of [addr, addr + len) that are in
// attached chunks from `data`, and calls write_file(data, addr, len) for the
// other parts.
void shadow_split_write(char *data, char *addr, size_t len,
                        void (*write_file)(char *data, char *addr, size_t len));

#ifdef __cplusplus
}
#endif

#endif // __SHADOW_H__
