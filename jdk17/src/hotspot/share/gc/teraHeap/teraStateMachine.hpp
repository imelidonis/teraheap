#ifndef SHARE_GC_TERAHEAP_TERASTATEMACHINE_HPP
#define SHARE_GC_TERAHEAP_TERASTATEMACHINE_HPP

#include "gc/teraHeap/teraEnum.h"
#include "gc/parallel/parallelScavengeHeap.hpp"
#include "gc/g1/g1CollectedHeap.hpp"
#include "gc/g1/g1Policy.hpp"
#include "gc/g1/g1IHOPControl.hpp"
#include "memory/allocation.hpp"
#include "memory/sharedDefines.h"
#include "gc/shared/gc_globals.hpp"
#include "oops/oop.inline.hpp"




class TeraStateMachine : public CHeapObj<mtInternal> {
public:
  virtual void fsm(states *cur_state, actions *cur_action, double gc_time_ms, double io_time_ms) = 0;

  virtual void state_wait_after_grow(states *cur_state, actions *cur_action,
                                     double gc_time_ms, double io_time_ms) = 0;

  virtual void state_wait_after_shrink(states *cur_state, actions *cur_action,
                                       double gc_time_ms, double io_time_ms) = 0;

  virtual void state_no_action(states *cur_state, actions *cur_action,
                       double gc_time_ms, double io_time_ms) = 0;
  
  // Read the memory statistics for the cgroup
  size_t read_cgroup_mem_stats(bool read_page_cache);

  // Read the process anonymous memory
  size_t read_process_anon_memory();
};

class G1TeraSimpleWaitStateMachine : public TeraStateMachine {
  private:

  double delay_before_action = 0;
  double previous_interval_gc = 0;
  double previous_interval_io = 0;
  actions last_action = GROW_HEAP;
  unsigned no_action_intervals = 0;

  double* _gc_history;
  double* _io_history;
  int history_index = 0;

  void analyze_history(states *cur_state_hist, actions *cur_action_hist, double *avg_gc_time, double *avg_io_time);

  public: 

  G1TeraSimpleWaitStateMachine() {
    _gc_history = NEW_C_HEAP_ARRAY(double, IntervalHistoryAmount, mtGC);
    _io_history = NEW_C_HEAP_ARRAY(double, IntervalHistoryAmount, mtGC);

    memset(_gc_history, 0, IntervalHistoryAmount * sizeof(double));
    memset(_io_history, 0, IntervalHistoryAmount * sizeof(double));
    thlog_or_tty->print_cr("Resizing Policy = TeraSimpleStateMacine\n");
    thlog_or_tty->flush();
  }

  ~G1TeraSimpleWaitStateMachine() {
    FREE_C_HEAP_ARRAY(double, _gc_history);
    FREE_C_HEAP_ARRAY(double, _io_history);
  }

  virtual void state_wait_after_grow(states *cur_state, actions *cur_action, double gc_time_ms, double io_time_ms);
  virtual void state_wait_after_shrink(states *cur_state, actions *cur_action, double gc_time_ms, double io_time_ms);
  virtual void state_no_action(states *cur_state, actions *cur_action, double gc_time_ms, double io_time_ms);

  virtual void fsm(states *cur_state, actions *cur_action, double gc_time_ms, double io_time_ms);
};

#endif // SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERASTATEMACHINE_HPP

