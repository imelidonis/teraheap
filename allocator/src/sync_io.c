/*
 * Synchronous I/O to the H2 file (pwrite, fsync) and kernel hints (madvise)
 * for the H2 mapping.
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "h2_allocator.h"
#include "sharedDefines.h"
#include "mem_pool.h"

// Explicit write 'data' with 'size' in certain 'offset' using system call
// without memcpy.
void h2_write(char *data, char *offset, size_t size) {
#ifdef ASSERT
	ssize_t s_check = 0;
	uint64_t diff = offset - th_mem_pool.mmap_start;

	s_check = pwrite(fd, data, size * HeapWordSize, diff);
	assertf(s_check == size * HeapWordSize, "Sanity check: s_check = %ld", s_check);
#else
  uint64_t diff = offset - th_mem_pool.mmap_start;
  // The result is not checked in product builds (as before)
  ssize_t s_unused = pwrite(fd, data, size * HeapWordSize, diff);
  (void) s_unused;
#endif
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
