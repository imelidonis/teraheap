#ifndef SHARE_GC_TERAHEAP_TERAHEAP_HPP
#define SHARE_GC_TERAHEAP_TERAHEAP_HPP

// The small functions on the GC hot paths that need other inline headers
// (oops, stacks, card table) are in teraHeap.inline.hpp: include it to call
// them.

#include "gc/parallel/objectStartArray.hpp"
#include "gc/teraHeap/teraStatistics.hpp"
#include "memory/allocation.hpp"
#include "memory/padded.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.hpp"
#include "runtime/globals.hpp"
#include "utilities/debug.hpp"
#include "utilities/stack.hpp"

// Allocator functions
#include <h2_allocator.h>

class CardTable;
class HeapRegion;

class TeraHeap: public CHeapObj<mtInternal> {
private:
  static char *_start_addr;       // TeraHeap start address of mmap region
  static char *_stop_addr;        // TeraHeap ends address of mmap region
  static size_t _size;            // _stop_addr - _start_addr (0 before H2 exists)
  ObjectStartArray _start_array;  // Keeps track of where objects
                                  // start in a 2^CARD_SEGMENT_SIZE block
  size_t _h2_stripe_cards;        // H2 card-table stripe: the cards of one
                                  // region (H2RegionSize / card size)
  CardTable* _h2_card_table;      // The H2 card table (set_h2_card_table())

  /*-----------------------------------------------
   * Stacks
   *---------------------------------------------*/
  // Full GC stacks of one GC thread. A thread pushes only to its own
  // stacks, so pushes take no lock. Each stack is filled by one Full GC
  // task and drained by a later one, by all threads in parallel: thread
  // w drains stacks w, w + n, ... (n = active threads).
  struct H2FullGCStacks {
    // H2 fields with backward references (H2 to H1) found by the H2 card
    // scan: the H1 objects are marked live in phase 1 (marking) ...
    Stack<oop *, mtGC> mark;
    // ... and the fields are adjusted in phase 3 (adjust pointers)
    Stack<oop *, mtGC> adjust;
    // First regions of humongous objects to move to H2: pushed in phase 2
    // (prepare compaction), moved in phase 4 (compaction)
    Stack<HeapRegion *, mtGC> humongous;
#ifdef ASSERT
    // H1 regions freed after moving their humongous object to H2 in
    // phase 4, checked to be on the free list at the end of Full GC
    Stack<HeapRegion *, mtGC> humongous_to_verify;
#endif // ASSERT
  };
  PaddedEnd<H2FullGCStacks>* _fgc_stacks;   // one per GC thread

  /*---------------------------------------------*/

  TeraStatistics *tera_stats;

  // Full GC adjust: the object a GC thread is moving to H2, at its H1
  // address and its new H2 address (NULL when it moves no object to H2).
  // Each thread writes its own entry for every object it moves, so the
  // entries are padded to a cache line each to avoid false sharing.
  struct H2GroupState {
    HeapWord* h1_addr;
    HeapWord* h2_addr;
  };
  PaddedEnd<H2GroupState>* _group_state;   // one per GC thread

  // The H2 card table
  inline CardTable* th_card_table() const;

  // h2_free_regions() callback (ctx: the TeraHeap): clears the start array
  // and the cards of a freed region
  static void h2_region_freed(char *region_start, char *last_alloc_start,
                              char *last_alloc_end, void *ctx);

  // How a Full GC writes objects to H2 (TeraH2FullGCWriteMode)
  enum H2FullGCWriteMode {
    H2_WRITE_BUFFER_ASYNC,  // promotion buffers, written with async I/O
    H2_WRITE_ASYNC,         // async I/O per object
    H2_WRITE_SYNC,          // pwrite per object
    H2_WRITE_MEMCPY         // memcpy into the H2 mapping
  };
  H2FullGCWriteMode _fgc_write_mode;

  // Parse TeraH2FullGCWriteMode; exits on an unknown value
  static H2FullGCWriteMode parse_fgc_write_mode(const char* mode);

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

  // Created once at startup (Universe). Like the G1 heap, it is never
  // destroyed: it lives until the process ends
  TeraHeap();

  // Set the H2 card table: once, when G1 creates it (after this object)
  void set_h2_card_table(CardTable* ct) {
    assert(_h2_card_table == NULL, "H2 card table already set");
    _h2_card_table = ct;
  }

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

  // Check if a pointer belongs to the TeraHeap. One compare: a pointer
  // below H2 wraps around to a large offset. False before H2 exists.
  static bool is_in_h2(const void* p) {
    return (uintptr_t) p - (uintptr_t) _start_addr < _size;
  }

  // Number of Full GC stacks of each kind (one per GC thread)
  uint h2_num_fgc_stacks() const { return ParallelGCThreads; }

  // Empty the Full GC stacks and free their memory: at the start of a
  // Full GC, and at its end after checking that they were drained
  void h2_clear_fgc_stacks();

  // True if all Full GC stacks are empty
  bool h2_fgc_stacks_empty();

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
  inline oop* h2_pop_back_ref_to_mark(uint stack_id);

  // Push a backward reference (H2 field `p` to H1 object `o`) found by
  // thread `worker_id` to its mark and adjust stacks
  inline void h2_push_backward_reference(uint worker_id, void *p, oop o, bool to_young);

  // Push the first region of a humongous object that is marked to move
  // to H2 to the humongous stack of thread `worker_id`, to move it during
  // the compaction phase
  inline void h2_push_humongous_start(uint worker_id, HeapRegion *hr);

#ifdef ASSERT
  // Push a freed humongous region to the verify stack of thread
  // `worker_id`, to check that it is on the free list after Full GC
  void push_humongous_to_verify(uint worker_id, HeapRegion *hr);

  // True if all humongous verify stacks are empty
  bool humongous_to_verify_empty();
#endif // ASSERT

  // Pop a backward reference from adjust stack `stack_id`, or NULL if it
  // is empty
  inline oop* h2_pop_back_ref_to_adjust(uint stack_id);

  // Pop the first region of a humongous object to move to H2 from
  // humongous stack `stack_id`, or NULL if it is empty
  inline HeapRegion *h2_pop_humongous_start(uint stack_id);

#ifdef ASSERT
  // Pop the next humongous region to verify it is on the free list from
  // any verify stack, or NULL if all are empty (only for debugging)
  HeapRegion *verify_next_humongous();
#endif // ASSERT

  // Resets the used field of all regions
  void h2_reset_used_field(void);

  // Marks the region containing obj as used
  inline void mark_used_region(HeapWord *obj);

  // If the current thread is relocating an object to H2, record cross-heap / cross-H2-region
  // reference metadata (region dependency or H2 card marking) for the reference slot `obj_field`.
  // No-op if relocation context is not active.
  inline void check_for_cross_heap_cross_h2_region_ref(uint thread_id, HeapWord *obj, void *obj_field);

  // Frees all unused regions
  void free_unused_regions(void);

  // TeraH2AllocatorStats: at the end of a young or mixed pause, print the
  // allocator statistics to the TeraHeap log; `tag` is YOUNG or MIXED
  void print_h2_alloc_stats(const char *tag);

  // Enables groupping with region of obj (multi-threaded)
  void thread_enable_groups(uint thread_id, HeapWord *old_addr, HeapWord *new_addr) {
    if (!EnableTeraHeap)
      return;
    H2GroupState& g = _group_state[thread_id];
    assert(g.h1_addr == NULL && g.h2_addr == NULL,
           "Thread %u corrupted group state.", thread_id);
    g.h1_addr = old_addr;
    g.h2_addr = new_addr;
  }

  // Disables region groupping (multi-threaded)
  void thread_disable_groups(uint thread_id) {
    if (!EnableTeraHeap)
      return;
    _group_state[thread_id].h1_addr = NULL;
    _group_state[thread_id].h2_addr = NULL;
  }

  // Add a new entry to `obj1` region dependency list that reference
  // `obj2` region
  // Lock-free: the allocator's dependency sets take parallel inserts
  // (lookups and inserts use CAS, see add_reference())
  void group_regions(HeapWord *obj1, HeapWord *obj2) {
    if (::h2_is_in_the_same_group((char *) obj1, (char *) obj2))
      return;
    ::h2_references((char *) obj1, (char *) obj2);
  }

  // Label of the H2 region that contains address p: the label of the
  // objects allocated in it
  uint64_t h2_get_region_label(void *p) {
    assert(p != NULL, "Sanity check");
    return ::h2_get_obj_label((char *) p);
  }

  // Sublabel of the H2 region that contains address p
  uint64_t h2_get_region_sublabel(void *p) {
    assert(p != NULL, "Sanity check");
    return ::h2_get_obj_sublabel((char *) p);
  }

  // Check if the object `obj` is an instance of the following
  // metadata class:
  // - Instance Mirror Klass
  // - Instance Reference Klass
  // - Instance Class Loader Klass
  // If yes return true, otherwise false
  inline bool is_metadata(oop obj);

  // Check if the object with `addr` span multiple regions
  int h2_continuous_regions(HeapWord *addr);

  // Cards per stripe when GC workers scan the H2 card table in parallel:
  // one stripe per region, so that a stripe times h2_continuous_regions()
  // covers whole regions
  size_t h2_stripe_cards() const { return _h2_stripe_cards; }

  // Move object with size 'size' from source address 'src' to the h2
  // destination address 'dst'. A Full GC (is_fgc) writes it as
  // TeraH2FullGCWriteMode says; young and mixed GCs use memcpy
  void h2_move_obj(HeapWord *src, HeapWord *dst, size_t size, bool is_fgc = false);

  // At the end of a Full GC: complete the writes of the objects moved to H2
  void h2_complete_transfers();

  // Tera statistics for objects that we move to H2, forward references,
  // and backward references.
  TeraStatistics* get_tera_stats() {
    assert(tera_stats != NULL, "TeraHeapStatistics and TeraH2CostPercentile not enabled!");
    return tera_stats;
  }

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

