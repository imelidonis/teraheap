#ifndef SHARE_GC_TERAHEAP_TERASTATISTICS_HPP
#define SHARE_GC_TERAHEAP_TERASTATISTICS_HPP

#include "gc/shared/gc_globals.hpp"
#include "memory/allocation.hpp"
#include "memory/padded.hpp"
#include "oops/oop.hpp"

#ifdef RUSAGE_MUTATOR
  #include <sys/resource.h>
#endif // RUSAGE_MUTATOR

// Statistics of TeraHeap (TeraHeapStatistics), printed to the TeraHeap log
// at the end of each young, mixed and Full GC. The per-thread H2 times and
// bytes are also used by the G1 cost model (TeraH2CostPercentile).
//
// GC threads update only their own counters (TeraWorkerStats, one cache
// line apart), so the counters need no lock or atomic; they are summed when
// printed. Times are kept in seconds, as G1 keeps them, and printed in ms.
class TeraStatistics: public CHeapObj<mtInternal> {
public:

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  // initial_evac_phase stands for "evacuation of initial collection set is now taking place"
  // Correspondingly, for optional evacuation phase.
  // dummy is default.
  enum evac_phase {
    inital_evac_phase = 0,
    optional_evac_phase,
    full_gc,
    dummy
  };
#endif

  // Where an object moved to H2 by a young/mixed GC came from. Survivor
  // regions are relabeled as eden at the start of the pause, so the
  // source is taken from the region's survival rate group.
  enum h2_source {
    h2_src_eden = 0,
    h2_src_survivor,
    h2_src_old,
    h2_src_count
  };

  // TeraH2Shadow: where the bytes a young/mixed GC copied to H2 landed
  enum h2_dest {
    h2_dest_file = 0,     // the H2 file mapping
    h2_dest_chunk,        // a pool chunk (DRAM)
    h2_dest_count
  };

private:
  // Counters of one GC thread for the current GC; reset after each GC
  struct TeraWorkerStats {
    // Backward references (H2 to H1) by the region of the H1 object
    size_t back_refs_young;
    size_t back_refs_old;

    // Objects moved to H2 and their bytes
    size_t objects_moved;
    size_t objects_bytes;

    // Words of H2 space lost when another thread moved the object first
    size_t h2_waste_words;

    // Bytes copied to H2: in total, per source region type and (TeraH2Shadow)
    // per destination
    size_t bytes_copy_h2;
    size_t bytes_copy_h2_src[h2_src_count];
    size_t bytes_h2_dest[h2_dest_count];

    // Full GC: bytes compaction moved inside H1, regions scanned and skipped
    size_t fgc_h1_bytes;
    int fgc_regions_scanned;
    int fgc_regions_skipped;

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
    // Time (s) to allocate H2 space and to copy objects to H2, per
    // evac_phase (initial, optional, Full GC). These do not include the time
    // during remembered set scanning, which G1 does not count as copy time.
    double time_alloc_h2[3];
    double time_copy_h2[3];

    // Allocation + copy time (s) per destination in the initial and the
    // optional evacuation
    double time_h2_dest[2][h2_dest_count];

    // Per evac_phase:
    //  0 stands for "do not count time for H2"
    //  1 stands for "count time for H2"
    //  2 stands for "in any case do not set the flag 1"
    uint during_h1_time_flag[3];
#endif
  };

  PaddedEnd<TeraWorkerStats>* _workers;

  // Since the start of the JVM
  size_t total_objects_moved;
  size_t total_objects_size;

  // Total humongous transferred (Full GC, worker 0 only)
  size_t total_h2_humongous;

  // Full GC: backward references pushed to the marking stacks; counted
  // under tera_heap_lock, which protects those stacks
  size_t fgc_back_refs_young;
  size_t fgc_back_refs_old;

  // time to scan h2 card table
  double h2_card_table_scan_time_ms;

  // number of cocnurrent marking cycle
  size_t conc_cycle_id;

  // total bytes copied to H1 + H2
  size_t h1_copied_bytes;

  // total bytes copied to H2
  size_t h2_copied_bytes;

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  // The highest allocation and copy time (ms) of a thread in this GC
  double h2_allocate_ms;
  double h2_copy_ms;

  // In which phase g1 currnetly is
  enum evac_phase which_phase;
#endif

  bool _is_mixed_gc;
  bool _is_full_gc;

#ifdef RUSAGE_MUTATOR
  time_t last_mutator_system_time_s;
  long last_mutator_major_page_faults;
  long last_mutator_minor_page_faults;

  time_t mutator_system_time_s;
  long mutator_major_page_faults;
  long mutator_minor_page_faults;
#endif // RUSAGE_MUTATOR

  uint reclaimed_regions_count;

  TeraWorkerStats& worker(uint worker_id) {
    assert(worker_id < ParallelGCThreads, "worker %u out of range", worker_id);
    return _workers[worker_id];
  }

public:

  TeraStatistics();

  // Initialize necessary counters back to their default values
  void reset_counters();

  // Count an object of 'bytes' that thread 'worker_id' moves to H2
  void add_object(uint worker_id, size_t bytes) {
    TeraWorkerStats& w = worker(worker_id);
    w.objects_moved++;
    w.objects_bytes += bytes;
  }

  // Count a backward reference (H2 to H1) found by a young/mixed GC thread;
  // 'to_young' if the H1 object is in a young region
  void add_back_ref(uint worker_id, bool to_young) {
    TeraWorkerStats& w = worker(worker_id);
    if (to_young) w.back_refs_young++; else w.back_refs_old++;
  }

  // Count a backward reference found by a Full GC; the caller holds
  // tera_heap_lock
  void add_back_ref_locked(bool to_young) {
    if (to_young) fgc_back_refs_young++; else fgc_back_refs_old++;
  }

  // Increase the number of humongous objects transferred to H2
  void add_h2_humongous() {
    total_h2_humongous++;
  }

  // Get the number of humongous objects transferred to H2
  size_t get_h2_humongous() {
    return total_h2_humongous;
  }

  // Print the statistics of TeraHeap at the end of each GC and reset the
  // counters
  void print_gc_stats();

  void record_h2_scan_time(double time){
    h2_card_table_scan_time_ms = time;
  }

  // Add the size of object in bytes that a thread copied to H2
  void thr_add_bytes_copy_h2(uint thread_id, size_t bytes) {
    worker(thread_id).bytes_copy_h2 += bytes;
  }

  // Get the total bytes that were copied TO h2
  size_t get_sum_thr_bytes_copy_h2();

  // Add the bytes of an object that a thread copied to H2 from a region
  // of type `src`
  void thr_add_bytes_copy_h2_src(uint thread_id, enum h2_source src, size_t bytes) {
    worker(thread_id).bytes_copy_h2_src[src] += bytes;
  }

  // Get the total bytes that were copied to H2 from regions of type `src`
  size_t get_sum_thr_bytes_copy_h2_src(enum h2_source src);

  // Add the bytes of an object that a thread copied to H2 at `dest`
  void thr_add_bytes_h2_dest(uint thread_id, enum h2_dest dest, size_t bytes) {
    worker(thread_id).bytes_h2_dest[dest] += bytes;
  }

  // Get the total bytes that were copied to H2 at `dest`
  size_t get_sum_thr_bytes_h2_dest(enum h2_dest dest);

  // Full GC: add the bytes of an object that compaction moved inside H1
  void thr_add_fgc_h1_bytes(uint thread_id, size_t bytes) {
    worker(thread_id).fgc_h1_bytes += bytes;
  }

  // Which number of concurrent marking cycle
  void record_cycle_no(size_t cocn_cycle_no) {
    conc_cycle_id = cocn_cycle_no;
  }

  // The amount of bytes g1 tracked as copied bytes during the last gc
  void record_h1_copied_bytes(size_t h1_bytes) {
    h1_copied_bytes = h1_bytes;
  }

  // The amount of bytes copied to H2
  void record_h2_copied_bytes(size_t h2_bytes) {
    h2_copied_bytes = h2_bytes;
  }

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  // Get the time (s) thread worker_id spent for copying bytes to H2
  double get_time_copy_h2(uint worker_id) {
    assert(which_phase != dummy, "Should not access in dummy phase\n");
    return worker(worker_id).time_copy_h2[which_phase];
  }

  // Get the time (s) thread worker_id spent for allocating space for H2
  double get_time_alloc_h2(uint worker_id) {
    assert(which_phase != dummy, "Should not access in dummy phase\n");
    return worker(worker_id).time_alloc_h2[which_phase];
  }

  // The average time (ms) per thread spent during allocating space and
  // copying bytes to H2
  double get_average_time_ms_h2(enum evac_phase phase);

  // Add the time (s) it took for a thread to make an allocation in H2
  void thr_add_time_alloc_h2(uint thread_id, double time) {
    assert(which_phase != dummy, "Should not access in dummy phase\n");
    TeraWorkerStats& w = worker(thread_id);
    if (w.during_h1_time_flag[which_phase] == 1) {
      w.time_alloc_h2[which_phase] += time;
    }
  }

  // Add the time (s) it took for a thread to make a copy to H2
  void thr_add_time_copy_h2(uint thread_id, double time) {
    assert(which_phase != dummy, "Should not access in dummy phase\n");
    TeraWorkerStats& w = worker(thread_id);
    if (w.during_h1_time_flag[which_phase] == 1) {
      w.time_copy_h2[which_phase] += time;
    }
  }

  // Add the allocation + copy time (s) of an object copied to H2 at `dest`
  // (counted like thr_add_time_copy_h2())
  void thr_add_time_h2_dest(uint thread_id, enum h2_dest dest, double time);

  // The average time (ms) per contributing thread for the bytes copied to
  // H2 at `dest` in evacuation phase `phase`
  double get_average_time_ms_h2_dest(enum evac_phase phase, enum h2_dest dest);

  // Set the flag that indicates whether g1 is tracking time for obj copy phase in evacuation or not
  void set_during_h1_time_flag(uint worker_id, uint flag) {
    assert(which_phase == inital_evac_phase || which_phase == optional_evac_phase || which_phase == full_gc, "wrong value for enum\n");
    worker(worker_id).during_h1_time_flag[which_phase] = flag;
  }

  // Get the value of the flag that indicates whether g1 is tracking time for obj copy phase in evacuation or not
  uint get_during_h1_time_flag(uint worker_id) {
    assert(which_phase == inital_evac_phase || which_phase == optional_evac_phase || which_phase == full_gc, "wrong value for enum\n");
    return worker(worker_id).during_h1_time_flag[which_phase];
  }

  // Set the phase in which g1 currently is
  void set_which_phase(enum evac_phase phase) {
    which_phase = phase;
  }

  void record_h2_max_allocate_time() {
    h2_allocate_ms = get_max_thr_time_alloc_h2() * 1000.0;
  }

  void record_h2_max_copy_time() {
    h2_copy_ms = get_max_thr_time_copy_h2() * 1000.0;
  }
#endif

  void set_is_in_mix(bool is_mixed_gc) {
    _is_mixed_gc = is_mixed_gc;
  }

  void set_is_in_full_gc(bool is_full_gc) {
    _is_full_gc = is_full_gc;
  }

  bool is_in_full_gc() const {
    return _is_full_gc;
  }

  // Add the words of H2 space lost when another thread moved the object
  void add_h2_waste(uint worker_id, size_t words) {
    worker(worker_id).h2_waste_words += words;
  }

#ifdef RUSAGE_MUTATOR
  time_t get_last_mutator_system_time() {
    return last_mutator_system_time_s;
  }

  long get_last_mutator_major_page_faults() {
    return last_mutator_major_page_faults;
  }

  long get_last_mutator_minor_page_faults() {
    return last_mutator_minor_page_faults;
  }

  void set_last_mutator_system_time(time_t sys_time_s) {
    last_mutator_system_time_s = sys_time_s;
  }

  void set_last_mutator_major_page_faults(long major_page_faults) {
    last_mutator_major_page_faults = major_page_faults;
  }

  void set_last_mutator_minor_page_faults(long minor_page_faults) {
    last_mutator_minor_page_faults = minor_page_faults;
  }

  time_t get_mutator_system_time() {
    return mutator_system_time_s;
  }

  long get_mutator_major_page_faults() {
    return mutator_major_page_faults;
  }

  long get_mutator_minor_page_faults() {
    return mutator_minor_page_faults;
  }

  void add_mutator_system_time(time_t sys_time_s) {
    mutator_system_time_s += sys_time_s;
  }

  void add_mutator_major_page_faults(long major_page_faults) {
    mutator_major_page_faults += major_page_faults;
  }

  void add_mutator_minor_page_faults(long minor_page_faults) {
    mutator_minor_page_faults += minor_page_faults;
  }

  void report_rusage();

#endif // RUSAGE_MUTATOR

  void thr_add_regions_scanned(uint thread_id, int num_regions) {
    worker(thread_id).fgc_regions_scanned += num_regions;
  }

  void thr_add_regions_skipped(uint thread_id, int num_regions) {
    worker(thread_id).fgc_regions_skipped += num_regions;
  }

  void set_reclaimed_region_count(uint num_reclaimed_regions) {
    reclaimed_regions_count = num_reclaimed_regions;
  }

private:

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  double get_max_thr_time_alloc_h2();

  double get_max_thr_time_copy_h2();
#endif
};

#endif // SHARE_GC_TERAHEAP_TERASTATISTICS_HPP
