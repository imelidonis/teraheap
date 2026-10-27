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

#define STATISTICS  0                 //< Print allocator statistics

#define DEBUG_PRINT 0                 //< Debug prints

/* ------------------------------------------------------------------ */
/* Sizes                                                               */
/* ------------------------------------------------------------------ */

#define HeapWordSize 8                    //< Java heap word (bytes)

#define REGION_SIZE (256*1024LU*1024)     //< Region size (bytes)

#define MAX_REQS    64                    //< Maximum async I/O requests

#define PR_BUFFER_SIZE (2*1024LU*1024)    //< Promotion buffer size (bytes)

/* Objects bigger than this are written directly with async I/O; smaller
 * objects go through the promotion buffer. Must be less than PR_BUFFER_SIZE */
#define THRESHOLD (1*1024LU*1024)

#endif /* __H2_CONFIG_H__ */
