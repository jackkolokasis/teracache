#include "gc_interface/collectedHeap.hpp"
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#include "gc_implementation/teraHeap/teraDynamicResizingPolicy.hpp"
#include "memory/universe.hpp"
#include "gc_implementation/shared/cycleCounting.hpp"
#include "gc_implementation/teraHeap/teraHeap.hpp"
#include "gc_implementation/teraHeap/teraHeap.inline.hpp"

#define BUFFER_SIZE 1024
const uint64_t TeraDynamicResizingPolicy::CYCLES_PER_SECOND = get_cycles_per_second();

#define REGULAR_INTERVAL ((10LL * 1000)) 
//valid only for sith4 and jdk8. JDK 17 is not dependent on a hard-coded value for the H2 device, because a jvm flag -XX:DEVICE_H2 is used.
#define DEVICE_H2 "nvme1n1"
// Initialize the policy of the state machine
TeraStateMachine* TeraDynamicResizingPolicy::init_state_machine_policy() {
  switch (TeraResizingPolicy) {
    case 1:
      return new TeraSimpleWaitStateMachine();
    case 2:
      return new TeraAggrGrowStateMachine();
    case 3:
      return new TeraAggrShrinkStateMachine();
    case 4:
      return new TeraOptWaitAfterShrinkStateMachine();
    case 5:
      return new TeraShrinkAfterGrowStateMachine();
    case 6:
      return new TeraOptWaitAfterGrowStateMachine();
    case 7:
      return new TeraFullOptimizedStateMachine(); 
    case 8:
      return new TeraDirectGrowShrinkStateMachine(); 
    case 9:
      return new TeraFullDirectGrowShrinkStateMachine(); 
      break;
  }
  
  return new TeraSimpleStateMachine();
}

// We call this function after moving objects to H2 to reste the
// state and the counters.
// TODO: move this inside the policy
void TeraDynamicResizingPolicy::epilog_move_h2(bool full_gc_done,
                                               bool need_resizing) {
  // By default we unsert the direct promotion
  Universe::teraHeap()->unset_direct_promotion();
  bool epilog = state_machine->epilog_move_h2(full_gc_done, need_resizing,
                                              &cur_action, &cur_state);

  if (!epilog) 
    return;

  reset_counters();
  actions tmp_action = cur_action;
  cur_action = SHRINK_H1;
  calculate_gc_cost(0);
  cur_action = tmp_action;
}

// We use this function to take decision in case of minor GC which
// happens before a major gc.
void TeraDynamicResizingPolicy::dram_repartition(bool *need_full_gc,
                                                 bool *eager_move) {
  double avg_gc_time_ms, avg_io_time_ms;
  uint64_t device_active_time_ms = 0;
  TeraHeap *th = Universe::teraHeap();

  bool should_decide = calculate_gc_io_costs(&avg_gc_time_ms, &avg_io_time_ms, &device_active_time_ms);

  state_machine->fsm(&cur_state, &cur_action, avg_gc_time_ms, avg_io_time_ms,
                     device_active_time_ms, h2_cand_size_in_bytes, eager_move);
  print_state_action();

  switch (cur_action) {
    case SHRINK_H1:
      action_shrink_h1();
      break;
    case GROW_H1:
      action_grow_h1(need_full_gc);
      break;
    case MOVE_H2:
      Universe::teraHeap()->set_direct_promotion();
      break;
    case MOVE_BACK:
      action_move_back();
      break;
    case NO_ACTION:
    case IOSLACK:
    case WAIT_AFTER_GROW:
    case CONTINUE:
      break;
  }
  
  if (should_decide)
    reset_counters();
}

// Print states (for debugging and logging purposes)
void TeraDynamicResizingPolicy::print_state_action() {
  if (!TeraHeapStatistics)
    return;

  thlog_or_tty->stamp(true);
  thlog_or_tty->print_cr("STATE = %s\n", state_name[cur_state]);
  thlog_or_tty->print_cr("ACTION = %s\n", action_name[cur_action]);
  thlog_or_tty->flush();
}

// Initialize the array of state names
void TeraDynamicResizingPolicy::init_state_actions_names() {
  strncpy(action_name[0], "NO_ACTION",       10);
  strncpy(action_name[1], "SHRINK_H1",       10);
  strncpy(action_name[2], "GROW_H1",          8);
  strncpy(action_name[3], "MOVE_BACK",       10);
  strncpy(action_name[4], "CONTINUE",         9);
  strncpy(action_name[5], "MOVE_H2",          8);
  strncpy(action_name[6], "IOSLACK",          8);
  strncpy(action_name[7], "WAIT_AFTER_GROW", 16);
  
  strncpy(state_name[0], "S_NO_ACTION",      12);
  strncpy(state_name[1], "S_WAIT_SHRINK",    14);
  strncpy(state_name[2], "S_WAIT_GROW",      12);
  strncpy(state_name[3], "S_WAIT_MOVE",      12);
}

// Calculate the average of gc and io costs and return their values.
// We use these values to determine the next actions.
bool TeraDynamicResizingPolicy::calculate_gc_io_costs(double *avg_gc_time_ms,
                                                      double *avg_io_time_ms,
                                                      uint64_t *device_active_time_ms) {
  double iowait_time_ms = 0;
  uint64_t dev_time_end = 0;
  unsigned long long iowait_end = 0, cpu_end = 0;
  *device_active_time_ms = 0; 

  // Check if we are inside the window
  if (!is_window_limit_exeed()) {
    *avg_gc_time_ms = 0;
    *avg_io_time_ms = 0;
    return false;
  }

  // Calculate the user and iowait time during the window interval
  read_cpu_stats(&iowait_end, &cpu_end);
  calc_iowait_time(iowait_start, iowait_end, cpu_start, cpu_end,
                   interval, &iowait_time_ms);

  iowait_time_ms -= gc_iowait_time_ms;
  dev_time_end = get_device_active_time(/*"nvme1n1"*/DEVICE_H2);
  *device_active_time_ms = (dev_time_end - dev_time_start) - gc_dev_time;

  assert(gc_time <= interval, "GC time should be less than the window interval");
  assert(iowait_time_ms <= interval, "GC time should be less than the window interval");
  assert(*device_active_time_ms <= interval, "GC time should be less than the window interval");

  if (iowait_time_ms < 0 || *device_active_time_ms <= 0)
    iowait_time_ms = 0;

  history(gc_time - gc_compaction_phase_ms, iowait_time_ms);

  *avg_io_time_ms = need_action ? iowait_time_ms : calc_avg_time(hist_iowait_time, HIST_SIZE);
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool is_old_gen_full = (double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes() >= 0.85;
  *avg_io_time_ms = is_old_gen_full ? 0 : *avg_io_time_ms;
  *avg_gc_time_ms = calc_avg_time(hist_gc_time, GC_HIST_SIZE);
  
  if (TeraHeapStatistics) {
    thlog_or_tty->print_cr("Device active time = %lu\n", *device_active_time_ms);
    debug_print(*avg_io_time_ms, *avg_gc_time_ms, interval, iowait_time_ms, gc_time);
  }
  return true;
}

TeraDynamicResizingPolicy::TeraDynamicResizingPolicy() {
  window_start_time = get_cycles();//rdtsc();
  read_cpu_stats(&iowait_start, &cpu_start);
  gc_iowait_time_ms = 0;
  gc_time = 0;
  dev_time_start = get_device_active_time(/*"nvme1n1"*/DEVICE_H2);
  gc_dev_time = 0;
  cur_action = NO_ACTION;
  cur_state = S_NO_ACTION;

  memset(hist_gc_time, 0, GC_HIST_SIZE * sizeof(double));
  memset(hist_iowait_time, 0, HIST_SIZE * sizeof(double));

  window_interval = REGULAR_INTERVAL;
  transfer_hint_enabled = false;
  prev_full_gc_end = 0;
  last_full_gc_start = 0;
  gc_compaction_phase_ms = 0;

  init_state_actions_names();
  state_machine = init_state_machine_policy();
}
  
// Calculate ellapsed time
double TeraDynamicResizingPolicy::ellapsed_time(uint64_t start_time,
                                                uint64_t end_time) {

  double elapsed_time = (double)(end_time - start_time) / CYCLES_PER_SECOND;
  return (elapsed_time * 1000.0);
}

// Set current time since last window
void TeraDynamicResizingPolicy::reset_counters() {
  window_start_time = get_cycles();//rdtsc();
  read_cpu_stats(&iowait_start, &cpu_start);
  gc_time = 0;
  gc_dev_time = 0;
  gc_iowait_time_ms = 0;
  dev_time_start = get_device_active_time(/*"nvme1n1"*/DEVICE_H2);
  transfer_hint_enabled = false;
  gc_compaction_phase_ms = 0;
  window_interval = REGULAR_INTERVAL;
}

// Check if the window limit exceed time
bool TeraDynamicResizingPolicy::is_window_limit_exeed() {
  uint64_t window_end_time;
  static int i = 0;
  window_end_time = get_cycles();//rdtsc();
  interval = ellapsed_time(window_start_time, window_end_time);

#ifdef PER_MINOR_GC
  return true;
#else
  return (interval >= window_interval) ? true : false;
#endif // PER_MINOR_GC
}

// Init the iowait timer at the begining of the major GC.
void TeraDynamicResizingPolicy::gc_start(double start_time) {
  read_cpu_stats(&gc_iowait_start, &gc_cpu_start);
  gc_dev_start = get_device_active_time(/*"nvme1n1"*/DEVICE_H2);
  last_full_gc_start = start_time;
}

// Count the iowait time during gc and update the gc_iowait_time_ms
// counter for the current window
void TeraDynamicResizingPolicy::gc_end(double gc_duration, double last_full_gc) {
  unsigned long long gc_iowait_end = 0, gc_cpu_end = 0, gc_blk_io_end = 0;
  uint64_t gc_dev_end = 0;
  double iowait_time = 0;
  static double last_gc_end = 0;

  read_cpu_stats(&gc_iowait_end, &gc_cpu_end);
  calc_iowait_time(gc_iowait_start, gc_iowait_end, gc_cpu_start, gc_cpu_end,
                   gc_duration, &iowait_time);
  gc_iowait_time_ms += iowait_time;

  gc_dev_end = get_device_active_time(/*"nvme1n1"*/DEVICE_H2);
  gc_dev_time += (gc_dev_end - gc_dev_start);

  gc_time += gc_duration;

  prev_full_gc_end = last_gc_end;
  last_gc_end = last_full_gc;
}

// This function opens iostat and read the io wait time in percentage
void TeraDynamicResizingPolicy::read_cpu_stats(unsigned long long* cpu_iowait,
                                               unsigned long long* total_cpu) {
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

      *total_cpu = user + nice + system + iowait + irq + softirq + idle;
      *cpu_iowait = iowait + idle;

      break;
    }
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
void TeraDynamicResizingPolicy::calc_iowait_time(unsigned long long cpu_iowait_before,
                                                 unsigned long long cpu_iowait_after,
                                                 unsigned long long total_cpu_before,
                                                 unsigned long long total_cpu_after,
                                                 double duration, double* iowait_time) {

  unsigned long long iowait_diff = cpu_iowait_after - cpu_iowait_before;
  unsigned long long cpu_diff = total_cpu_after - total_cpu_before;

  if (cpu_diff == 0) {
    *iowait_time = 0;
    fprintf(stderr, "iowait_time = 0\n");
  }
  else
    *iowait_time = ((double) iowait_diff / cpu_diff) * duration;
}

uint64_t TeraDynamicResizingPolicy::get_device_active_time(const char* device) {
  char file_path[256];
  snprintf(file_path, sizeof(file_path), "/sys/block/%s/stat", device);
  FILE* dev_file = fopen(file_path, "r");

  if (!dev_file) {
    printf("Failed to open device statistics file.\n");
    return 0;
  }

  char line[BUFFER_SIZE];
  int res;
  
  while (fgets(line, sizeof(line), dev_file) != NULL) {
    uint64_t readIOs, readMerges, readSectors, readTicks, writeIOs, writeMerges, writeSectors, writeTicks;
    res = sscanf(line, "%lu %lu %lu %lu %lu %lu %lu %lu", &readIOs, &readMerges, &readSectors, &readTicks, &writeIOs, &writeMerges, &writeSectors, &writeTicks);

    if (res != 8) {
      fprintf (stderr, "Error reading device usage\n");
      return 0;
    }
  
    res = fclose(dev_file);
    if (res != 0) {
      fprintf(stderr, "Error closing file");
      return 0;
    }

    return readTicks + writeTicks;
  }

  return 0;
}

void TeraDynamicResizingPolicy::action_grow_h1(bool *need_full_gc) {
  TeraHeap *th = Universe::teraHeap();

  th->set_grow_h1();
  ParallelScavengeHeap::old_gen()->resize(10000);
  th->unset_grow_h1();

  // Recalculate the GC cost
  calculate_gc_cost(0);
  // We grow the heap so, there is no need to perform the gc. We
  // postpone the gc.
  *need_full_gc = false;
}

void TeraDynamicResizingPolicy::action_shrink_h1() {
  TeraHeap *th = Universe::teraHeap();

  th->set_shrink_h1();
  ParallelScavengeHeap::old_gen()->resize(10000);
  th->unset_shrink_h1();

  // Recalculate the GC cost
  calculate_gc_cost(0);
}

void TeraDynamicResizingPolicy::action_move_back() {
  return;
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

// Grow the capacity of H1.
size_t TeraDynamicResizingPolicy::grow_h1() {
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t new_size = 0;
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t ideal_page_cache  = prev_page_cache_size + shrinked_bytes;
  size_t cur_page_cache = state_machine->read_cgroup_mem_stats(true);

  bool ioslack = (cur_page_cache < ideal_page_cache); 
  shrinked_bytes = 0;
  prev_page_cache_size = 0;

  if (ioslack) {
    if (TeraHeapStatistics) {
      thlog_or_tty->print_cr("Grow_by = %lu\n", (ideal_page_cache - cur_page_cache));
      thlog_or_tty->flush();
    }

    // Reset the metrics for page cache because now we use the ioslack
    return cur_size + (ideal_page_cache - cur_page_cache); 
  }

  new_size = cur_size + (GROW_STEP * state_machine->read_cgroup_mem_stats(true));

  if (TeraHeapStatistics) {
    thlog_or_tty->print_cr("[GROW_H1] Before = %lu | After = %lu | PageCache = %lu\n",
                           cur_size, new_size, state_machine->read_cgroup_mem_stats(true));
    thlog_or_tty->flush();
  }

  return new_size;
}

// Shrink the capacity of H1 to increase the page cache size.
// This functions is called by the collector
size_t TeraDynamicResizingPolicy::shrink_h1() {
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  size_t free_space = cur_size - used_size;
  size_t new_size = used_size + (SHRINK_STEP * free_space); ;

  set_shrinked_bytes(cur_size - new_size);
  set_current_size_page_cache();

  if (TeraHeapStatistics) {
    thlog_or_tty->print_cr("[SHRINK_H1] Before = %lu | After = %lu | PageCache = %lu\n",
                           cur_size, new_size, state_machine->read_cgroup_mem_stats(true));
    thlog_or_tty->flush();
  }

  return new_size;
}
  
// Decrease the size of H2 candidate objects that are in H1 and
// should be moved to H2. We measure only H2 candidates objects that
// are primitive arrays and leaf objects.
void TeraDynamicResizingPolicy::decrease_h2_candidate_size(size_t size) {
  h2_cand_size_in_bytes -= size * HeapWordSize;
  if (h2_cand_size_in_bytes < 0)
    h2_cand_size_in_bytes = 0;

#ifdef LAZY_MOVE_H2
  transfer_hint_enabled = true;
#else
  transfer_hint_enabled = Universe::teraHeap()->is_direct_promote() ? false : true;
#endif // LAZY_MOVE_H2
}

// Save the history of the GC and iowait overheads. We maintain two
// ring buffers (one for GC and one for iowait) and update these
// buffers with the new values for GC cost and IO overhead.
void TeraDynamicResizingPolicy::history(double gc_time_ms, double iowait_time_ms) {
  static int index = 0;
  double gc_ratio = calculate_gc_cost(gc_time_ms);

  hist_gc_time[index % GC_HIST_SIZE] = gc_ratio * interval;
  hist_iowait_time[index % HIST_SIZE] = iowait_time_ms;
  index++;
}

// Calculation of the GC cost prediction.
double TeraDynamicResizingPolicy::calculate_gc_cost(double gc_time_ms) {
  static double last_gc_time_ms  = 0;
  static double last_gc_free_bytes_ratio = 1;
  static double gc_percentage_ratio = 0;

  // Interval between the previous and the current gc
  double gc_interval_ms = (last_full_gc_start - prev_full_gc_end) * 1000;

  // Non major GC cycles happens.
  if (gc_time_ms == 0 && gc_interval_ms == 0)
    return 0;

  bool is_no_action = !(cur_action == SHRINK_H1 || cur_action == GROW_H1);

  if (gc_time_ms == 0 && is_no_action) {
    return gc_percentage_ratio;
  }

  // Free bytes
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  double cur_live_bytes_ratio = (double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes();
  double cur_free_bytes_ratio = (1.0 - cur_live_bytes_ratio);
  double cost = 0;

  if (gc_time_ms == 0) {
    cost = last_gc_time_ms * (1.0 - last_gc_free_bytes_ratio);
    gc_percentage_ratio = (cost * last_gc_free_bytes_ratio) / (cur_free_bytes_ratio * gc_interval_ms);

    if (TeraHeapStatistics) {
      thlog_or_tty->print_cr("gc_percentage = %lf\n", gc_percentage_ratio);
      thlog_or_tty->print_cr("last_gc_time_ms = %f\n", last_gc_time_ms);
      thlog_or_tty->print_cr("1.0 - cur_free = %f\n", (1.0 - cur_free_bytes_ratio));
      thlog_or_tty->print_cr("gc_interval_ms = %f", gc_interval_ms);
    }

    return gc_percentage_ratio;
  }

  // Calculate the cost of current GC
  last_gc_time_ms = gc_time_ms;
  last_gc_free_bytes_ratio = cur_free_bytes_ratio;
  cost = last_gc_time_ms * (1.0 - cur_free_bytes_ratio);
  gc_percentage_ratio = (cost * last_gc_free_bytes_ratio) / (cur_free_bytes_ratio * gc_interval_ms);

  if (TeraHeapStatistics) {
    thlog_or_tty->print_cr("gc_percentage = %lf\n", gc_percentage_ratio);
  }

  return gc_percentage_ratio;
}
  
bool TeraDynamicResizingPolicy::is_action_move_h2() {
  return cur_action == MOVE_H2;
}
