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

_Static_assert(THRESHOLD < PR_BUFFER_SIZE, "THRESHOLD must be less than PR_BUFFER_SIZE");

/*
 * Write the objects in the promotion buffer `buf` to H2 and empty it.
 * Called with buf->buffer_lock held.
 */
static void flush_buffer(struct pr_buffer *buf) {
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
 * size: Size of the object (in words)
 */
void h2_buffer_insert(char* obj, char* new_adr, size_t size) {
	struct pr_buffer *buf = region_of(new_adr)->pr_buffer;
	size_t bytes = size * HeapWordSize;

	pthread_mutex_lock(&buf->buffer_lock);

	if (bytes > THRESHOLD) {
		// Big objects are written directly
		h2_awrite(obj, new_adr, size);
		pthread_mutex_unlock(&buf->buffer_lock);
		return;
	}

	/* Allocate a buffer for the region and set buffer allocation ptr */
	if (buf->buffer == NULL) {
		buf->buffer = malloc(PR_BUFFER_SIZE * sizeof(char));
		buf->alloc_ptr = buf->buffer;
	}

	/*
	 * Flush the buffer if the object does not fit in its free space, or if
	 * its new address does not follow the buffered objects
	 */
	if (buf->size != 0 &&
	    (bytes > PR_BUFFER_SIZE - buf->size || buf->first_obj_addr + buf->size != new_adr)) {
		flush_buffer(buf);
	}

	if (buf->size == 0) {
		assertf(buf->first_obj_addr == NULL, "Sanity check");
		buf->first_obj_addr = new_adr;
	}

	memcpy(buf->alloc_ptr, obj, bytes);
	buf->alloc_ptr += bytes;
	buf->size += bytes;

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

		flush_buffer(buf);

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
