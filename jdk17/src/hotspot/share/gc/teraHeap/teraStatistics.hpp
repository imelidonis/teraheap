#ifndef SHARE_GC_TERAHEAP_TERASTATISTICS_HPP
#define SHARE_GC_TERAHEAP_TERASTATISTICS_HPP

#include "memory/allocation.hpp"
#include "oops/oop.hpp"

#ifdef RUSAGE_MUTATOR
  #include <sys/resource.h>
#endif // RUSAGE_MUTATOR

#ifdef BACK_REF_STAT
#include <map>
#include <tr1/tuple>
#endif

// A class with all the statistics for TeraHeap
// NOTE: many counters and metrics are not used, however
//    they may be required for debugging later therefore,
//    we leave them here for later usage.
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

private:
  long  total_objects_moved;               
  long  total_objects_size; 
  long  forward_ref;
  long  backward_ref;

  // Total humongous transferred
  size_t total_h2_humongous;

  // time to scan h2 card table
  double h2_card_table_scan_time_ms;
  // time for evacuation to be completed
  double evac_time_ms;

  // bytes copied to H2 from each thread
  size_t* thr_bytes_copy_h2;

  // number of cocnurrent marking cycle
  size_t conc_cycle_id;

  // total bytes copied to H1 + H2
  size_t h1_copied_bytes;

  // total bytes copied to H2
  size_t h2_copied_bytes;

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  // note: these counters do not include root scanning time during evacuation phases for compatibility with g1
  // total time of allocation to H2 
  double h2_allocate_ms;

  // total time of copying to H2
  double h2_copy_ms;

  // Time spent from thread i in evac_phase p for allocation
  double** thr_time_alloc_h2;

  // Time spent from thread i in evac_phase p for copy
  double** thr_time_copy_h2;

  // Flag for thread i in evac_phase p:
  //  0 stands for "do not count time for H2"
  //  1 stands for "count time for H2"
  //  2 stands for "in any case do not set the flag 1"
  uint**   during_h1_time_flag;

  // In which phase g1 currnetly is 
  enum evac_phase which_phase;
#endif

  bool _is_mixed_gc;
  bool _is_full_gc;

  // In words
  uint h2_waste_space;

  // Object size distribution between B, KB, MB
  uint64_t obj_distr_size[3];

#ifdef RUSAGE_MUTATOR
  time_t last_mutator_system_time_s;
  long last_mutator_major_page_faults;
  long last_mutator_minor_page_faults;

  time_t mutator_system_time_s;
  long mutator_major_page_faults;
  long mutator_minor_page_faults;
#endif // RUSAGE_MUTATOR

#ifdef BACK_REF_STAT
  // This histogram keeps internally statistics for the backward
  // references (H2 to H1)
  std::map<oop *, std::tr1::tuple<int, int, int> > histogram;
  oop *back_ref_obj;
#endif

#ifdef FWD_REF_STAT
  // This histogram keeps internally statistics for the forward references
  // (H1 to H2) per object
  std::map<oop, int> fwd_ref_histo;
#endif

  int *thr_fgc_regions_scanned;
  int *thr_fgc_regions_skipped;
  uint reclaimed_regions_count;

public:

  TeraStatistics();

  // Initialize necessary counters back to their default values
  void reset_counters();

  // Increase by one the counter that shows the total number of
  // objects that are moved to H2. Increase by 'size' the counter that shows
  // the total size of the objects that are moved to H2. Increase by one
  // the number of objects that are moved in the current gc cycle to H2.
  void add_object(long size);


  // Increase by one the number of forward references per GC;
  void add_fwd_ref();

  // Increase by one the number of backward references per GC;
  void add_back_ref();

  // Increase the number of humongous objects transferred to H2
  void add_h2_humongous();

  // Get the number of humongous objects transferred to H2
  size_t get_h2_humongous();

  
  // Increase the appropriate counter for the distribution
  // NOTE: call when adding objects to H2
  void add_obj_size_distribution(size_t size);

  // Print the statistics of TeraHeap at the end of each FGC
  // Will print:
  //	- the total forward references from the H1 to the H2
  //	- the total backward references from H2 to the H1
  //	- the total objects that has been moved to H2
  //	- the current total size of objects in H2
  //	- the current total objects that are moved in H2
  void print_gc_stats();

  void record_h2_scan_time(double time){
    h2_card_table_scan_time_ms = time;
  }

  void record_evacuation_time(double time ){
    evac_time_ms = time;
  }

  // Add the size of object in bytes that a thread copied to H2
  void thr_add_bytes_copy_h2(uint thread_id, size_t bytes);

  // Get the total bytes that were copied TO h2
  size_t get_sum_thr_bytes_copy_h2();

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
  // Get the time thread worker_id spent for copying bytes to H2
  double get_time_copy_h2(uint worker_id);

  // Get the time thread worker_id spent for allocating space for H2
  double get_time_alloc_h2(uint worker_id);

  // The average time per thread spent during copying bytes to H2
  double get_average_time_ms_h2(enum evac_phase phase);

  // The total time that all threads spent during allocating space for H2
  double get_sum_thr_time_alloc_h2();

  // The total time that all threads spent during copying bytes to H2
  double get_sum_thr_time_copy_h2();

  // Add the time it took for a thread to make an allocation in H2
  void thr_add_time_alloc_h2(uint thread_id, double time);

  // Add the time it took for a thread to make a copy to H2
  void thr_add_time_copy_h2(uint thread_id, double time);

  // Set the flag that indicates whether g1 is tracking time for obj copy phase in evacuation or not
  void set_during_h1_time_flag(uint worker_id, uint flag) {
    assert(which_phase == inital_evac_phase || which_phase == optional_evac_phase, "wrong value for enum\n"); 
    during_h1_time_flag[worker_id][which_phase] = flag;
  }

  // Get the value of the flag that indicates whether g1 is tracking time for obj copy phase in evacuation or not
  uint get_during_h1_time_flag(uint worker_id) {
    assert(which_phase == inital_evac_phase || which_phase == optional_evac_phase, "wrong value for enum\n"); 
    return during_h1_time_flag[worker_id][which_phase];
  }

  // Set the phase in which g1 currently is 
  void set_which_phase(enum evac_phase phase) {
    which_phase = phase;
  }

  void record_h2_max_allocate_time() {
    h2_allocate_ms = get_max_thr_time_alloc_h2();
  }

  void record_h2_max_copy_time() {
    h2_copy_ms = get_max_thr_time_copy_h2();
  }
#endif

  void set_is_in_mix(bool is_mixed_gc) {
    _is_mixed_gc = is_mixed_gc;
  }

  void set_is_in_full_gc(bool is_full_gc) {
    _is_full_gc = is_full_gc;
  }

  void add_h2_waste(uint waste) {
    h2_waste_space += waste;
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

#ifdef BACK_REF_STAT
  // Add a new entry to the histogram for back reference that start from
  // 'obj' and results in H1 (new or old generation).
  // Use this function with a single GC thread
  void h2_update_back_ref_stats(bool is_old, bool is_tera_cache);

  void h2_enable_back_ref_traversal(oop *obj);

  // Print the histogram
  void h2_print_back_ref_stats();
#endif

#ifdef FWD_REF_STAT
  // Add a new entry to the histogram for forward reference that start from
  // H1 and results in 'obj' in H2
  void h2_add_fwd_ref_stat(oop obj);
  
  // Print the histogram
  void h2_print_fwd_ref_stat();
#endif

  void thr_add_regions_scanned(uint thread_id, int num_regions);
  void thr_add_regions_skipped(uint thread_id, int num_regions);
  void set_reclaimed_region_count(uint num_reclaimed_regions) {
    reclaimed_regions_count = num_reclaimed_regions;
  }

private:

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  double get_max_thr_time_alloc_h2();

  double get_max_thr_time_copy_h2();
#endif

  int get_total_regions_scanned();

  int get_total_regions_skipped();
};

#endif // SHARE_GC_TERAHEAP_TERASTATISTICS_HPP
