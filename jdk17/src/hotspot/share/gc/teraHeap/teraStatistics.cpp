#include "gc/shared/gc_globals.hpp"
#include "gc/teraHeap/teraStatistics.hpp"
#include "memory/padded.inline.hpp"
#include "runtime/arguments.hpp"


#ifdef RUSAGE_MUTATOR
  #include <sys/resource.h>

  void TeraStatistics::report_rusage() {

    struct rusage current_start;
    getrusage(RUSAGE_SELF, &current_start);

    this->add_mutator_system_time(
      current_start.ru_stime.tv_sec - last_mutator_system_time_s
    );
    this->add_mutator_major_page_faults(
      current_start.ru_majflt - last_mutator_major_page_faults
    );

    this->add_mutator_minor_page_faults(
      current_start.ru_minflt - last_mutator_minor_page_faults
    );

    thlog_or_tty->print_cr("--- [rusage][final] MutatorSystemsTimeS %lu", mutator_system_time_s);
    thlog_or_tty->print_cr("--- [rusage][final] TotalSystemsTimeS %lu\n", current_start.ru_stime.tv_sec);

    thlog_or_tty->print_cr("--- [rusage][final] MutatorMinorPageFaults %lu", mutator_minor_page_faults);
    thlog_or_tty->print_cr("--- [rusage][final] TotalMinorPageFaults %lu\n", current_start.ru_minflt);

    thlog_or_tty->print_cr("--- [rusage][final] MutatorMajorPageFaults %lu", mutator_major_page_faults);
    thlog_or_tty->print_cr("--- [rusage][final] TotalMajorPageFaults %lu", current_start.ru_majflt);

    thlog_or_tty->flush();
  }
#endif // RUSAGE_MUTATOR

TeraStatistics::TeraStatistics() {
  // NOTE: not freed as the destructor is never called
  _workers = PaddedArray<TeraWorkerStats, mtGC>::create_unfreeable(ParallelGCThreads);

  total_objects_moved = 0;
  total_objects_size = 0;
  total_h2_humongous = 0;

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  which_phase = dummy;
  h2_allocate_ms = 0;
  h2_copy_ms = 0;
#endif

  h1_copied_bytes = 0;
  h2_copied_bytes = 0;
  conc_cycle_id = 0;

  _is_mixed_gc = false;
  _is_full_gc = false;

#ifdef RUSAGE_MUTATOR
  struct rusage mutator_usage;
  getrusage(RUSAGE_SELF, &mutator_usage);

  mutator_system_time_s = mutator_usage.ru_stime.tv_sec;
  mutator_major_page_faults = mutator_usage.ru_majflt;
  mutator_minor_page_faults = mutator_usage.ru_minflt;

  last_mutator_system_time_s = mutator_system_time_s;
  last_mutator_major_page_faults = mutator_major_page_faults;
  last_mutator_minor_page_faults = mutator_minor_page_faults;

  thlog_or_tty->print_cr("--- [rusage][init] MutatorSystemsTimeS %lu\n", mutator_system_time_s);
  thlog_or_tty->print_cr("--- [rusage][init] MutatorMajorPageFaults %lu\n", mutator_major_page_faults);
  thlog_or_tty->print_cr("--- [rusage][init] MutatorMinorPageFaults %lu\n", mutator_minor_page_faults);

  thlog_or_tty->flush();
#endif // RUSAGE_MUTATOR

  reset_counters();
}

void TeraStatistics::reset_counters(void) {
  for (uint i = 0; i < ParallelGCThreads; i++) {
    memset((TeraWorkerStats*) &_workers[i], 0, sizeof(TeraWorkerStats));
  }

  reclaimed_regions_count = 0;
  h2_card_table_scan_time_ms = 0;

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET
  which_phase = dummy;
  h2_allocate_ms = 0;
  h2_copy_ms = 0;
#endif
}

// Print the statistics of TeraHeap at the end of each GC:
//	- the backward references (H2 to H1) found in this GC, in total and by
//	  the region (young, old) of the H1 object
//	- the objects moved to H2 since the start of the JVM
//	- the times (ms) of this GC to scan the H2 card table and to allocate
//	  and copy objects to H2 (highest of the threads)
void TeraStatistics::print_gc_stats() {
  size_t back_young = 0, back_old = 0, objects = 0, objects_bytes = 0;
  size_t waste_words = 0, h2_bytes = 0, fgc_h1_bytes = 0;
  int regions_scanned = 0, regions_skipped = 0;

  for (uint i = 0; i < ParallelGCThreads; i++) {
    const TeraWorkerStats& w = _workers[i];
    back_young += w.back_refs_young;
    back_old += w.back_refs_old;
    objects += w.objects_moved;
    objects_bytes += w.objects_bytes;
    waste_words += w.h2_waste_words;
    h2_bytes += w.bytes_copy_h2;
    fgc_h1_bytes += w.fgc_h1_bytes;
    regions_scanned += w.fgc_regions_scanned;
    regions_skipped += w.fgc_regions_skipped;
  }
  total_objects_moved += objects;
  total_objects_size += objects_bytes;

  const char* tag = _is_full_gc ? "[FULL]" : (_is_mixed_gc ? "[MIXED]" : "[YOUNG]");

  thlog_or_tty->print_cr("%s | BACK_PTRS = " SIZE_FORMAT, tag, back_young + back_old);
  thlog_or_tty->print_cr("%s | BACK_PTRS_YOUNG = " SIZE_FORMAT, tag, back_young);
  thlog_or_tty->print_cr("%s | BACK_PTRS_OLD = " SIZE_FORMAT, tag, back_old);

  if (_is_full_gc) {
    thlog_or_tty->print_cr("[FULL] | TOTAL_OBJECTS  = " SIZE_FORMAT, total_objects_moved);
    thlog_or_tty->print_cr("[FULL] | TOTAL_OBJECTS_SIZE = " SIZE_FORMAT, total_objects_size);
    thlog_or_tty->print_cr("[FULL] | RECLAIMED_REGIONS = %u", reclaimed_regions_count);
    thlog_or_tty->print_cr("[FULL] | TIME_SCAN_H2_CT %.3lf ms", h2_card_table_scan_time_ms);
  #ifdef TWO_FACTOR_COST_MODEL_IN_CSET
    thlog_or_tty->print_cr("[FULL] | TIME_TO_ALLOC_H2 %.3lf ms", h2_allocate_ms);
    thlog_or_tty->print_cr("[FULL] | TIME_TO_COPY_H2 %.3lf ms", h2_copy_ms);
  #endif
    // H2: objects compaction moved to H2 (with humongous objects); H1:
    // objects compaction moved inside H1
    thlog_or_tty->print_cr("[FULL] | BYTES_COPIED_TO_H2 " SIZE_FORMAT " and BYTES_COPIED_TO_H1 " SIZE_FORMAT
                           " in CONC_MARK_CYCLE_NO " SIZE_FORMAT, h2_bytes, fgc_h1_bytes, conc_cycle_id);
    thlog_or_tty->print_cr("[FULL] | Regions Scanned = %d", regions_scanned);
    thlog_or_tty->print_cr("[FULL] | Regions Skipped = %d", regions_skipped);
  } else if (_is_mixed_gc) {
    thlog_or_tty->print_cr("[MIXED] | TOTAL_OBJECTS  = " SIZE_FORMAT, total_objects_moved);
    thlog_or_tty->print_cr("[MIXED] | TOTAL_OBJECTS_SIZE = " SIZE_FORMAT, total_objects_size);
    thlog_or_tty->print_cr("[MIXED] | TIME_SCAN_H2_CT %.3lf ms", h2_card_table_scan_time_ms);
  #ifdef TWO_FACTOR_COST_MODEL_IN_CSET
    thlog_or_tty->print_cr("[MIXED] | TIME_TO_ALLOC_H2 %.3lf ms", h2_allocate_ms);
    thlog_or_tty->print_cr("[MIXED] | TIME_TO_COPY_H2 %.3lf ms", h2_copy_ms);
  #endif
    thlog_or_tty->print_cr("[MIXED] | BYTES_COPIED_TO_H2 " SIZE_FORMAT " and BYTES_COPIED_TO_H1 " SIZE_FORMAT
                           " in CONC_MARK_CYCLE_NO " SIZE_FORMAT, h2_copied_bytes, h1_copied_bytes, conc_cycle_id);
    thlog_or_tty->print_cr("[MIXED] | WASTE_SPACE = " SIZE_FORMAT, waste_words * HeapWordSize);
  } else {
    thlog_or_tty->print_cr("[YOUNG] | TIME_SCAN_H2_CT %.3lf ms", h2_card_table_scan_time_ms);
  }

  thlog_or_tty->flush();

  // Init the statistics counters of TeraHeap to zero for the next GC
  _is_mixed_gc = false;
  _is_full_gc = false;

  reset_counters();
}

size_t TeraStatistics::get_sum_thr_bytes_copy_h2_src(enum h2_source src) {
  size_t sum_bytes = 0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    sum_bytes += _workers[i].bytes_copy_h2_src[src];
  }
  return sum_bytes;
}

size_t TeraStatistics::get_sum_thr_bytes_h2_dest(enum h2_dest dest) {
  size_t sum_bytes = 0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    sum_bytes += _workers[i].bytes_h2_dest[dest];
  }
  return sum_bytes;
}

size_t TeraStatistics::get_sum_thr_bytes_copy_h2() {
  size_t sum_bytes = 0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    sum_bytes += _workers[i].bytes_copy_h2;
  }
  return sum_bytes;
}

#ifdef TWO_FACTOR_COST_MODEL_IN_CSET

double TeraStatistics::get_max_thr_time_alloc_h2() {
  double max_time = 0.0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    const TeraWorkerStats& w = _workers[i];
    double time = w.time_alloc_h2[inital_evac_phase] + w.time_alloc_h2[optional_evac_phase] + w.time_alloc_h2[full_gc];
    if (time > max_time) {
      max_time = time;
    }
  }

  return max_time;
}

double TeraStatistics::get_max_thr_time_copy_h2() {
  double max_time = 0.0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    const TeraWorkerStats& w = _workers[i];
    double time = w.time_copy_h2[inital_evac_phase] + w.time_copy_h2[optional_evac_phase] + w.time_copy_h2[full_gc];
    if (time > max_time) {
      max_time = time;
    }
  }

  return max_time;
}

void TeraStatistics::thr_add_time_h2_dest(uint thread_id, enum h2_dest dest, double time) {
  assert(which_phase == inital_evac_phase || which_phase == optional_evac_phase, "young/mixed evacuation only");
  if (which_phase != inital_evac_phase && which_phase != optional_evac_phase) {
    return;
  }
  TeraWorkerStats& w = worker(thread_id);
  if (w.during_h1_time_flag[which_phase] == 1) {
    w.time_h2_dest[which_phase][dest] += time;
  }
}

double TeraStatistics::get_average_time_ms_h2_dest(enum evac_phase phase, enum h2_dest dest) {
  assert(phase == inital_evac_phase || phase == optional_evac_phase, "wrong value for enum\n");
  double sum_time = 0.0;
  uint contributing_threads = 0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    const double t = _workers[i].time_h2_dest[phase][dest];
    if (t != 0) {
      sum_time += t;
      contributing_threads++;
    }
  }
  return contributing_threads != 0 ? sum_time / contributing_threads * 1000.0 : 0.0;
}

double TeraStatistics::get_average_time_ms_h2(enum evac_phase phase) {
  assert(which_phase == dummy, "Should access in dummy phase\n");
  assert(phase == inital_evac_phase || phase == optional_evac_phase, "wrong value for enum\n");
  assert(_is_full_gc == 0, "we are in full gc here");
  uint contributing_threads_alloc = 0;
  uint contributing_threads_copy = 0;
  double sum_time_alloc = 0.0;
  double sum_time_copy = 0.0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    const TeraWorkerStats& w = _workers[i];
    if (w.time_alloc_h2[phase] != 0)
      contributing_threads_alloc++;
    if (w.time_copy_h2[phase] != 0)
      contributing_threads_copy++;
    sum_time_alloc += w.time_alloc_h2[phase];
    sum_time_copy += w.time_copy_h2[phase];
  }
  double avg_copy = 0.0;
  double avg_alloc = 0.0;

  if (contributing_threads_alloc != 0) {
    avg_alloc = sum_time_alloc / (double) contributing_threads_alloc;
  }

  if (contributing_threads_copy != 0) {
    avg_copy = sum_time_copy / (double) contributing_threads_copy;
  }
  return (avg_copy + avg_alloc) * 1000.0;
}
#endif
