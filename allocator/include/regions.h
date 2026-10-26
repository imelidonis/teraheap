#ifndef __REGIONS_H__
#define __REGIONS_H__

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

#include "h2_allocator.h"
#include "segments.h"

#if defined (__ia64__) || defined (__x86_64__)
#define INT_PTR unsigned long

#else
#define INT_PTR unsigned int
#endif

  extern uint64_t region_array_size;
  extern uint64_t max_rdd_id; //< Total different rdds

  extern volatile struct _mem_pool th_mem_pool; //< Allocator pool
  extern int fd;              //< File descriptor for the opended file
  extern int num_reqs;        //< Number of asynchronous write requests

#ifdef __cplusplus
}
#endif

#endif /* __REGIONS_H__ */
