/**************************************************
 *
 * file: sharedDefines.h
 *
 * @Author:   Iacovos G. Kolokasis
 * @Version:  21-05-2020
 * @email:    kolokasis@ics.forth.gr
 *
 ***************************************************
 */

#ifndef _SHARE_DEFINES_H_
#define _SHARE_DEFINES_H_


 /************************************
  * Source code that we need to remove after testing
  ************************************/

// #define TERA_DEBUG
#ifdef TERA_DEBUG
  
  #include <iostream>
  #define stdprint std::cerr

	#define TERA_REMOVEx(code) 
	#define TERA_REMOVE(code)

  // Debug TeraHeap (REMOVE)----
  // #define TERA_DBG_PHASES
  // #define TERA_DBG_VERIFY_H2
  // #define TERA_DBG_VERIFY_CT
  // ---------------------------


#else
	#define TERA_REMOVEx(code)
	#define TERA_REMOVE(code)
#endif  // TERA_DEBUG

// #define TERA_AVOID_FULL_GC       // if full gc is not aware of H2, this should be enabled
#define TERA_LOG				          // Define logging for TeraHeap

#define TERA_FLAG				         // Define teraFlag word
#define TERA_ASSERT              // Extended assertions for TeraHeap
#define TERA_MAINTENANCE         // if objs are transfered to H2, these should be enabled : check-ups, fence h2 heap
                                 // helps for the independent development of g1 full gc

#define TERA_CARDS               // Enable Teraheap card table and scan h2 card table for back refs during evacuations

#define TERA_CONC_MARKING        // Do the marking : mark transitive closure, old region statistics (h2 liveness) , sort based on garbage-1st policy 
#define TERA_EVAC_MOVE           // Move objs that have their tera flag enabled, to h2

#define DISABLE_TRAVERSE_OLD_GEN  // Enables CT optimization to avoid scanning H2 regions with pointers to Old Gen during Young GC

// #define BACK_REF_STAT            //< Collect statistics for backward
                                    //refenrences. Works only with -XX:GCThreads=1

//#define FMAP_HYBRID				       //< When we use fastmap hybrid version we
                                   // employ huge pages for mutator threads and
                                   // regular pages for GC 

#define MADVISE_ON				        //< During minor gc we advise kernel for
                                  // random accesses. During mutator thread
                                  // execution we advise kernel for sequential
                                  // accesses

#define TERA_CARD_SIZE			 13   // This is the size of each card in
                                  // TeraHeap card table. The size is in bit
                                  // e.g 9 = 512bytes
                                  // 2^13 = 8K = 8*1024

#define TERA_INTERPRETER	        // Enable Interpreter to support TeraHeap

#define TERA_C1				            //< Enable C1 to support TeraHeap

#define TERA_C2				            //< Enable C2 to support TeraHeap

// #define C2_ONLY_LEAF_CALL		    //< C2 Compiler version - Comparisons and
                                  // card marking are all implemented in the
                                  // make_leaf_call()

//#define FASTMAP                 // Enable this define when you run
                                  // with fastmap with enabled
                                  // -XX:AllocateHeapAt="/mnt/dir"
                                  // or -XX:AllocateOldGenAt="/mnt/dir"




/**********************************
 * Write Mode to H2
 **********************************/
// #define SYNC				            //< Enable explicit I/O path for the writes
                                // in TeraHeap during major GC


// #define ASYNC				          //< Asynchronous I/O path for the writes in
// #define PR_BUFFER			        //< Enable promotion buffer for async I/O to
                                // reduce the number of system calls 


// #define FMAP				            //< When we use fastmap we need to ensure
                                // that all the writes in buffered cached
                                // will be flushed to the device because the
                                // memory of fast map is different from
                                // buffer cache. 

/**********************************
 * Statistics
 **********************************/
 // #define FWD_REF_STAT               //< Collect statistics for class object

 /**********************************
  * States of TeraFlag
  **********************************/
#define MOVE_TO_TERA			255	    //< Move this object to tera cache
#define DUMMY_H2_ADDRESS  256     //< Mark that object took address.
//                                // NOTE: will be removed.

#define TERA_TO_OLD 	    328     //< Pointer from TeraHeap to Old Gen. Move
                                  // this object to TeraHeap

#define IN_TERA_CACHE     2147483561  //< This object is located in TeraHeap

#define INIT_TF				    2035    //< Initial object state

#define INIT_TF_HEX			  0x7f3U  //< Initial object state

#define LIVE_TERA_OBJ     202     //< Object marked as live during GC Analysis

#define VISITED_TERA_OBJ  203     //< Object visited during GC Analysis

// #define RUSAGE_MUTATOR  //< Enables rusage measuremetns excluding STW GCs

#endif  // _SHARE_DEFINES_H_
