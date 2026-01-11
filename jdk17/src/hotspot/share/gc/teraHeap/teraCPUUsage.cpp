#include "gc/teraHeap/teraCPUUsage.hpp"
#include "gc/teraHeap/teraHeap.hpp"

#define BUFFER_SIZE 1024

TeraCPUUsage::TeraCPUUsage() {
  conc_gc_thr_cpu_start_time = NEW_C_HEAP_ARRAY(double, ConcGCThreads + 1, mtGC);
  conc_gc_thr_cpu_total_time = NEW_C_HEAP_ARRAY(double, ConcGCThreads + 1, mtGC);
  conc_gc_thr_id = NEW_C_HEAP_ARRAY(pid_t, ConcGCThreads + 1, mtGC);
  total_stw_cpu_time = 0;

  refinement_thr_cpu_start_time = NEW_C_HEAP_ARRAY(double, G1ConcRefinementThreads, mtGC);
  refinement_thr_cpu_total_time = NEW_C_HEAP_ARRAY(double, G1ConcRefinementThreads, mtGC);

#ifdef DYNAMICHEAP_DEBUG
  fprintf(stderr, "refinement threads %d gc threads %d parallel threads %d\n", ConcGCThreads, G1ConcRefinementThreads, ParallelGCThreads);
#endif

  memset(conc_gc_thr_cpu_start_time, 0, (ConcGCThreads + 1) * sizeof(double));
  memset(conc_gc_thr_cpu_total_time, 0, (ConcGCThreads + 1) * sizeof(double));
  memset(conc_gc_thr_id, 0, (ConcGCThreads + 1) * sizeof(pid_t));

  memset(refinement_thr_cpu_start_time, 0, G1ConcRefinementThreads * sizeof(double));
  memset(refinement_thr_cpu_total_time, 0, G1ConcRefinementThreads * sizeof(double));


  total_stw_cpu_time = 0;

  num_mutator_threads = 0;
  interval = 0;

  processors = os::active_processor_count();
}

TeraCPUUsage::~TeraCPUUsage() {
  FREE_C_HEAP_ARRAY(double, conc_gc_thr_cpu_start_time);
  FREE_C_HEAP_ARRAY(double, conc_gc_thr_cpu_total_time);
  FREE_C_HEAP_ARRAY(pid_t, conc_gc_thr_id);
  FREE_C_HEAP_ARRAY(double, refinement_thr_cpu_start_time);
  FREE_C_HEAP_ARRAY(double, refinement_thr_cpu_total_time);
}

void TeraCPUUsage::calculate_stw_pauses(double time, bool is_pause_cleanup) {
	
#ifdef DYNAMICHEAP_DEBUG
   fprintf(stderr, "in calculate_stw_pauses: num_mutator_threads = %d, os::active_processor_count = %d\n", num_mutator_threads, os::active_processor_count());
  fprintf(stderr,"	registering stw pause time %.6f and total time is %.6f\n", (time * MIN(num_mutator_threads, os::active_processor_count())), total_stw_cpu_time);  
#endif
  if (is_pause_cleanup) {
    total_stw_cpu_time += (time * MIN(num_mutator_threads, os::active_processor_count()));
    return;
  }

  total_stw_cpu_time += (time * MIN(num_mutator_threads, os::active_processor_count()));
}

void TeraCPUUsage::start_conc_gc_thr_timers(int thr_id) {

#ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "	starting conc gc timer for thread %d\n", thr_id);
#endif
  conc_gc_thr_cpu_start_time[thr_id] = os::elapsedVTime();
  conc_gc_thr_id[thr_id] = os::current_thread_id();
}

void TeraCPUUsage::start_refinement_thr_timers(int thr_id) {

#ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "	starting refinmenet thread timer for thread %d\n", thr_id);
#endif


  refinement_thr_cpu_start_time[thr_id] = os::elapsedVTime();
}

void TeraCPUUsage::reset_conc_gc_thr_timers(int thr_id) {
  conc_gc_thr_cpu_start_time[thr_id] = 0;
  conc_gc_thr_id[thr_id] = 0;
}

void TeraCPUUsage::reset_refinement_thr_timers(int thr_id) {
  refinement_thr_cpu_start_time[thr_id] = 0;
}

void TeraCPUUsage::read_conc_gc_thr_cpu_time(int thr_id) {
 conc_gc_thr_cpu_total_time[thr_id] += os::elapsedVTime() - conc_gc_thr_cpu_start_time[thr_id]; 
 reset_conc_gc_thr_timers(thr_id);

#ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "	ending conc gc timer in %.6f for thread %d\n",conc_gc_thr_cpu_total_time[thr_id], thr_id);
#endif
 
}

void TeraCPUUsage::print_conc_gr_total_times(int thr_id) {

#ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "	reading conc gc timer in %.6f for thread %d\n",conc_gc_thr_cpu_total_time[thr_id], thr_id);
#endif
}

void TeraCPUUsage::read_refinement_thr_cpu_time(int thr_id) {
 refinement_thr_cpu_total_time[thr_id] += os::elapsedVTime() - refinement_thr_cpu_start_time[thr_id]; 
 reset_refinement_thr_timers(thr_id);

#ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "	ending refinement thread timer in %.6f for thread %d\n",refinement_thr_cpu_total_time[thr_id], thr_id);
#endif
}


double read_thread_time(pid_t tid) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
  //printf("[DEBUG] Reading CPU time for thread ID: %d\n", tid);

  FILE* file = fopen(path, "r");
  if (!file) {
    return 0;
  }

  char line[BUFFER_SIZE];
  if (!fgets(line, sizeof(line), file)) {
    fclose(file);
    return 0;
  }
  fclose(file);

//  fprintf(stderr, "[DEBUG] Raw stat line: %s\n", line);

  char* stats = strrchr(line, ')');
  if (!stats || *(stats + 1) != ' ') {
    return 0;
  }
  stats += 2;
  unsigned long long utime = 0, stime = 0;
  sscanf(stats, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %llu", &utime, &stime);

  //printf("[DEBUG] utime (field 14): %llu\n", utime);
  //printf("[DEBUG] stime (field 15): %llu\n", stime);

  double cpu_time_secs = (utime + stime) / (double) sysconf(_SC_CLK_TCK);
  //printf("[DEBUG] Total CPU time (ticks): %llu\n", utime + stime);
  //printf("[DEBUG] Total CPU time (seconds): %.6f\n", cpu_time_secs);

  return cpu_time_secs;
} 

double TeraCPUUsage::calculate_cpu_total_gc_time() {
  double current_thread_cpu_time = 0.0;
  double total_cpu_time = 0.0;
  double conc_gc_thr_cpu_time = 0.0;
  double temp = 0.0;

  for (size_t i = 0; i < G1ConcRefinementThreads; i++) {
   total_cpu_time += refinement_thr_cpu_total_time[i];
   refinement_thr_cpu_total_time[i] = 0.0;
  }

  for (size_t i = 0; i < ConcGCThreads + 1; i++) {
    if (conc_gc_thr_cpu_start_time[i] == 0) {
      conc_gc_thr_cpu_time += conc_gc_thr_cpu_total_time[i];
      conc_gc_thr_cpu_total_time[i] = 0;
      continue;
    }

    current_thread_cpu_time = read_thread_time(conc_gc_thr_id[i]); // tid from array // check utime and stime
    temp = conc_gc_thr_cpu_total_time[i] + (current_thread_cpu_time - conc_gc_thr_cpu_start_time[i]);

    if (temp < 0) {
      temp = 0;
    }

    conc_gc_thr_cpu_time += temp;
    conc_gc_thr_cpu_total_time[i] = 0;
    conc_gc_thr_cpu_start_time[i] = current_thread_cpu_time;
  }

  conc_gc_thr_cpu_time = (num_mutator_threads + (int) ConcGCThreads > os::active_processor_count())
    ? conc_gc_thr_cpu_time - MAX(0, (os::active_processor_count() - num_mutator_threads) * interval)
    : 0;

#ifdef DYNAMICHEAP_DEBUG
  fprintf(stderr, "for conc gc threads: %f, from stw pauses %f\n", conc_gc_thr_cpu_time, total_stw_cpu_time);
#endif

  total_cpu_time += (total_stw_cpu_time + conc_gc_thr_cpu_time);
  total_stw_cpu_time = 0;

  return total_cpu_time * MILLIUNITS; // convert to ms
		
}

TeraSimpleCPUUsage::TeraSimpleCPUUsage() {}
TeraSimpleCPUUsage::~TeraSimpleCPUUsage() {}
TeraMultiExecutorCPUUsage::TeraMultiExecutorCPUUsage() {}
TeraMultiExecutorCPUUsage::~TeraMultiExecutorCPUUsage() {}

// This functions uses /proc/stat to read the cpu usage. This is the
// ideal scenario of calculating the iowait time because we run
void TeraSimpleCPUUsage::read_cpu_usage(bool is_start, bool is_gc) {
  unsigned long long total_cpu;
  unsigned long long cpu_iowait;
  FILE* stat_file = fopen("/proc/stat", "r");

  if (!stat_file) {
    fprintf(stderr, "Failed to open /proc/stat\n");
    return;
  }

  char buffer[BUFFER_SIZE];
  unsigned long long user, nice, system, idle, iowait, irq, softirq;

  while (fgets(buffer, BUFFER_SIZE, stat_file)) {
    if (strncmp(buffer, "cpu", 3) == 0) {
      sscanf(buffer, "%*s %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq);

      total_cpu = user + nice + system + iowait + irq + softirq + idle;
      cpu_iowait = iowait + idle;

      break;
    }
  }
if (!is_gc) {
    fprintf(stdout, "[INFO] Non-GC phase: total_cpu = %llu, iowait = %llu\n",
           total_cpu, cpu_iowait);
    is_start ? cpu_start = total_cpu : cpu_end = total_cpu;
    is_start ? iowait_start = cpu_iowait : iowait_end = cpu_iowait;
} else {
    fprintf(stdout, "[INFO] GC phase: total_cpu = %llu, iowait = %llu\n",
           total_cpu, cpu_iowait);
    is_start ? gc_cpu_start = total_cpu : gc_cpu_end = total_cpu;
    is_start ? gc_iowait_start = cpu_iowait : gc_iowait_end = cpu_iowait;
}


  int res = fclose(stat_file);
  if (res != 0) {
    fprintf(stderr, "Error closing file");
  }
}

// Calculate iowait time based on the following formula
//
//                (cpu_iowait_after - cpu_iowait_before) 
//  iowait_time = -------------------------------------- * duration 
//                 (total_cpu_after - total_cpu_before)
//
void TeraSimpleCPUUsage::calculate_iowait_time(double duration,
                                               double *iowait_time, bool is_gc) {

  unsigned long long iowait_diff = 0;
  unsigned long long cpu_diff = 0;

  iowait_diff = (iowait_end - iowait_start);
  cpu_diff =  (cpu_end - cpu_start);


  *iowait_time = (cpu_diff == 0) ? 0 : ((double) iowait_diff / cpu_diff) * duration * processors;

  for (int i = 0; i < 5; i++) print_conc_gr_total_times(i);
}
  

// Read CPU usage
void TeraMultiExecutorCPUUsage::read_cpu_usage(bool is_start, bool is_gc) {
  if (is_gc)
    return;

  struct rusage tmp;
  getrusage(RUSAGE_SELF, &tmp);

  is_start ? start = tmp : end = tmp;
}

// Calculate iowait time of the process
void TeraMultiExecutorCPUUsage::calculate_iowait_time(double duration,
                                                      double *iowait_time,
                                                      bool is_gc) {
  if (is_gc) {
    *iowait_time = 0;
    return;
  }

  double user_time_ms = (double) (end.ru_utime.tv_sec - start.ru_utime.tv_sec) * 1000 +
                        (double) (end.ru_utime.tv_usec - start.ru_utime.tv_usec) / 1000;

  double system_time_ms = (double) (end.ru_stime.tv_sec - start.ru_stime.tv_sec) * 1000 +
                          (double) (end.ru_stime.tv_usec - start.ru_stime.tv_usec) / 1000;

  *iowait_time = duration - ((user_time_ms + system_time_ms) / 8);
}


