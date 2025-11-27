#ifndef SHARE_GC_TERAHEAP_TERADYNAMICRESIZINGPOLICY_HPP
#define SHARE_GC_TERAHEAP_TERADYNAMICRESIZINGPOLICY_HPP

#include "gc/teraHeap/teraEnum.h"
#include "gc/teraHeap/teraCPUUsage.hpp"
#include "gc/teraHeap/teraStateMachine.hpp"
#include "memory/allocation.hpp"
#include "memory/sharedDefines.h"
#include <stdlib.h>
#include <string.h>

#define HIST_SIZE 5
#define GC_HIST_SIZE 1
#define NUM_ACTIONS 8
#define NUM_STATES 4
#define NAME_LEN 20

class TeraDynamicResizingPolicy : public CHeapObj<mtInternal> {
private:
  char state_name[NUM_STATES][NAME_LEN]; //< Define state names
  char action_name[NUM_ACTIONS][NAME_LEN]; //< Define state names
  uint64_t window_start_time;         //< Window start time
  double gc_time;                     //< Total gc time for the
                                      // interval of the window
  double interval;                    //< Interval of the window

  actions cur_action;                 //< Current action
  actions prev_action;                //< Previous action

  states cur_state;                   //< Current state
				      //
  double hist_gc_time[GC_HIST_SIZE];  //< History of the gc time in
                                      // previous intervals
  double hist_iowait_time[HIST_SIZE]; //< History of the iowait time in
                                      // previous intervals
  TeraStateMachine *state_machine;    //< FSM
  TeraCPUUsage *cpu_usage;             // Cpu utilization for
                                      // estimating iowait
  double interval_window_start_timer;

  double full_gc_timer;

  bool should_shrink;
  bool should_grow;
  int type_of_gc;
  double g1_iowait_time_ms;           //< iowait time
  // Check if the window limit exceed time
  bool is_window_limit_exeed();

  // Count timer. We avoid to use os::elapsed_time() because internally
  // uses the clock_get_time which adds extra overhead. This function
  // is executed in the common path.
  uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
  }

  // Find the average of the array elements
  double calc_avg_time(double *arr, int size);

  // After each growing operation of H1 we wait to see the effect of
  // the action. If we reach a gc or the io cost is higher than gc
  // cost then we go to no action state. 
  void state_wait_after_grow(double io_time_ms, double gc_time_ms);

  // Initialize the array of state names
  void init_state_actions_names();

  // Intitilize the policy of the state machine.
  TeraStateMachine* init_state_machine_policy();
  
  // Intitilize the cpu usage statistics
  TeraCPUUsage* init_cpu_usage_stats();
  
  // Calculation of the GC cost prediction.
  //double calculate_gc_cost(double gc_time_ms);

  // Print states (for debugging and logging purposes)
  void print_state_action();

  // GrowH1 action
  void action_grow_heap(bool remark_phase);
  
  // ShrinkH1 action
  void action_shrink_heap();

  // Calculate the average of gc and io costs and return their values.
  // We use these values to determine the next actions.
  void calculate_gc_io_costs(double *avg_gc_time_ms, double *avg_io_time_ms);
                           
  
  // Print counters for debugging purposes
  void debug_print(double avg_iowait_time, double avg_gc_time, double interval,
                   double cur_iowait_time, double cur_gc_time);
  
  // Save the history of the GC and iowait overheads. We maintain two
  // ring buffers (one for GC and one for iowait) and update these
  // buffers with the new values for GC cost and IO overhead.
  void history(double gc_time, double iowait_time_ms);
  
  // Set current time since last window
  void reset_counters();

public:
  // Constructor
  TeraDynamicResizingPolicy();

  // Destructor
  virtual ~TeraDynamicResizingPolicy();


  void g1_record_mutator_thread_id(pid_t tid);

  void start_full_gc_timer();
  void end_full_gc_timer();

  bool should_grow_heap() { return should_grow; }

  bool should_shrink_heap() { return should_shrink; }

  void dram_repartition(bool remark_phase = false);

  void g1_start_interval_stats(void);
  void g1_end_interval_stats(int gc_type);

  void register_stw_pause(double time, bool is_pause_cleanup);

  void register_refinement_threads_timers(int worker_id, bool start);

  void register_concurrent_gc_threads_timers(int worker_id, bool start);
};

#endif // SHARE_GC_TERAHEAP_TERADYNAMICRESIZINGPOLICY_HPP
