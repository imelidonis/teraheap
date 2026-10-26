// Promotion buffers: small objects are copied to a per-region buffer and written to H2 together.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "regions.h"
#include "region_types.h"
#include "sharedDefines.h"

#if PR_BUFFER

/*
 * Flush the promotion buffer of a certain region
 *
 * seg: Index of the region in region array
 *
 */
static void flush_buffer(uint64_t seg) {
	struct pr_buffer *buf = region_array[seg].pr_buffer;

	if (buf->size == 0)
		return;

	assertf(buf->size <= PR_BUFFER_SIZE, "Sanity check");

	// Write the buffer to TeraHeap
	h2_awrite(buf->buffer, buf->first_obj_addr, buf->size / HeapWordSize);

	buf->alloc_ptr = buf->buffer;
	buf->first_obj_addr = NULL;
	buf->size = 0;
}

/*
 * Add an obect to the promotion buffer. We use promotion buffer to avoid write
 * system calls for small sized objects.
 *
 * obj: Object that will be writter in the promotion buffer
 * new_adr: Is used to know where the first object in the promotion buffer will
 *			be move to H2
 * size: Size of the object
 */
void h2_buffer_insert(char* obj, char* new_adr, size_t size) {
	uint64_t seg = (new_adr - region_array_base) / ((uint64_t)REGION_SIZE);
	struct pr_buffer *buf = region_array[seg].pr_buffer;

  pthread_mutex_lock(&buf->buffer_lock);

	char*  start_adr  = buf->first_obj_addr;
	size_t cur_size   = buf->size;
	size_t free_space = PR_BUFFER_SIZE - cur_size;

	assertf(THRESHOLD < PR_BUFFER_SIZE, "Threshold should be less that promotion buffer size");

	if ((size * HeapWordSize) > THRESHOLD) {
    h2_awrite(obj, new_adr, size);
    pthread_mutex_unlock(&buf->buffer_lock);
    return;
	}

	/* Allocate a buffer for the region and set buffer allocation ptr */
	if (buf->buffer == NULL) {
		buf->buffer = malloc(PR_BUFFER_SIZE * sizeof(char));
		buf->alloc_ptr = buf->buffer;
	}

	/* Case1: Buffer is empty */
	if (cur_size == 0) {
		assertf(start_adr == NULL, "Sanity check");

		memcpy(buf->alloc_ptr, obj, size * HeapWordSize);

		buf->first_obj_addr = new_adr;
		buf->alloc_ptr += size * HeapWordSize;
		buf->size = size * HeapWordSize;

    pthread_mutex_unlock(&buf->buffer_lock);

		return;
	}
	
	/* 
	 * Case2: Object size is grater than the available free space in buffer
	 * Case3: Object's new address is not contignious with the other objects - 
	 * object belongs to next addresses in the region
	 * In both cases we flush the buffer and allocate the object in a new
	 * buffer.
	 */
	if (((size * HeapWordSize) > free_space) || ((start_adr + cur_size) != new_adr)) {
		flush_buffer(seg);
		
		memcpy(buf->alloc_ptr, obj, size * HeapWordSize);

		buf->first_obj_addr = new_adr;
		buf->alloc_ptr += size * HeapWordSize;
		buf->size = size * HeapWordSize;

    pthread_mutex_unlock(&buf->buffer_lock);

		return;
	}
	
	memcpy(buf->alloc_ptr, obj, size * HeapWordSize);

	buf->alloc_ptr += size * HeapWordSize;
	buf->size += size * HeapWordSize;

  pthread_mutex_unlock(&buf->buffer_lock);
}

/*
 * Flush all active buffers and free each buffer memory. We need to free their
 * memory to limit waste space.
 */
void h2_free_all_buffers() {
	uint64_t i;
	struct pr_buffer *buf;

  for (i = 0; i < region_array_size; i++) {
		buf = region_array[i].pr_buffer;

    pthread_mutex_lock(&buf->buffer_lock);

		/* Buffer is not empty, so flush it */
		if (buf->size != 0)
			flush_buffer(i);

		/* If the buffer is already flushed, just free bufffer's memory */
		if (buf->buffer != NULL) {
			free(buf->buffer);
			buf->buffer = NULL;
			buf->alloc_ptr = NULL;
			buf->first_obj_addr = NULL;
			buf->size = 0;
		}

    pthread_mutex_unlock(&buf->buffer_lock);
	}
}
#endif
