#ifndef SHARE_GC_TERAHEAP_TERACPUUSAGE_HPP
#define SHARE_GC_TERAHEAP_TERACPUUSAGE_HPP

#include "memory/allocation.hpp"
#include <sys/resource.h>

#define STAT_START true
#define STAT_END false
#define GC_STAT true
#define MUTATOR_STAT false

// TODO: Should we need to calculte the system time?
class TeraCPUUsage : public CHeapObj<mtInternal> {
protected: 
  double *conc_gc_thr_cpu_start_time;
  double *conc_gc_thr_cpu_total_time;
  pid_t  *conc_gc_thr_id;

  double *refinement_thr_cpu_start_time;
  double *refinement_thr_cpu_total_time;

  double total_stw_cpu_time;

  int num_mutator_threads;
  double interval;

  int processors;

public:
  TeraCPUUsage();
 ~TeraCPUUsage();
  
  virtual void print() const = 0;

  // Read CPU usage
  virtual void read_cpu_usage(bool is_start, bool is_gc) = 0;
  
  // Calculate iowait time of the process
  virtual void calculate_iowait_time(double duration, double *iowait_time,
                                     bool is_gc) = 0;
	
  void start_conc_gc_thr_timers(int thr_id);
  void start_refinement_thr_timers(int thr_id);

  void reset_conc_gc_thr_timers(int thr_id);
  void reset_refinement_thr_timers(int thr_id);

  void read_conc_gc_thr_cpu_time(int thr_id);
  void read_refinement_thr_cpu_time(int thr_id); 

  void print_conc_gr_total_times(int thr_id);
  void calculate_stw_pauses(double time, bool is_pause_cleanup);

  double calculate_cpu_total_gc_time();

  void incr_mutator_threads() { num_mutator_threads++; }
  void set_interval(double duration) { interval = duration; }
};

class TeraSimpleCPUUsage : public TeraCPUUsage {
private:

  unsigned long long iowait_start;    //< CPU iowait at the start of the window
  unsigned long long iowait_end;      //< CPU iowait at the end of the window

  unsigned long long cpu_start;       //< CPU usage at the start of the window
  unsigned long long cpu_end;         //< CPU usage at the end of the window
  
  unsigned long long gc_iowait_start; //< IO wait time created during gc
  unsigned long long gc_iowait_end;   //< IO wait time created during gc
  
  unsigned long long gc_cpu_start;    //< IO wait time created during gc
  unsigned long long gc_cpu_end;      //< IO wait time created during gc


public:


  TeraSimpleCPUUsage();
  virtual ~TeraSimpleCPUUsage();
void print() const override {
  printf("[TeraSimpleCPUUsage @ %p]\n", static_cast<const void*>(this));
  printf("[TeraSimpleCPUUsage] iowait_start: %llu, iowait_end: %llu\n", iowait_start, iowait_end);
  printf("[TeraSimpleCPUUsage] cpu_start: %llu, cpu_end: %llu\n", cpu_start, cpu_end);
  printf("[TeraSimpleCPUUsage] gc_iowait_start: %llu, gc_iowait_end: %llu\n", gc_iowait_start, gc_iowait_end);
  printf("[TeraSimpleCPUUsage] gc_cpu_start: %llu, gc_cpu_end: %llu\n", gc_cpu_start, gc_cpu_end);
}

  // Read CPU usage
  void read_cpu_usage(bool is_start, bool is_gc);
  
  // Calculate iowait time of the process
  void calculate_iowait_time(double duration, double *iowait_time, bool is_gc);
};

class TeraMultiExecutorCPUUsage : public TeraCPUUsage {
private:
  struct rusage start, end;

public:
  TeraMultiExecutorCPUUsage();
  virtual ~TeraMultiExecutorCPUUsage();
  void print() const override {
  printf("[TeraMultiExecutorCPUUsage @ %p]\n", static_cast<const void*>(this));
  printf(" rusage start: utime=%ld.%06ld stime=%ld.%06ld\n",
         start.ru_utime.tv_sec, start.ru_utime.tv_usec,
         start.ru_stime.tv_sec, start.ru_stime.tv_usec);

  printf("[TeraMultiExecutorCPUUsage] rusage end: utime=%ld.%06ld stime=%ld.%06ld\n",
         end.ru_utime.tv_sec, end.ru_utime.tv_usec,
         end.ru_stime.tv_sec, end.ru_stime.tv_usec);
}

  // Read CPU usage
  void read_cpu_usage(bool is_start, bool is_gc);
  
  // Calculate iowait time of the process
  void calculate_iowait_time(double duration, double *iowait_time, bool is_gc);
};

#endif // SHARE_GC_TERAHEAP_TERACPUUSAGE_HPP
