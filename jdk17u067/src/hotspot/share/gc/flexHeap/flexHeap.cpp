#include "gc/shared/collectedHeap.hpp"
// #include "gc/shared/cycleCounting.hpp"
#include "gc/parallel/parallelScavengeHeap.hpp"
#include "gc/flexHeap/flexHeap.hpp"
#include "memory/universe.hpp"

#define BUFFER_SIZE 1024
#define REGULAR_INTERVAL ((2LL * 1000)) 
#define GROWTH_FACTOR 1.2
#define SHRINK_FACTOR 0.8
// const uint64_t FlexHeap::CYCLES_PER_SECOND{get_cycles_per_second()};

// Intitilize the cpu usage statistics
FlexCPUUsage* FlexHeap::init_cpu_usage_stats() {
  return FlexCPUStatsPolicy ? static_cast<FlexCPUUsage*>(new FlexMultiTenantCPUUsage()) :
                              static_cast<FlexCPUUsage*>(new FlexSimpleCPUUsage());
}

// Initialize the policy of the state machine
FlexStateMachine* FlexHeap::init_state_machine_policy() {
  switch (FlexResizingPolicy) {
    case 1:
      // to be deleted
      return new FlexSimpleWaitStateMachine();
    case 2:
      // to be deleted
      return new FlexFullOptimizedStateMachine(); 
    case 3:
      // Use it for evaluatio
      return new FlexStateMachineWithOptimalState();
    case 4:
      // to be deleted
      return new FlexSimpleStateMachineOnlyDelay();
    case 5:
      // Keep only five and six and the rest remove them
      // Use it for evaluation
      return new FlexSimpleWaitStateMachineOnlyDelay();
    case 6:
      // Use it for evaluatio
      return new FlexSimpleWaitStateMachineOnlyDelayWithOptimalState();
    default:
      break;
  }
  
  return new FlexSimpleStateMachine();
}

// We use this function to take decision in case of minor GC which
// happens before a major gc.
void FlexHeap::dram_repartition(bool *need_full_gc) {
  double avg_gc_time_ms, avg_io_time_ms;

  calculate_gc_io_costs(&avg_gc_time_ms, &avg_io_time_ms);

  state_machine->fsm(&cur_state, &cur_action, avg_gc_time_ms, avg_io_time_ms);
  print_state_action(avg_gc_time_ms, avg_io_time_ms);

  switch (cur_action) {
    case FH_SHRINK_HEAP:
      action_shrink_heap(need_full_gc);
      break;
    case FH_GROW_HEAP:
      action_grow_heap(need_full_gc);
      break;
    case FH_NO_ACTION:
    case FH_IOSLACK:
    case FH_WAIT_AFTER_GROW:
    case FH_CONTINUE:
      break;
  }
  prev_action = cur_action;
  reset_counters();
}

// Print states (for debugging and logging purposes)
void FlexHeap::print_state_action(double avg_gc_time_ms, double avg_io_time_ms) {
  // if (!FlexHeapStatistics)
  //   return;

  tty->stamp(true);
  tty->print("REAL_GC_TIME_MS = %lf | ", gc_time);
  tty->print("GC_TIME_MS = %lf | ", avg_gc_time_ms);
  tty->print("IO_TIME_MS = %lf | ", avg_io_time_ms);
  // tty->print("GC_TIME_ACCUM_MS = %lf | ", gc_time_accum_ms);
  tty->print("STATE = %s | ", state_name[cur_state]);
  tty->print("ACTION = %s\n", action_name[cur_action]);
  tty->flush();
}

// Initialize the array of state names
void FlexHeap::init_state_actions_names() {
  strncpy(action_name[0], "NO_ACTION",       10);
  strncpy(action_name[1], "SHRINK_HEAP",     12);
  strncpy(action_name[2], "GROW_HEAP",       10);
  strncpy(action_name[3], "CONTINUE",         9);
  strncpy(action_name[4], "IOSLACK",          8);
  strncpy(action_name[5], "WAIT_AFTER_GROW", 16);
  
  strncpy(state_name[0], "S_NO_ACTION",      12);
  strncpy(state_name[1], "S_WAIT_SHRINK",    14);
  strncpy(state_name[2], "S_WAIT_GROW",      12);
  strncpy(state_name[3], "S_STABLE",         9);
}

// Calculate the average of gc and io costs and return their values.
// We use these values to determine the next actions.
void FlexHeap::calculate_gc_io_costs(double *avg_gc_time_ms,
                                     double *avg_io_time_ms) {
  double iowait_time_ms = 0;
  uint64_t dev_time_end = 0;

  // Check if we are inside the window
  if (!is_window_limit_exeed()) {
    *avg_gc_time_ms = 0;
    *avg_io_time_ms = 0;
    return;
  }

  // Calculate the user and iowait time during the window interval
  cpu_usage->read_cpu_usage(STAT_END);
  cpu_usage->calculate_iowait_time(interval, &iowait_time_ms);

  assert(gc_time <= interval, "GC time should be less than the window interval");
  assert(iowait_time_ms <= interval, "GC time should be less than the window interval");

  if (iowait_time_ms < 0)
    iowait_time_ms = 0;

  history(gc_time, iowait_time_ms);

  *avg_io_time_ms = calc_avg_time(hist_iowait_time, FH_HIST_SIZE);
  *avg_gc_time_ms = calc_avg_time(hist_gc_time, FH_GC_HIST_SIZE);

  if (FlexHeapStatistics)
    debug_print(*avg_io_time_ms, *avg_gc_time_ms, interval, iowait_time_ms, gc_time);
}

FlexHeap::FlexHeap() {
  cpu_usage = init_cpu_usage_stats();

  // window_start_time = get_cycles();
  window_start_time = os::elapsedTime();
  cpu_usage->read_cpu_usage(STAT_START);
  gc_time = 0;
  cur_action = FH_NO_ACTION;
  prev_action = FH_NO_ACTION;
  cur_state = FHS_NO_ACTION;

  memset(hist_gc_time, 0, FH_GC_HIST_SIZE * sizeof(double));
  memset(hist_iowait_time, 0, FH_HIST_SIZE * sizeof(double));

  window_interval = REGULAR_INTERVAL;
  prev_full_gc_end = 0;
  last_full_gc_start = 0;

  init_state_actions_names();
  state_machine = init_state_machine_policy();

  num_major_gc = 0;
}
  
// Calculate ellapsed time
double FlexHeap::ellapsed_time(double start_time,
                               double end_time) {

  // double elapsed_time = (double)(end_time - start_time) / CYCLES_PER_SECOND;
  double elapsed_time = end_time - start_time;
  return (elapsed_time * 1000.0);
}

// Set current time since last window
void FlexHeap::reset_counters() {
  // window_start_time = get_cycles();
  window_start_time = os::elapsedTime();
  cpu_usage->read_cpu_usage(STAT_START);
  gc_time = 0;
  window_interval = REGULAR_INTERVAL;
}

// Check if the window limit exceed time
bool FlexHeap::is_window_limit_exeed() {
  double window_end_time;
  static int i = 0;
  // window_end_time = get_cycles();
  window_end_time = os::elapsedTime();
  interval = ellapsed_time(window_start_time, window_end_time);

#ifdef PER_MINOR_GC
  return true;
#else
  return (interval >= window_interval) ? true : false;
#endif // PER_MINOR_GC
}

void FlexHeap::gc_start(double start_time) {
  last_full_gc_start = start_time;
  num_major_gc ++;
}

// Count the iowait time during gc and update the gc_iowait_time_ms
// counter for the current window
void FlexHeap::gc_end(double gc_duration, double last_full_gc) {
  static double last_gc_end = 0;
  gc_time += gc_duration;
  prev_full_gc_end = last_gc_end;
  last_gc_end = last_full_gc;
}

void FlexHeap::action_grow_heap(bool *need_full_gc) {
  should_grow_heap = true;
  ParallelScavengeHeap::old_gen()->resize(10000);
  should_grow_heap = false;

  // Recalculate the GC cost
  calculate_gc_cost(0, true);
  // calculate_gc_cost_accumulated(0, true);
  // We grow the heap so, there is no need to perform the gc. We
  // postpone the gc.
  *need_full_gc = false;
}

void FlexHeap::action_shrink_heap(bool *need_full_gc) {

  // Check if the heap is full and you cannot reclaim space for page
  // cache. In that case we need to perform a full gc to free space
  // and then reclaim space for the page cache.
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  double old_gen_occupancy = (double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes();
  if (old_gen_occupancy >= 0.88) {
    *need_full_gc = true;
  }
  
  should_shrink_heap = true;
  old_gen->resize(10000);
  should_shrink_heap = false;

  // Recalculate the GC cost
  calculate_gc_cost(0, true);
  // calculate_gc_cost_accumulated(0, true);
}

// Print counters for debugging purposes
void FlexHeap::debug_print(double avg_iowait_time, double avg_gc_time,
                                            double interval, double cur_iowait_time, double cur_gc_time) {
  tty->print_cr("avg_iowait_time_ms = %lf | ", avg_iowait_time);
  tty->print_cr("avg_gc_time_ms = %lf | ", avg_gc_time);
  tty->print_cr("interval = %lf\n", interval);
  tty->flush();
}

// Find the average of the array elements
double FlexHeap::calc_avg_time(double *arr, int size) {
  double sum = 0;

  for (int i = 0; i < size; i++) {
    sum += arr[i];
  }

  return (double) sum / size;
}

// Grow the capacity of H1.
size_t FlexHeap::grow_heap() {
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t new_size = 0;
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t ideal_page_cache  = prev_page_cache_size + shrinked_bytes;
  size_t cur_page_cache = state_machine->read_cgroup_mem_stats(true);

  // bool ioslack = (cur_page_cache < ideal_page_cache); 
  bool ioslack = false;
  shrinked_bytes = 0;
  prev_page_cache_size = 0;

  if (ioslack) {
    if (FlexHeapStatistics) {
      tty->print_cr("Grow_by = %lu\n", (ideal_page_cache - cur_page_cache));
      tty->flush();
    }

    // Reset the metrics for page cache because now we use the ioslack
    return cur_size + (ideal_page_cache - cur_page_cache); 
  }

  // new_size = cur_size + (GROW_STEP * state_machine->read_cgroup_mem_stats(true));
  new_size = cur_size + (adaptive_resizing_step(true) * state_machine->read_cgroup_mem_stats(true));

  if (FlexHeapStatistics) {
    tty->print_cr("[GROW_H1] Before = %lu | After = %lu | PageCache = %lu\n",
                           cur_size, new_size, state_machine->read_cgroup_mem_stats(true));
    tty->flush();
  }

  return new_size;
}

// Shrink the capacity of H1 to increase the page cache size.
// This functions is called by the collector
size_t FlexHeap::shrink_heap() {
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  size_t free_space = cur_size - used_size;
  size_t new_size = used_size + (adaptive_resizing_step(false) * free_space); ;

  set_shrinked_bytes(cur_size - new_size);
  set_current_size_page_cache();

  if (FlexHeapStatistics) {
    tty->print_cr("[SHRINK_H1] Before = %lu | After = %lu | PageCache = %lu\n",
                           cur_size, new_size, state_machine->read_cgroup_mem_stats(true));
    tty->flush();
  }

  return new_size;
}
  
// Save the history of the GC and iowait overheads. We maintain two
// ring buffers (one for GC and one for iowait) and update these
// buffers with the new values for GC cost and IO overhead.
void FlexHeap::history(double gc_time_ms, double iowait_time_ms) {
  static int index = 0;
  double gc_cost_ms = calculate_gc_cost(gc_time_ms);
  // gc_time_accum_ms = calculate_gc_cost_accumulated(gc_time_ms);

  // hist_gc_time[index % FH_GC_HIST_SIZE] = gc_ratio * interval;
  hist_gc_time[index % FH_GC_HIST_SIZE] = gc_cost_ms;
  hist_iowait_time[index % FH_HIST_SIZE] = iowait_time_ms;
  index++;
}

// This function estimates the GC cost by calculating the ratio between the last
// major GC time and the projected time to fill the old generation.
//
// Steps involved in the calculation:
// 1. When no recent GC time is available (`gc_time_ms == 0`), it uses the
// previous GC's duration.
// 2. The current free space in the old generation is used alongside the average
// promotion rate from the young generation to determine the expected time
// (`time_to_fill_old_gen`) until the next major GC.
// 3. The GC cost percentage (`gc_percentage_ratio`) is then calculated by
// dividing the GC time by `time_to_fill_old_gen`.
//
// This is first correct function that I have up until now
double FlexHeap::calculate_gc_cost(double gc_time_ms, bool recalculate_intervals) {
  static double last_gc_time_ms = 0;
  static double gc_ratio_per_interval = 0;
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  PSAdaptiveSizePolicy *ergonomics = ParallelScavengeHeap::heap()->size_policy();
  static int seen_intervals = 0;

  if (gc_time_ms != 0) {
    last_gc_time_ms = gc_time_ms;
    seen_intervals = 1;

    double cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
    double intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes();
    // We use std::ceil to round up because intervals need to be integers and
    // not less than 1.
    gc_ratio_per_interval = last_gc_time_ms / static_cast<int>(ceil(intervals_until_next_major_gc));

    return gc_ratio_per_interval;
  }

  if (recalculate_intervals) {
    double cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
    double intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes();
    // We use std::ceil to round up because intervals need to be integers and
    // not less than 1.
    gc_ratio_per_interval = last_gc_time_ms / (static_cast<int>(ceil(intervals_until_next_major_gc)) + seen_intervals);

    return gc_ratio_per_interval;
  } 

  seen_intervals++;
  return gc_ratio_per_interval;
}

// Second function for calculating gc cost
// double FlexHeap::calculate_gc_cost(double gc_time_ms, bool recalculate_intervals) {
//   static double last_gc_time_ms = 0;
//   static double gc_per_interval_ms = 0;
//   
//   PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
//   double cur_live_bytes_ratio = (double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes();
//   double gc_cost_last_major_gc = 0;
//   double intervals_until_next_major_gc = 0;
//   double cur_free_bytes = 0;
//   PSAdaptiveSizePolicy *ergonomics = nullptr;

//   if (gc_time_ms == 0) {
//     cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
//     ergonomics = ParallelScavengeHeap::heap()->size_policy();

//     intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes();
//     gc_per_interval_ms = last_gc_time_ms / static_cast<int>(ceil(intervals_until_next_major_gc));

//     return gc_per_interval_ms;
//   }
//   
//   // Calculate the cost of current GC
//   last_gc_time_ms = gc_time_ms;
//   cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
//   ergonomics = ParallelScavengeHeap::heap()->size_policy();

//   intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes(); 
//   gc_per_interval_ms = last_gc_time_ms / static_cast<int>(ceil(intervals_until_next_major_gc));

//   return gc_per_interval_ms;
// }

double FlexHeap::adaptive_resizing_step(bool should_grow) {
  static double grow_step = ResizingStep;
  static double shrink_step = (1 - ResizingStep);

  if (!AdaptiveResizingStep) {
    return should_grow ? ResizingStep : (1 - ResizingStep);
  }

  if (should_grow) {
    grow_step = (cur_action == prev_action) ? MIN(grow_step + 0.1, 0.8) : ResizingStep; 
    return (prev_action == FH_SHRINK_HEAP) ? (1 - shrink_step) : grow_step;
  }

  shrink_step = (cur_action == prev_action) ? MAX(shrink_step - 0.1, 0.2) : (1 - ResizingStep); 
  return (prev_action == FH_GROW_HEAP) ? (1 - grow_step) : shrink_step;
}

// double FlexHeap::adaptive_resizing_step(bool should_grow) {
//   static double grow_step = ResizingStep;
//   static double shrink_step = (1 - ResizingStep);

//   if (!AdaptiveResizingStep) {
//     return should_grow ? ResizingStep : (1 - ResizingStep);
//   }

//   if (should_grow) {
//     grow_step = (cur_action == prev_action) ? MIN(grow_step + 0.1, 0.8) : ResizingStep; 
//     return grow_step;
//   }

//   shrink_step = (cur_action == prev_action) ? MAX(shrink_step - 0.1, 0.2) : (1 - ResizingStep); 
//   return shrink_step;
// }
