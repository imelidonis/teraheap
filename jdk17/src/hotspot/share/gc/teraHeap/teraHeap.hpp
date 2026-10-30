#ifndef SHARE_GC_TERAHEAP_TERAHEAP_HPP
#define SHARE_GC_TERAHEAP_TERAHEAP_HPP

#include "gc/parallel/objectStartArray.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "utilities/stack.inline.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.hpp"
#include "gc/teraHeap/teraStatistics.hpp"

// Allocator functions
#include <h2_allocator.h>

class PSCardTable;

class TeraHeap: public CHeapObj<mtInternal> {
private:
  static char *_start_addr;       // TeraHeap start address of mmap region
  static char *_stop_addr;        // TeraHeap ends address of mmap region
  ObjectStartArray _start_array;  // Keeps track of where objects
                                  // start in a 2^CARD_SEGMENT_SIZE block
  size_t _h2_stripe_cards;        // H2 card-table stripe: the cards of one
                                  // region (H2RegionSize / card size)

  /*-----------------------------------------------
   * Stacks
   *---------------------------------------------*/
  // Full GC stacks of one GC thread. A thread pushes only to its own
  // stacks, so pushes take no lock. Each stack is filled by one Full GC
  // task and drained by a later one: the backward references by all
  // threads in parallel (thread w drains stacks w, w + n, ...), the
  // humongous regions by thread 0.
  struct H2FullGCStacks {
    // H2 fields with backward references (H2 to H1) found by the H2 card
    // scan: the H1 objects are marked live in phase 1 (marking) ...
    Stack<oop *, mtGC> mark;
    // ... and the fields are adjusted in phase 3 (adjust pointers)
    Stack<oop *, mtGC> adjust;
    // First regions of humongous objects to move to H2: pushed in phase 2
    // (prepare compaction), moved in phase 4 (compaction)
    Stack<HeapRegion *, mtGC> humongous;
  };
  PaddedEnd<H2FullGCStacks>* _fgc_stacks;   // one per GC thread

#ifdef ASSERT
  // Stack to keep moved h2 regions to verify they are free after Full GC
  static Stack<HeapRegion *, mtGC> _th_humongous_to_verify_stack;
#endif // ASSERT

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

  // h2_free_regions() callback (ctx: the TeraHeap): clears the start array
  // and the cards of a freed region
  static void h2_region_freed(char *region_start, char *last_alloc_start,
                              char *last_alloc_end, void *ctx);

  // TeraH2Shadow (allocator shadow.h): enabled, and the counters at the
  // start of the current pause for the [H2_SHADOW] line
  bool _shadow_on;
  bool _shadow_mixed;
  uint64_t _shadow_switch_skipped;           // READY chunks left by this pause's switch
  struct h2_shadow_stats _shadow_at_start;      // after this pause's switch
  struct h2_shadow_stats _shadow_at_last_end;   // at the previous pause end
  double _shadow_start_ms;                   // time of h2_shadow_pause_start() in this pause
  double _rand_faults_ms;                    // time of h2_enable_rand_faults() in the last pause
  double _alloc_state_ms;                    // time of update_allocator_state() in the last pause

  // Start the shadow chunks if TeraH2Shadow is set and the build and the
  // kernel support them
  void h2_shadow_init();

  // TeraH2AllocatorStats: regions allocated at the last print, and the
  // regions freed and the time to free them since then and in total
  uint64_t _as_last_allocated;
  uint64_t _as_taken_total;
  uint64_t _as_freed;
  uint64_t _as_freed_total;
  double _as_free_ms;
  double _as_free_ms_total;
public:
  // True if the GC workers time their H2 (SH) evacuation work and count the
  // bytes they move to H2: for the statistics, or for the H2 copy cost
  // that G1 learns and uses to choose the collection set
  static bool h2_cost_timers() {
#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
    return EnableTeraHeap && (TeraHeapStatistics || TeraH2CostPercentile > 0);
#else
    return false;
#endif
  }

  // Constructor
  TeraHeap();

  // Destructor
  ~TeraHeap();

  bool h2_verify_top() {
    bool res = ::h2_verify_top();

    if (!res) {
      fprintf(stderr, "[ERROR] Allocator bounds [%p, %p), top: %p\n",
              ::h2_start_addr_mem_pool(), ::h2_stop_addr_mem_pool(), ::h2_cur_alloc_ptr());
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

  // Number of Full GC stacks of each kind (one per GC thread)
  uint h2_num_fgc_stacks() const { return ParallelGCThreads; }

  // Empty the Full GC stacks and free their memory: at the start of a
  // Full GC, and at its end after checking that they were drained
  void h2_clear_fgc_stacks();

  // True if all Full GC stacks are empty
  bool h2_fgc_stacks_empty();

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
  char *h2_add_object(oop obj, size_t size, size_t worker_id);

  // Pop a backward reference from mark stack `stack_id`, or NULL if it is
  // empty. The H1 object it points to must be kept alive.
  oop* h2_pop_back_ref_to_mark(uint stack_id);

  // Push a backward reference (H2 field `p` to H1 object `o`) found by
  // thread `worker_id` to its mark and adjust stacks
  void h2_push_backward_reference(uint worker_id, void *p, oop o, bool to_young);

  // Push the first region of a humongous object that is marked to move
  // to H2 to the humongous stack of thread `worker_id`, to move it during
  // the compaction phase
  void h2_push_humongous_start(uint worker_id, HeapRegion *hr);

#ifdef ASSERT
  // Push a humongous region to a stack to verify that they are on the 
  // free-list after Full GC.
  void push_humongous_to_verify(HeapRegion *hr);
#endif // ASSERT

  // Pop a backward reference from adjust stack `stack_id`, or NULL if it
  // is empty
  oop* h2_pop_back_ref_to_adjust(uint stack_id);

  // Pop the first region of a humongous object to move to H2 from
  // humongous stack `stack_id`, or NULL if it is empty
  HeapRegion *h2_pop_humongous_start(uint stack_id);

  #ifdef ASSERT
  // Get the next humongous region to verify it is on the free list (only for debugging)
  HeapRegion *verify_next_humongous();
  #endif // ASSERT

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

  // TeraH2AllocatorStats: at the end of a young or mixed pause, print the
  // allocator statistics to the TeraHeap log; `tag` is YOUNG or MIXED
  void print_h2_alloc_stats(const char *tag);

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

  // Cards per stripe when GC workers scan the H2 card table in parallel:
  // one stripe per region, so that a stripe times h2_continuous_regions()
  // covers whole regions
  size_t h2_stripe_cards() const { return _h2_stripe_cards; }

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

  void update_allocator_state();

  // TeraH2Shadow: true if mixed GCs copy objects to H2 into DRAM chunks
  bool h2_shadow_enabled() const { return _shadow_on; }

  // At the start of a young/mixed pause, before any H2 work (mutators are
  // stopped and no JNI critical region is active): switch written-back
  // chunks back to the H2 file. In a mixed GC new H2 space gets chunks.
  void h2_shadow_pause_start(bool mixed);

  // TeraH2Shadow, in a mixed GC after h2_shadow_pause_start(): the bytes
  // that can still be copied to H2 into free pool chunks (DRAM); the rest
  // goes to the H2 file. 0 if shadow is off or the pause is not a mixed GC.
  size_t h2_shadow_budget_bytes() const;

  // TeraH2Shadow: true if H2 address `addr` is in a pool chunk (DRAM)
  bool h2_shadow_is_chunk(HeapWord* addr) const {
    return _shadow_on && ::h2_shadow_is_attached((char*) addr);
  }

  // After the evacuation and update_allocator_state(): hand the filled
  // chunks to the writer thread; print [H2_SHADOW] (TeraHeapStatistics)
  void h2_shadow_pause_end();

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

