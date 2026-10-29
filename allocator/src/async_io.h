#ifndef __ASYNC_IO_H__
#define __ASYNC_IO_H__

#include <aio.h>
#include <stdint.h>
#include "sharedDefines.h"

#ifdef __cplusplus
extern "C" {
#endif

	// Initialize the array of I/O requests for the asynchronous I/O
	void	req_init(void);
	
#ifdef __cplusplus
}
#endif

#endif // __ASYNC_IO_H__ 
