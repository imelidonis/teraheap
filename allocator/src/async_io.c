#include <asm-generic/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <aio.h>
#include <unistd.h>
#include <pthread.h>
#include "async_io.h"
#include "mem_pool.h"
#include "shadow.h"

// Application-defined structure for tracking I/O requests
struct ioRequest {    
	int          state;	  // Status of request
	char *		   buffer;  // Internal buffer
	struct aiocb aiocbp;	// Asynchronous I/O control block
};

static struct ioRequest request[MAX_REQS];
static pthread_mutex_t request_lock;

// Initialize the array of I/O requests for the asynchronous I/O
void req_init() {
	int i;

  pthread_mutex_init(&request_lock, NULL);

	for (i = 0; i < MAX_REQS; i++) {
		request[i].state = 0;
		request[i].buffer = NULL;
	}
}

// If request i is in progress, update its state with aio_error() and free
// its buffer when it has completed. Returns the state of the request.
static int req_poll(int i) {
	if (request[i].state != EINPROGRESS)
		return request[i].state;

	request[i].state = aio_error(&request[i].aiocbp);

	switch (request[i].state) {
		case 0:
			free(request[i].buffer);
			request[i].buffer = NULL;
			break;

		case EINPROGRESS:
			break;

		case ECANCELED:
			assertf(0, "Request cacelled");
			break;

		default:
			assertf(0, "AIO_ERROR %d", request[i].state);
			break;
	}
	return request[i].state;
}

// Check to find available slot in the i/o request array for the new i/o
// request.
// Return the 'index' of the available slot in the array, or return '-1' if all
// the slots are active and allocated.
static int find_slot() {
	static volatile int i = 0;

	for (; i < MAX_REQS; i++) {
		if (req_poll(i) == 0)
			return i;
	}

	i = 0;
	return -1;
}

// Add new I/O request in the array
// Arguments:
//	fd	   - File descriptor
//	data   - Data to be transfered to the device
//	size   - Size of the data
//	offset - Write the data to the specific offset in the file
//	
static void req_add(int fd, char *data, size_t size, uint64_t offset) {
  pthread_mutex_lock(&request_lock);
  int slot;					// Find available slot for the request

	// Wait here until find an available slot for the request
	while ((slot = find_slot()) == -1)
		;

	// Create and initialize the aiocb structure.
	// If we don't init to 0, we have undefined behavior.
	// E.g. through sigevent op.aio_sigevent there could be a callback function
	// being set, that the program tries to call - which will then fail.
  struct aiocb obj = {0};

	obj.aio_fildes = fd;
	obj.aio_offset = offset;
	request[slot].buffer = malloc(size * sizeof(char));
	memcpy(request[slot].buffer, data, size);
	obj.aio_buf = request[slot].buffer;
	obj.aio_nbytes = size;            

	request[slot].state = EINPROGRESS;
	request[slot].aiocbp = obj;

#ifdef ASSERT
	int check = aio_write(&request[slot].aiocbp);
	assertf(check == 0, "Write failed");
#else
	aio_write(&request[slot].aiocbp);
#endif
  pthread_mutex_unlock(&request_lock);
}

// Explicit asynchronous write 'data' with 'size' in certain 'offset' using
// system call without memcpy.
// Do not use h2_awrite() with h2_write()
static void awrite_bytes(char *data, char *offset, size_t bytes) {
  req_add(fd, data, bytes, offset - th_mem_pool.mmap_start);
}

void h2_awrite(char *data, char *offset, size_t size) {
  // Parts inside shadow chunks are copied into them: a write to the file
  // there would land behind the chunk and be lost (shadow.h)
  shadow_split_write(data, offset, size * HeapWordSize, awrite_bytes);
}

// Check if all the asynchronous requestes have been completed (updates the
// state of each request in progress)
// Return 1 on succesfull, and 0 otherwise
int	h2_areq_completed() {
	for (int i = 0; i < MAX_REQS; i++) {
		if (req_poll(i) == EINPROGRESS)
			return 0;
	}
	return 1;
}
