#include "gc/shared/gc_globals.hpp"
#include "gc/teraHeap/teraStatistics.hpp"
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
  total_objects_moved = 0;
  total_objects_size = 0;

  total_h2_humongous = 0;

  // NOTE: these arrays are not freed as the destructor is never called
  thr_time_alloc_h2 = NEW_C_HEAP_ARRAY(double, ParallelGCThreads, mtGC);
  thr_time_copy_h2 = NEW_C_HEAP_ARRAY(double, ParallelGCThreads, mtGC);
  thr_bytes_copy_h2 = NEW_C_HEAP_ARRAY(size_t, ParallelGCThreads, mtGC);

  h2_card_table_scan_time_ms = 0;
  evac_time_ms = 0;
  h2_allocate_ms = 0;
  h2_copy_ms = 0;

  _is_mixed_gc = false;
  _is_full_gc = false;

  h2_waste_space = 0;

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

  thr_fgc_regions_scanned = NEW_C_HEAP_ARRAY(int, ParallelGCThreads, mtGC);
  thr_fgc_regions_skipped = NEW_C_HEAP_ARRAY(int, ParallelGCThreads, mtGC);
  reclaimed_regions_count = 0;

  reset_counters();
}

void TeraStatistics::reset_counters(void) {
  forward_ref  = 0;
  backward_ref = 0;
  reclaimed_regions_count = 0;

  memset(thr_time_alloc_h2, 0, ParallelGCThreads * sizeof(double));
  memset(thr_time_copy_h2, 0, ParallelGCThreads * sizeof(double));
  memset(thr_bytes_copy_h2, 0, ParallelGCThreads * sizeof(size_t));

  memset(thr_fgc_regions_scanned, 0, ParallelGCThreads * sizeof(int));
  memset(thr_fgc_regions_skipped, 0, ParallelGCThreads * sizeof(int));
}

// Increase by one the counter that shows the total number of
// objects that are moved to H2. Increase by 'size' the counter that shows 
// the total size of the objects that are moved to H2. Increase by one
// the number of objects that are moved in the current gc cycle to H2.
void TeraStatistics::add_object(long size) {
  total_objects_moved++;
  total_objects_size += size;
}

// Increase by one the number of forward references per GC;
void TeraStatistics::add_fwd_ref() {
  forward_ref++;
}

// Increase by one the number of backward references per GC;
void TeraStatistics::add_back_ref() {
  backward_ref++;
}

// Increase the number of humongous objects transferred to H2
void TeraStatistics::add_h2_humongous() {
  total_h2_humongous++;
}

// Get the number of humongous objects transferred to H2
size_t TeraStatistics::get_h2_humongous() {
  return total_h2_humongous;
}

void TeraStatistics::thr_add_time_alloc_h2(uint thread_id, double time) {
  thr_time_alloc_h2[thread_id] += time;
}

void TeraStatistics::thr_add_time_copy_h2(uint thread_id, double time) {
  thr_time_copy_h2[thread_id] += time;
}

void TeraStatistics::thr_add_bytes_copy_h2(uint thread_id, size_t bytes) {
  thr_bytes_copy_h2[thread_id] += bytes;
}

void TeraStatistics::add_obj_size_distribution(size_t size) {
		size_t obj_size = (size * HeapWordSize) / 1024UL;
		int count = 0;

		while (obj_size > 0) {
			count++;
			obj_size/=1024UL;
		}

		assert(count <=2, "Array out of range");

		++obj_distr_size[count];
}

// Print the statistics of TeraHeap at the end of each FGC
// Will print:
//	- the curr backward references from H2 to the H1
//	- the total objects that has been moved to H2
void TeraStatistics::print_gc_stats() {

  if (_is_mixed_gc) {
    thlog_or_tty->print_cr("[MIXED] | BACK_PTRS = %lu", backward_ref);
    thlog_or_tty->print_cr("[MIXED] | TOTAL_OBJECTS  = %lu", total_objects_moved);
    thlog_or_tty->print_cr("[MIXED] | TOTAL_OBJECTS_SIZE = %lu", total_objects_size);
    thlog_or_tty->print_cr("[MIXED] | TIME_SCAN_H2_CT %.3lf ms", h2_card_table_scan_time_ms);
    thlog_or_tty->print_cr("[MIXED] | TIME_TO_ALLOC_H2 %.3lf ms", h2_allocate_ms);
    thlog_or_tty->print_cr("[MIXED] | TIME_TO_COPY_H2 %.3lf ms", h2_copy_ms);
    thlog_or_tty->print_cr("[MIXED] | WASTE_SPACE = %u", h2_waste_space * HeapWordSize);
  } else if (_is_full_gc) {
    // FGC phases breakdown
    thlog_or_tty->print_cr("[FULL] | BACK_PTRS = %lu", backward_ref);
    thlog_or_tty->print_cr("[FULL] | TOTAL_OBJECTS  = %lu", total_objects_moved);
    thlog_or_tty->print_cr("[FULL] | TOTAL_OBJECTS_SIZE = %lu", total_objects_size);
    thlog_or_tty->print_cr("[FULL] | RECLAIMED_REGIONS = %u", reclaimed_regions_count);
    thlog_or_tty->print_cr("[FULL] | TIME_SCAN_H2_CT %.3lf ms", h2_card_table_scan_time_ms);
    thlog_or_tty->print_cr("[FULL] | TIME_TO_ALLOC_H2 %.3lf ms", h2_allocate_ms);
    thlog_or_tty->print_cr("[FULL] | TIME_TO_COPY_H2 %.3lf ms (accurate for single threaded)", h2_copy_ms);
    
    thlog_or_tty->print_cr("[FULL] | Regions Scanned = %d", get_total_regions_scanned());
    thlog_or_tty->print_cr("[FULL] | Regions Skipped = %d", get_total_regions_skipped());
  } else {
    // Young
    thlog_or_tty->print_cr("[YOUNG] | BACK_PTRS = %lu", backward_ref);
    thlog_or_tty->print_cr("[YOUNG] | TIME_SCAN_H2_CT %.3lf ms", h2_card_table_scan_time_ms);
    thlog_or_tty->print_cr("[YOUNG] | RECLAIMED_REGIONS = %u", reclaimed_regions_count);
  }

  thlog_or_tty->flush();

  // Init the statistics counters of TeraHeap to zero for the next GC  
  _is_mixed_gc = false;
  _is_full_gc = false;

  reset_counters();
}

double TeraStatistics::get_max_thr_time_alloc_h2() {
  double max_time = 0.0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    double time = thr_time_alloc_h2[i];
    if (time > max_time) {
      max_time = time;
    }
  }

  return max_time;
}

double TeraStatistics::get_max_thr_time_copy_h2() {
  double max_time = 0.0;
  for (uint i = 0; i < ParallelGCThreads; i++) {
    double time = thr_time_copy_h2[i];
    if (time > max_time) {
      max_time = time;
    }
  }

  return max_time;
}

double TeraStatistics::get_sum_thr_time_alloc_h2(){
  double sum_time = 0.0;
  for(uint i = 0; i < ParallelGCThreads; i++){
    sum_time += thr_time_alloc_h2[i];
  }
  return sum_time;
}

double TeraStatistics::get_sum_thr_time_copy_h2(){
  double sum_time = 0.0;
  for(uint i = 0; i < ParallelGCThreads; i++){
    sum_time += thr_time_copy_h2[i];
  }
  return sum_time;
}

size_t TeraStatistics::get_sum_thr_bytes_copy_h2(){
  size_t sum_bytes = 0;
  for(uint i = 0; i < ParallelGCThreads; i++){
    sum_bytes += thr_bytes_copy_h2[i];
  }
  return sum_bytes;
}

double TeraStatistics::get_average_time_ms_h2() {
  uint contributing_threads_alloc = 0, contributing_threads_copy = 0;

  for(uint i = 0; i < ParallelGCThreads; i++){
    if(thr_time_alloc_h2[i] != 0) contributing_threads_alloc++;
    if(thr_time_copy_h2[i] != 0) contributing_threads_copy++;
  }

  double sum_time_copy = get_sum_thr_time_copy_h2();
  double sum_time_alloc = get_sum_thr_time_alloc_h2();

  double avg_copy = 0.0, avg_alloc = 0.0;

  if(contributing_threads_alloc == 0) {
    avg_alloc = 0.0;
  } else {
    avg_alloc = sum_time_alloc / (double) contributing_threads_alloc;
  }

  if(contributing_threads_copy == 0) {
    avg_copy = 0.0;
  } else {
    avg_copy = sum_time_copy / (double) contributing_threads_copy;
  }
  return (avg_copy + avg_alloc)* 1000.0;
}

#ifdef BACK_REF_STAT
// Add a new entry to the histogram for 'obj'
void TeraStatistics::h2_update_back_ref_stats(bool is_old, bool is_tera_cache) {
	std::tr1::tuple<int, int, int> val;
	std::tr1::tuple<int, int, int> new_val;

	val = histogram[back_ref_obj];
	
	if (is_old) {                         // Reference is in the old generation  
		new_val = std::tr1::make_tuple(
				std::tr1::get<0>(val),
				std::tr1::get<1>(val) + 1,
				std::tr1::get<2>(val));
	}
	else if (is_tera_cache) {             // Reference is in the tera cache
		new_val = std::tr1::make_tuple(
				std::tr1::get<0>(val),
				std::tr1::get<1>(val),
				std::tr1::get<2>(val) + 1);
	} else {                              // Reference is in the new generation
		new_val = std::tr1::make_tuple(
				std::tr1::get<0>(val) + 1,
				std::tr1::get<1>(val),
				std::tr1::get<2>(val));
	}
	
	histogram[back_ref_obj] = new_val;
}

// Enable traversal `obj` for backward references.
void TeraStatistics::h2_enable_back_ref_traversal(oop* obj) {
	std::tr1::tuple<int, int, int> val;

	val = std::tr1::make_tuple(0, 0, 0);

	back_ref_obj = obj;
  // Add entry to the histogram if does not exist
	histogram[obj] = val;
}

// Print the histogram
void TeraStatistics::h2_print_back_ref_stats() {
	std::map<oop *, std::tr1::tuple<int, int, int> >::const_iterator it;
	
	thlog_or_tty->print_cr("Start_Back_Ref_Statistics\n");

	for(it = histogram.begin(); it != histogram.end(); ++it) {
		if (std::tr1::get<0>(it->second) > 1000 || std::tr1::get<1>(it->second) > 1000) {
			thlog_or_tty->print_cr("[HISTOGRAM] ADDR = %p | NAME = %s | NEW = %d | OLD = %d | TC = %d\n",
					it->first, oop(it->first)->klass()->internal_name(), std::tr1::get<0>(it->second),
					std::tr1::get<1>(it->second), std::tr1::get<2>(it->second));
		}
	}
	
	thlog_or_tty->print_cr("End_Back_Ref_Statistics\n");

	// Empty the histogram at the end of each minor gc
	histogram.clear();
}
#endif

#ifdef FWD_REF_STAT
// Add a new entry to the histogram for forward reference that start from
// H1 and results in 'obj' in H2 
void TeraStatistics::h2_add_fwd_ref_stat(oop obj) {
	fwd_ref_histo[obj]++;
}

// Print the histogram
void TeraStatistics::h2_print_fwd_ref_stat() {
	std::map<oop,int>::const_iterator it;

	thlog_or_tty->print_cr("Start_Fwd_Ref_Statistics\n");

	for(it = fwd_ref_histo.begin(); it != fwd_ref_histo.end(); ++it) {
		thlog_or_tty->print_cr("[FWD HISTOGRAM] ADDR = %p | NAME = %s | REF = %d\n",
				(HeapWord *)it->first, oop(it->first)->klass()->internal_name(), it->second);
	}
	
	thlog_or_tty->print_cr("End_Fwd_Ref_Statistics\n");

	// Empty the histogram at the end of each major gc
	fwd_ref_histo.clear();
}
#endif

void TeraStatistics::thr_add_regions_scanned(uint thread_id, int num_regions) {
  thr_fgc_regions_scanned[thread_id] += num_regions;
}

void TeraStatistics::thr_add_regions_skipped(uint thread_id, int num_regions) {
  thr_fgc_regions_skipped[thread_id] += num_regions;
}

int TeraStatistics::get_total_regions_scanned() {
  int num_regions = 0;
  for (uint i = 0; i < ParallelGCThreads; i++)
    num_regions += thr_fgc_regions_scanned[i];
  return num_regions;
}

int TeraStatistics::get_total_regions_skipped() {
  int num_regions = 0;
  for (uint i = 0; i < ParallelGCThreads; i++)
    num_regions += thr_fgc_regions_skipped[i];
  return num_regions;
}

