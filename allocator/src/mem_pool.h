#ifndef __MEM_POOL_H__
#define __MEM_POOL_H__

/*
 * H2 memory pool (internal): the H2 file, its mapping and the allocation
 * pointer. Defined in mem_pool.c; the I/O code (sync_io.c, async_io.c) uses
 * the file descriptor and the mapping start.
 */

#include <stdint.h>

struct _mem_pool{
    char *mmap_start;                                   //< Memory mapped allocation start addresss
    char* start_address;                                //< Aligned start address of TeraCache
    char* cur_alloc_ptr;                                //< Current allocation pointer of TeraCache
    char* stop_address;                                 //< Last address of TeraCache

    uint64_t size;                                              //< Current allocated bytes in TeraCache
};

extern volatile struct _mem_pool th_mem_pool;  //< Allocator pool
extern int fd;                                 //< File descriptor of the H2 file
extern uint64_t dev_size;                      //< Size of the H2 file (bytes)

#endif /* __MEM_POOL_H__ */
