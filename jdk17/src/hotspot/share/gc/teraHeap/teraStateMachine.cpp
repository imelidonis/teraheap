#include "gc/teraHeap/teraStateMachine.hpp"

#define BUFFER_SIZE 1024
#define EPSILON 100
#define TRANSFER_THRESHOLD 0.4f


// Read the process anonymous memory
size_t TeraStateMachine::read_process_anon_memory() {
    // Open /proc/pid/stat file
    char path[BUFFER_SIZE];
    snprintf(path, sizeof(path), "/proc/%d/stat", getpid());
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        perror("Error opening /proc/pid/stat");
        exit(EXIT_FAILURE);
    }

    // Read the contents of /proc/pid/stat into a buffer
    char buffer[BUFFER_SIZE];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        perror("Error reading /proc/pid/stat");
        exit(EXIT_FAILURE);
    }

    // Close the file
    fclose(fp);

    // Tokenize the buffer to extract RSS
    char *token = strtok(buffer, " ");
    for (int i = 1; i < 24; ++i) {
        token = strtok(NULL, " ");
        if (token == NULL) {
            fprintf(stderr, "Error tokenizing /proc/pid/stat\n");
            exit(EXIT_FAILURE);
        }
    }

    // Convert the token to a long int
    size_t rss = atol(token) * os::vm_page_size();

    return rss;
}

// Read the memory statistics for the cgroup
size_t TeraStateMachine::read_cgroup_mem_stats(bool read_page_cache) {
  static int is_v2 = -1;  // Static variable to cache cgroup version detection
  if (is_v2 == -1) {
    struct stat buffer;
    is_v2 = (stat("/sys/fs/cgroup/cgroup.controllers", &buffer) == 0);
  }

  // Determine memory.stat path
  const char* file_path = is_v2 ? "/sys/fs/cgroup/memlim/memory.stat" : "/sys/fs/cgroup/memory/memlim/memory.stat";

  // Open the file for reading
  FILE* file = fopen(file_path, "r");

  if (file == NULL) {
    fprintf(stderr, "Failed to open memory.stat\n");
    return 0;
  }

  char line[BUFFER_SIZE];
  size_t res = 0;
  const char* search_key = read_page_cache ? (is_v2 ? "file" : "cache") : (is_v2 ? "anon" : "rss");

  // Read file and find the required value
  while (fgets(line, sizeof(line), file)) {
    if (strncmp(line, search_key, strlen(search_key)) == 0) { // Match the key at start
      res = atoll(line + strlen(search_key) + 1); // Extract the value
      break;
    }
  }

  // Close the file
  fclose(file);
  return res;
}

void G1TeraSimpleWaitStateMachine::state_wait_after_grow(states *cur_state, 
							 actions *cur_action, 
							 double gc_time_ms,
							 double io_time_ms) {

  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  size_t conc_mark_start_threshold = g1h->policy()->get_ihop_control()->get_conc_mark_start_threshold();
  size_t used_bytes = g1h->non_young_capacity_bytes();
  const size_t unused = g1h->unused_committed_regions_in_bytes();
  const size_t capacity = g1h->capacity();
  const size_t used = capacity - unused;
  bool ihop = false;

  if (conc_mark_start_threshold != 0) {
    ihop = ((double) used_bytes / conc_mark_start_threshold < 1.0);
  }  

  #ifdef ENABLE_THRESHOLD
    double relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;

    if (relative_diff <= 0.05 && ihop) {
      *cur_state = S_NO_ACTION;
      *cur_action = NO_ACTION;
      delay_before_action = gc_time_ms + io_time_ms;
      return;
    }
  #endif

  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  double gc_change_percent = (gc_time_ms - previous_interval_gc) / gc_time_ms;
  double io_change_percent = (io_time_ms - previous_interval_io) / io_time_ms;
  bool under_h1_max_limit = g1h->capacity() < g1h->max_capacity(); 
  bool overhead_increased = (gc_change_percent < 0 && io_change_percent < 0) ? false : true;
  bool gc_increased = (gc_change_percent > io_change_percent);
//  bool io_negligible = (io_time_ms * 1000.0 < gc_time_ms);
  bool io_negligible = false;

  if (NegligibleIOFactor > 0.0) {
    io_negligible = (io_time_ms * NegligibleIOFactor < gc_time_ms);
  }

  previous_interval_io = io_time_ms;
  previous_interval_gc = gc_time_ms;
  delay_before_action = gc_time_ms + io_time_ms;

  #ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "in state_wait_after_grow gc is %.2f, io is %.2f\n", gc_time_ms, io_time_ms); 
    fprintf(stderr, "gc_percent_change is %.2f, io_percehnt_change_is %.2f, overhead_increased is %s ihop is %s no_action_intervals = %d\n", 
        gc_change_percent, 
        io_change_percent, 
        overhead_increased ? "true" : "false", 
	ihop ? "true" : "false" , no_action_intervals);
  #endif

  if (!is_delay_decreased && overhead_increased) {
    if (NegligibleIOFactor > 0.0 && !gc_increased && io_negligible && (gc_time_ms > io_time_ms)) {
      gc_increased = true;
    }  

    if (gc_increased) {
      *cur_state = S_WAIT_GROW;
      *cur_action = (g1h->capacity() >= g1h->max_capacity()) ? WAIT_AFTER_GROW : GROW_HEAP;
      last_action = *cur_action;
    } else {
      *cur_state =  (used > (capacity * 0.95)) ? S_WAIT_GROW : S_WAIT_SHRINK;
      *cur_action = (used > (capacity * 0.95)) ? WAIT_AFTER_GROW : SHRINK_HEAP;
      last_action = *cur_action;
    }
    return;
  } 

  if (!ihop && under_h1_max_limit) { 
    *cur_state = S_WAIT_GROW;
    *cur_action = (g1h->capacity() >= g1h->max_capacity()) ? WAIT_AFTER_GROW : GROW_HEAP;
    last_action = *cur_action;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
  return;

}

void G1TeraSimpleWaitStateMachine::state_wait_after_shrink(states *cur_state, 
							   actions *cur_action, 
							   double gc_time_ms, 
							   double io_time_ms) {

  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  size_t conc_mark_start_threshold = g1h->policy()->get_ihop_control()->get_conc_mark_start_threshold();
  size_t used_bytes = g1h->non_young_capacity_bytes();
  const size_t unused = g1h->unused_committed_regions_in_bytes();
  const size_t capacity = g1h->capacity();
  const size_t used = capacity - unused;
  bool ihop = false;

  if (conc_mark_start_threshold != 0) {
    ihop = ((double) used_bytes / conc_mark_start_threshold < 1.0);
  }  
  #ifdef ENABLE_THRESHOLD

    double relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;

    if (relative_diff <= 0.05 && ihop) {
      *cur_state = S_NO_ACTION;
      *cur_action = NO_ACTION;
      delay_before_action = gc_time_ms + io_time_ms;
      return;
    }
  #endif

  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  double gc_change_percent = (gc_time_ms - previous_interval_gc) / gc_time_ms;
  double io_change_percent = (io_time_ms - previous_interval_io) / io_time_ms;
  bool overhead_increased = (gc_change_percent < 0 && io_change_percent < 0) ? false : true;
  bool gc_increased = (gc_change_percent > io_change_percent);
  //bool io_negligible = (io_time_ms * 1000.0 < gc_time_ms);
  bool io_negligible = false;

  if (NegligibleIOFactor > 0.0) {
    io_negligible = (io_time_ms * NegligibleIOFactor < gc_time_ms);
  }

  previous_interval_io = io_time_ms;
  previous_interval_gc = gc_time_ms;
  delay_before_action = gc_time_ms + io_time_ms;


  #ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "in state_wait_after_shrink gc is %.2f, io is %.2f\n", gc_time_ms, io_time_ms); 
    fprintf(stderr, "gc_percent_change is %.2f, io_percehnt_change_is %.2f, overhead_increased is %s ihop is %s no_action_intervals is %d\n", 
        gc_change_percent, 
        io_change_percent, 
        overhead_increased ? "true" : "false",
	ihop ? "true" : "false" , no_action_intervals);
  #endif

  if (!is_delay_decreased && overhead_increased) {
    if (NegligibleIOFactor > 0.0 && !gc_increased && io_negligible && (gc_time_ms > io_time_ms)) {
      gc_increased = true;
    }  
    if (gc_increased) {
      *cur_state = S_WAIT_GROW;
      *cur_action = (g1h->capacity() >= g1h->max_capacity()) ? WAIT_AFTER_GROW : GROW_HEAP;
      last_action = GROW_HEAP;
    } else {
      *cur_state = (used > (capacity * 0.90)) ? S_WAIT_GROW : S_WAIT_SHRINK;
      *cur_action = (used > (capacity * 0.90)) ? GROW_HEAP : SHRINK_HEAP;
      last_action = *cur_action;
    }
    return;
  } 


  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.8));

  if (ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }

  if (!ioslack && (used < (capacity * 0.90))) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_HEAP;
    last_action = SHRINK_HEAP;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

void G1TeraSimpleWaitStateMachine::state_no_action(states *cur_state, 
						   actions *cur_action, 
						   double gc_time_ms, 
						   double io_time_ms){

  #ifdef ENABLE_THRESHOLD
    double relative_diff = fabs((gc_time_ms + io_time_ms) - delay_before_action) / delay_before_action;

    if (relative_diff <= 0.05) {
      *cur_state = S_NO_ACTION;
      *cur_action = NO_ACTION;
      delay_before_action = gc_time_ms + io_time_ms;
      return;
    }
  #endif

  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;
  previous_interval_io = io_time_ms;
  previous_interval_gc = gc_time_ms;


  if (is_delay_decreased) {
    *cur_state = S_NO_ACTION;
    *cur_action = NO_ACTION;
    return;
  }

  G1CollectedHeap *g1h = G1CollectedHeap::heap();
  bool under_h1_max_limit = g1h->capacity() < g1h->max_capacity();
  double gc_change_percent = (gc_time_ms - previous_interval_gc) / gc_time_ms;
  double io_change_percent = (io_time_ms - previous_interval_io) / io_time_ms;
  bool overhead_increased = (gc_change_percent < 0 && io_change_percent < 0) ? false : true;
  bool gc_increased = (gc_change_percent > io_change_percent);
//  bool io_negligible = (io_time_ms * 1000.0 < gc_time_ms);
  const size_t unused = g1h->unused_committed_regions_in_bytes();
  const size_t capacity = g1h->capacity();
  const size_t used = capacity - unused;
  bool io_negligible = false;

  if (NegligibleIOFactor > 0.0) {
    io_negligible = (io_time_ms * NegligibleIOFactor < gc_time_ms);
  }

  if (!is_delay_decreased && overhead_increased) {

    if (NegligibleIOFactor > 0.0 && !gc_increased && io_negligible && (gc_time_ms > io_time_ms)) {
      gc_increased = true;
    }  
    if (gc_increased) {
      *cur_state = S_WAIT_GROW;
      *cur_action = (g1h->capacity() >= g1h->max_capacity()) ? WAIT_AFTER_GROW : GROW_HEAP;
      last_action = GROW_HEAP;
    } else {
      *cur_state = (used > (capacity * 0.90)) ? S_WAIT_GROW : S_WAIT_SHRINK;
      *cur_action = (used > (capacity * 0.90)) ? GROW_HEAP : SHRINK_HEAP;
      last_action = *cur_action;
    }
    return;
  } 

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

void G1TeraSimpleWaitStateMachine::analyze_history(states *cur_state_hist, actions *cur_action_hist, double *avg_gc_time, double *avg_io_time) {
  double sum_gc_time = 0.0;
  double sum_io_time = 0.0;

  uintx total_gc_numbers = IntervalHistoryAmount;
  uintx total_io_numbers = IntervalHistoryAmount;

  for (uintx i = 0; i < IntervalHistoryAmount; ++i) {
    #ifdef DYNAMICHEAP_DEBUG
      fprintf(stderr, "gc_history[%ld] = %f\n", i, _gc_history[i]);
      fprintf(stderr, "io_history[%ld] = %f\n", i, _io_history[i]);
    #endif
    if (_gc_history[i] == 0 && _io_history[i] == 0) {
      total_gc_numbers--;
      total_io_numbers--;
    } 
    sum_gc_time += _gc_history[i];
    sum_io_time += _io_history[i];
  }

  *avg_gc_time = sum_gc_time / total_gc_numbers;
  *avg_io_time = sum_io_time / total_io_numbers;

  #ifdef DYNAMICHEAP_DEBUG
    fprintf(stderr, "Historical Analysis (Averages over %u intervals):\n", IntervalHistoryAmount);
    fprintf(stderr, "  Avg GC Time: %.2fms, Avg IO Time: %.2fms\n", *avg_gc_time, *avg_io_time);
  #endif

  if (history_index == (int)IntervalHistoryAmount) {
    history_index = 0;
  }
}

void G1TeraSimpleWaitStateMachine::fsm(states *cur_state, 
				       actions *cur_action, 
				       double gc_time_ms, 
				       double io_time_ms) {
  double avg_gc_time = 0;
  double avg_io_time = 0;

  _gc_history[history_index] = gc_time_ms;
  _io_history[history_index] = io_time_ms;
  history_index = (history_index + 1) % (IntervalHistoryAmount + 1);

  analyze_history(cur_state, cur_action, &avg_gc_time, &avg_io_time);

  switch (*cur_state) {
    case S_WAIT_GROW:
      state_wait_after_grow(cur_state, cur_action, avg_gc_time, avg_io_time);
      break;
    case S_WAIT_SHRINK:
      state_wait_after_shrink(cur_state, cur_action, avg_gc_time, avg_io_time);
      break;
    case S_NO_ACTION:
      state_no_action(cur_state, cur_action, avg_gc_time, avg_io_time);
      break;
    default:
      break;
  } 
}
