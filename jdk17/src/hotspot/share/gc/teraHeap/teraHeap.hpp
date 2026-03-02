#ifndef SHARE_GC_TERAHEAP_TERAHEAP_HPP
#define SHARE_GC_TERAHEAP_TERAHEAP_HPP

#include "gc/parallel/objectStartArray.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "utilities/stack.inline.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.hpp"
#include "gc/teraHeap/teraStatistics.hpp"

// Allocator functions
#include <regions.h>

class PSCardTable;

class TeraHeap: public CHeapObj<mtInternal> {
private:
  static char *_start_addr;       // TeraHeap start address of mmap region
  static char *_stop_addr;        // TeraHeap ends address of mmap region
  ObjectStartArray _start_array;  // Keeps track of where objects
                                  // start in a 2^CARD_SEGMENT_SIZE block

  /*-----------------------------------------------
   * Stacks
   *---------------------------------------------*/
  // Stack to keep back pointers (Objects that are pointed out of
  // TeraHeap objects) to mark them as alive durin mark_and_push phase of
  // the Full GC.
  static Stack<oop *, mtGC> _th_stack;

  // Stack to keep the element addresses of objects that are located in
  // TeraHeap and point to objects in the heap. We adjust these pointers
  // during adjust phase of the Full GC.
  static Stack<oop *, mtGC> _th_adjust_stack;

  // Stack to keep the humongous objects that are marked to move to H2.
  // We drain this stack in the compaction phase of a Full GC.
  static Stack<HeapRegion *, mtGC> _th_humongous_stack;
  /*---------------------------------------------*/

  TeraStatistics *tera_stats;

  HeapWord **h1_addr_arr;
  HeapWord **h2_addr_arr;

  HeapWord *obj_h1_addr;  // We need to check this
                          // object that will be moved
                          // to H2 if it has back ptrs
                          // to H1

  HeapWord *obj_h2_addr;  // We need to check this
                          // object that will be moved
                          // to H2 if it has back ptrs
                          // to H1

  static CardTable* th_card_table(void);
public:
  // Constructor
  TeraHeap();

  // Destructor
  ~TeraHeap();

  bool h2_verify_top() {
    bool res = verify_top();

    if (!res) {
      fprintf(stderr, "[ERROR] Allocator bounds [%p, %p), top: %p\n",
              start_addr_mem_pool(), stop_addr_mem_pool(), cur_alloc_ptr());
    }

    return res; 
  }

  // Get object start array for h2
  ObjectStartArray *h2_start_array() { return &_start_array; }

  // Return H2 start address
  char *h2_start_addr(void);

  // Return H2 stop address
  char *h2_end_addr(void);

  // Get the top allocated address of the H2. This address depicts the
  // end address of the last allocated object in the last region of
  // H2.
  char *h2_top_addr(void);

  // Check if H2 is empty.
  // Return true if H2 is empty, false otherwise
  bool h2_is_empty(void);

  // Check if a pointer belongs to the TeraHeap.
  bool is_in_h2(const void* p);

  // Deallocate the backward references stacks
  void h2_clear_back_ref_stacks();

  // Deallocate the humongous region stack
  void h2_clear_humongous_stack();

  // Give advise to kernel to expect page references in sequential order
  void h2_enable_seq_faults();

  // Give advise to kernel to expect page references in random order
  void h2_enable_rand_faults();

  // Check if the first object `obj` in the H2 region is valid. If not
  // that depicts that the region is empty
  bool check_if_valid_object(HeapWord *obj);

  // Traverses all objects in H2 to check if they are valid.
  bool check_if_valid_h2();

  // Get the ending address of the last object of the region obj
  // belongs to.
  HeapWord *get_last_object_end(HeapWord *obj);

  // Checks if the address of obj is the beginning of a region
  bool is_start_of_region(HeapWord *obj);

  // Retrurn the start address of the first object of the secific region
  HeapWord *get_first_object_in_region(HeapWord *addr);

  // Add new object in the region
  char *h2_add_object(oop obj, size_t size);

  // Pop the objects that are in `_th_stack`. These objects are
  // located in the Java Heap and we need to ensure that they will be
  // kept alive.
  oop* h2_get_next_back_reference();

  // Update backward reference stacks that we use in marking and
  // pointer adjustment phases of major GC.
  void h2_push_backward_reference(void *p, oop o);

  // Add the first humongous region of a humongous object
  // that is marked to move to H2 in a seperate stack to
  // move them during the compaction phase.
  void h2_push_humongous_start(void *p);

  // Get the next backward reference from the stack to adjust
  oop* h2_adjust_next_back_reference();

  // Get the next humongous starting region from the stack to move the whole object to H2
  HeapRegion *h2_get_next_humongous_start();

  // Explicit (using systemcall) write 'data' with 'size' to the specific
  // 'offset' in the file.
  void h2_write(char *data, char *offset, size_t size);

  // Explicit (using systemcall) asynchronous write 'data' with 'size' to
  // the specific 'offset' in the file.
  void h2_awrite(char *data, char *offset, size_t size);

  // We need to ensure that all the writes in TeraHeap using asynchronous
  // I/O have been completed succesfully.
  int h2_areq_completed();

  // Fsync writes in TeraHeap
  // We need to make an fsync when we use fastmap
  void h2_fsync();

#ifdef PR_BUFFER
  // Add an object 'obj' with size 'size' to the promotion buffer. 'New_adr' is
  // used to know where the object will move to H2. We use promotion buffer to
  // reduce the number of system calls for small sized objects.
  void h2_promotion_buffer_insert(char *obj, char *new_adr, size_t size);

  // At the end of the major GC flush and free all the promotion
  // buffers.
  void h2_free_promotion_buffers();
#endif

  // Resets the used field of all regions
  void h2_reset_used_field(void);

  // Marks the region containing obj as used
  void mark_used_region(HeapWord *obj);

  // Prints all active regions
  void print_h2_active_regions(void);

  // If the current thread is relocating an object to H2, record cross-heap / cross-H2-region
  // reference metadata (region dependency or H2 card marking) for the reference slot `obj_field`.
  // No-op if relocation context is not active.
  void check_for_cross_heap_cross_h2_region_ref(uint thread_id, HeapWord *obj, void *obj_field);

  // Frees all unused regions
  void free_unused_regions(void);

  // Prints all the region groups
  void print_region_groups(void);

  // Enables groupping with region of obj (multi-threaded)
  void thread_enable_groups(uint thread_id, HeapWord *old_addr, HeapWord *new_addr);

  // Disables region groupping (multi-threaded)
  void thread_disable_groups(uint thread_id);

  void print_object_name(HeapWord *obj, const char *name);

  // Add a new entry to `obj1` region dependency list that reference
  // `obj2` region
  void group_regions(HeapWord *obj1, HeapWord *obj2);

  // Iterate over all objects in each region and print their states
  // This function is for debugging purposes to understand and fix the
  // locality in regions
  void h2_print_objects_per_region(void);

  // Check if backward adjust stack is empty
  bool h2_is_empty_back_ref_stacks();

  // Get the group Id of the objects that belongs to this region. We
  // locate the objects of the same group to the same region. We use the
  // field 'p' of the object to identify in which region the object
  // belongs to.
  uint64_t h2_get_region_groupId(void *p);

  // Get the partition Id of the objects that belongs to this region. We
  // locate the objects of the same group to the same region. We use the
  // field 'p' of the object to identify in which region the object
  // belongs to.
  uint64_t h2_get_region_partId(void *p);

  // Check if the object `obj` is an instance of the following
  // metadata class:
  // - Instance Mirror Klass
  // - Instance Reference Klass
  // - Instance Class Loader Klass
  // If yes return true, otherwise false
  bool is_metadata(oop obj);

  // Check if the object with `addr` span multiple regions
  int h2_continuous_regions(HeapWord *addr);

  // Check where the object starts
  bool h2_object_starts_in_region(HeapWord *obj);

  // Move object with size 'size' from source address 'src' to the h2
  // destination address 'dst' 
  void h2_move_obj(HeapWord *src, HeapWord *dst, size_t size, bool is_fgc = false);

  // Complete the transfer of the objects in H2
  void h2_complete_transfers();

  // Check if the group of regions in H2 is enabled
  bool is_h2_group_enabled();

  // Tera statistics for objects that we move to H2, forward references,
  // and backward references.
  TeraStatistics* get_tera_stats();

  // ------------------
  // Utility functions
  // ------------------
  // Make every card of H2 dirty (used for debugging)
  void dirty_all_cards();
  // ------------------

  void print_sigsegv_info(void *siginfo);

  bool is_in_reclaimed_region(char *addr);
};

#endif

