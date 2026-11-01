/*
 * Synchronous I/O to the H2 file (pwrite, fsync) and kernel hints (madvise)
 * for the H2 mapping.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "h2_allocator.h"
#include "sharedDefines.h"
#include "mem_pool.h"
#include "shadow.h"

// Write 'bytes' from 'data' to the H2 address 'addr' with pwrite()
static void pwrite_bytes(char *data, char *addr, size_t bytes) {
  off_t off = addr - th_mem_pool.mmap_start;
  while (bytes > 0) {
    ssize_t n = pwrite(fd, data, bytes, off);
    if (n < 0 && errno == EINTR)
      continue;
    assertf(n > 0, "pwrite to H2 failed: n = %ld", (long) n);
    if (n <= 0)
      return;   // as before, product builds do not stop on a failed write
    data += n;
    off += n;
    bytes -= (size_t) n;
  }
}

// Explicit write 'data' with 'size' (in HeapWords) to the H2 address
// 'offset' using system calls. Parts inside shadow chunks are copied into
// them: a write to the file there would land behind the chunk and be lost
// (shadow.h)
void h2_write(char *data, char *offset, size_t size) {
  shadow_split_write(data, offset, size * HeapWordSize, pwrite_bytes);
}
	
// We need to ensure that all the writes will be flushed from buffer
// cur_alloc_ptrcur_alloc_ptrhe and they will be written to the device.
void h2_fsync() {
#ifdef ASSERT
	int check = fsync(fd);
	assertf(check == 0, "Error in fsync");
#else
	fsync(fd);
#endif
}

// This function if for the FastMap hybrid version. Give advise to kernel to
// serve all the pagefault using regular pages.
void h2_enable_regular_flts(void) {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_NOHUGEPAGE);
}

// This function if for the FastMap hybrid version. Give advise to kernel to
// serve all the pagefault using huge pages.
void h2_enable_huge_flts(void) {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_HUGEPAGE);
}

// Give advise to kernel to expect page references in sequential order.  (Hence,
// pages in the given range can be aggressively read ahead, and may be freed
// soon after they are accessed.)
void h2_enable_seq() {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_SEQUENTIAL);
}

// Give advise to kernel to expect page references in random order (Hence, read
// ahead may be less useful than normally.)
void h2_enable_rand() {
  madvise(th_mem_pool.mmap_start, dev_size, MADV_NORMAL);
}
