#include "gc/g1/g1CollectedHeap.hpp"
#include "gc/parallel/parallelScavengeHeap.hpp"
#include "gc/teraHeap/teraDynamicResizingPolicy.hpp"
#include "memory/universe.hpp"
#include "gc/teraHeap/teraHeap.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include <ioWait.hpp>

#define BUFFER_SIZE 1024
#define CYCLES_PER_SECOND 2.4e9; // CPU frequency of 2.4 GHz
#define REGULAR_INTERVAL ((10LL * 1000)) 

static inline double since_test_start_s() {
  // Latches the first time it's called, then returns elapsed seconds since then
  static double t0 = os::elapsedTime();
  return os::elapsedTime() - t0;
}
  
// Intitilize the cpu usage statistics
TeraCPUUsage* TeraDynamicResizingPolicy::init_cpu_usage_stats() {
  return TeraCPUStatsPolicy ? static_cast<TeraCPUUsage*>(new TeraMultiExecutorCPUUsage()) :
                              static_cast<TeraCPUUsage*>(new TeraSimpleCPUUsage());
}

// Initialize the policy of the state machine
TeraStateMachine* TeraDynamicResizingPolicy::init_state_machine_policy() {
  switch (TeraResizingPolicy) {
    case 1:
      return new G1TeraSimpleWaitStateMachine();
  }
  
  return new G1TeraSimpleWaitStateMachine();
}

// We use this function to take decision in case of minor GC which
// happens before a major gc.
void TeraDynamicResizingPolicy::dram_repartition(bool remark_phase) { // change name for ramrk and full gc
  double avg_gc_time_ms, avg_io_time_ms;

  calculate_gc_io_costs(&avg_gc_time_ms, &avg_io_time_ms);

#ifdef DYNAMICHEAP_DEBUG
  fprintf(stderr, "[TeraDynamicResizingPolicy::dram_repartition() avg gc time ms %f, avg io time ms %f\n", avg_gc_time_ms, avg_io_time_ms);
#endif
  state_machine->fsm(&cur_state, &cur_action, avg_gc_time_ms, avg_io_time_ms);
  //print_action(cur_action);
  print_state_action();

  switch (cur_action) {
    case SHRINK_HEAP:
      action_shrink_heap();
      break;
    case GROW_HEAP:
      action_grow_heap(remark_phase);
      break;
    default:
      break;
  }

  prev_action = cur_action;
  g1_start_interval_stats();
}

void TeraDynamicResizingPolicy::g1_start_interval_stats(void) {
	
#ifdef DYNAMICHEAP_DEBUG
	fprintf(stderr, "\n--------------------starting interval timer and cpu stats-------------------\n\n");
#endif
  interval_window_start_timer = os::elapsedTime(); 
  gc_time = 0;
  ebpf_enable_tracking();
}

void TeraDynamicResizingPolicy::g1_end_interval_stats(int gc_type) {

  type_of_gc = gc_type;
  interval = (os::elapsedTime() - interval_window_start_timer);
  cpu_usage->set_interval(interval);

  g1_iowait_time_ms = ebpf_disable_tracking();
  g1_iowait_time_ms = MIN(g1_iowait_time_ms, interval * MILLIUNITS * os::active_processor_count());

#ifdef DYNAMICHEAP_DEBUG
	fprintf(stderr, "\n-------------------ending interval timer %.6f and cpu stats----------------------\n\n", interval);
#endif
}

void TeraDynamicResizingPolicy::g1_record_mutator_thread_id(pid_t tid) {
  cpu_usage->incr_mutator_threads();
  ebpf_add_tid(tid);
}

// Print states (for debugging and logging purposes)
void TeraDynamicResizingPolicy::print_state_action() {

#ifdef DYNAMICHEAP_DEBUG
  fprintf(stderr, "at timestamp = %f\n", since_test_start_s());
  fprintf(stderr, "STATE = %s\n", state_name[cur_state]);
  fprintf(stderr, "ACTION = %s\n", action_name[cur_action]);
#endif
}

// Initialize the array of state names
void TeraDynamicResizingPolicy::init_state_actions_names() {
  strncpy(action_name[0], "NO_ACTION",       10);
  strncpy(action_name[1], "SHRINK_HEAP",       12);
  strncpy(action_name[2], "GROW_HEAP",          10);
  strncpy(action_name[3], "CONTINUE",         9);
  strncpy(action_name[4], "IOSLACK",          8);
  strncpy(action_name[5], "WAIT_AFTER_GROW", 16);
  
  strncpy(state_name[0], "S_NO_ACTION",      12);
  strncpy(state_name[1], "S_WAIT_SHRINK",    14);
  strncpy(state_name[2], "S_WAIT_GROW",      12);
  strncpy(state_name[3], "S_STABLE",      9);
}

// Calculate the average of gc and io costs and return their values.
// We use these values to determine the next actions.
void TeraDynamicResizingPolicy::calculate_gc_io_costs(double *avg_gc_time_ms,
                                                      double *avg_io_time_ms) {
  double iowait_time_ms = g1_iowait_time_ms;

  history(cpu_usage->calculate_cpu_total_gc_time(), iowait_time_ms);

  *avg_io_time_ms = calc_avg_time(hist_iowait_time, HIST_SIZE);
  *avg_gc_time_ms = calc_avg_time(hist_gc_time, GC_HIST_SIZE);
}

TeraDynamicResizingPolicy::TeraDynamicResizingPolicy() {

  cpu_usage = init_cpu_usage_stats();

  window_start_time = rdtsc();
  interval_window_start_timer = os::elapsedTime(); 
  gc_time = 0;
  cur_action = NO_ACTION;
  cur_state = S_NO_ACTION;
  prev_action = NO_ACTION;
  memset(hist_gc_time, 0, GC_HIST_SIZE * sizeof(double));
  memset(hist_iowait_time, 0, HIST_SIZE * sizeof(double));
  g1_iowait_time_ms = 0;
  init_state_actions_names();
  state_machine = init_state_machine_policy();
  full_gc_timer = 0;

  ebpf_start();
}

TeraDynamicResizingPolicy::~TeraDynamicResizingPolicy() {
  delete state_machine;
  delete cpu_usage;
  ebpf_stop();
}

void TeraDynamicResizingPolicy::start_full_gc_timer() { 
  full_gc_timer = os::elapsedTime();
}

void TeraDynamicResizingPolicy::end_full_gc_timer() { 
  register_stw_pause(os::elapsedTime() - full_gc_timer, false);
}

void TeraDynamicResizingPolicy::action_grow_heap(bool remark_phase) {
  G1CollectedHeap* g1h = G1CollectedHeap::heap();

  should_grow = true;
  g1h->resize_heap_if_necessary();
  if (remark_phase) {
    g1h->uncommit_regions_if_necessary();
  }

  should_grow = false;
}

void TeraDynamicResizingPolicy::action_shrink_heap() {
  G1CollectedHeap *g1h = G1CollectedHeap::heap();

  should_shrink = true;
  g1h->resize_heap_if_necessary();
  g1h->uncommit_regions_if_necessary();
  should_shrink = false;
}

// Print counters for debugging purposes
void TeraDynamicResizingPolicy::debug_print(double avg_iowait_time, double avg_gc_time,
                                            double interval, double cur_iowait_time, double cur_gc_time) {
  thlog_or_tty->print_cr("avg_iowait_time_ms = %lf\n", avg_iowait_time);
  thlog_or_tty->print_cr("avg_gc_time_ms = %lf\n", avg_gc_time);
  thlog_or_tty->print_cr("cur_iowait_time_ms = %lf\n", cur_iowait_time);
  thlog_or_tty->print_cr("cur_gc_time_ms = %lf\n", cur_gc_time);
  thlog_or_tty->print_cr("interval = %lf\n", interval);
  thlog_or_tty->flush();
}


// Find the average of the array elements
double TeraDynamicResizingPolicy::calc_avg_time(double *arr, int size) {
  double sum = 0;

  for (int i = 0; i < size; i++) {
    sum += arr[i];
  }

  return (double) sum / size;
}


// Save the history of the GC and iowait overheads. We maintain two
// ring buffers (one for GC and one for iowait) and update these
// buffers with the new values for GC cost and IO overhead.
void TeraDynamicResizingPolicy::history(double gc_time_ms, double iowait_time_ms) {
  static int index = 0;

  hist_gc_time[index % GC_HIST_SIZE] = gc_time_ms;
  hist_iowait_time[index % HIST_SIZE] = iowait_time_ms;
  index++;
}


void TeraDynamicResizingPolicy::register_stw_pause(double time, bool is_pause_cleanup) {
  cpu_usage->calculate_stw_pauses(time, is_pause_cleanup);
}

void TeraDynamicResizingPolicy::register_refinement_threads_timers(int worker_id, bool start) {
  if (start) {
    cpu_usage->start_refinement_thr_timers(worker_id);
    return; 
  }
  cpu_usage->read_refinement_thr_cpu_time(worker_id);

}

void TeraDynamicResizingPolicy::register_concurrent_gc_threads_timers(int worker_id, bool start) {
  if (start) {
    cpu_usage->start_conc_gc_thr_timers(worker_id);
    return; 
  }
  cpu_usage->read_conc_gc_thr_cpu_time(worker_id);
}
