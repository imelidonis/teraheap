#ifndef __H2_CONFIG_H__
#define __H2_CONFIG_H__

/*
 * Compile-time options and sizes of the H2 allocator (internal).
 * All the allocator's options are here; nothing outside the allocator
 * depends on them.
 */

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

// #define ASSERT                     //< Enable assertf() (make DEBUG=1 sets it)

#define ANONYMOUS  0                  //< 1: anonymous small mmaps instead of the H2 file

#define MALLOC_ON  1                  //< Allocate async I/O buffers dynamically

#define PR_BUFFER  1                  //< Promotion buffers for small objects

#define STATISTICS  0                 //< Print allocator statistics

#define DEBUG_PRINT 0                 //< Debug prints

#define LOCKFREE_GLOBAL_UPDATE        //< Per-thread allocation counters, folded
                                      // in h2_update_allocator_global_state()

#define CUSTOM_GROUPING               //< Group regions by GC thread instead of
                                      // by rdd (one allocating thread per region)

/* ------------------------------------------------------------------ */
/* Sizes                                                               */
/* ------------------------------------------------------------------ */

#define HeapWordSize 8                    //< Java heap word (bytes)

#define REGION_SIZE (256*1024LU*1024)     //< Region size (bytes)

#define MAX_REQS    64                    //< Maximum async I/O requests

#define BUFFER_SIZE (8*1024LU*1024)       //< Buffer size (bytes) for async I/O

#define PR_BUFFER_SIZE (2*1024LU*1024)    //< Promotion buffer size (bytes)

/* Objects bigger than this are written directly with async I/O; smaller
 * objects go through the promotion buffer. Must be less than PR_BUFFER_SIZE */
#define THRESHOLD (1*1024LU*1024)

#define MMAP_SIZE (4*1024*1024)           //< Size of small mmaps (ANONYMOUS)

#if ANONYMOUS
  #define V_SPACE (100*1024LU*1024*1024*1024) //< Virtual address space for small mmaps
  #define REGION_ARRAY_SIZE ((V_SPACE)/(REGION_SIZE))
  #define MAX_PARTITIONS 256                  //< Maximum partitions per RDD (id array size)
  #define MAX_RDD_ID ((REGION_ARRAY_SIZE)/(MAX_PARTITIONS)) //< Total different rdds
#else
  #define MAX_PARTITIONS 256                  //< Maximum partitions per RDD (id array size)
#endif

#define GROUP_ARRAY_SIZE ((REGION_ARRAY_SIZE)/2)

#endif /* __H2_CONFIG_H__ */
