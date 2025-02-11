#include "gc/shared/collectedHeap.hpp"
#include "gc/parallel/parallelScavengeHeap.hpp"
#include "gc/teraHeap/teraDynamicResizingPolicy.hpp"
#include "memory/universe.hpp"
#include "gc/teraHeap/teraHeap.hpp"
#include "memory/universe.hpp"

#define BUFFER_SIZE 1024

// const uint64_t TeraDynamicResizingPolicy::CYCLES_PER_SECOND{get_cycles_per_second()};

#define REGULAR_INTERVAL ((10LL * 1000)) 
  
// Intitilize the cpu usage statistics
TeraCPUUsage* TeraDynamicResizingPolicy::init_cpu_usage_stats() {
  return TeraCPUStatsPolicy ? static_cast<TeraCPUUsage*>(new TeraMultiExecutorCPUUsage()) :
                              static_cast<TeraCPUUsage*>(new TeraSimpleCPUUsage());
}

// Initialize the policy of the state machine
TeraStateMachine* TeraDynamicResizingPolicy::init_state_machine_policy() {
  switch (TeraResizingPolicy) {
    case 1:
      return new TeraStateMachineOnlyDelay();
    case 2:
      return new TeraStateMachineOptimalState();
    case 3:
      return new TeraStateMachineDelayAndCosts();
    default:
      break;
  }

  return new TeraStateMachineOnlyDelay();
}

// We call this function after moving objects to H2 to reste the
// state and the counters.
// TODO: move this inside the policy
void TeraDynamicResizingPolicy::epilog_move_h2(bool full_gc_done,
                                               bool need_resizing) {
	// By default we unsert the direct promotion
	Universe::teraHeap()->get_policy()->unset_direct_promotion();
	bool epilog = state_machine->epilog_move_h2(full_gc_done, need_resizing,
			&cur_action, &cur_state);

	if (!epilog)
		return;

	reset_counters();
	actions tmp_action = cur_action;
	cur_action = SHRINK_H1;
	calculate_gc_cost(0, true);
	cur_action = tmp_action;
}

// We use this function to take decision in case of minor GC which
// happens before a major gc.
void TeraDynamicResizingPolicy::dram_repartition(bool *need_full_gc,
                                                 bool *eager_move) {
	double avg_gc_time_ms, avg_io_time_ms;
	uint64_t device_active_time_ms = 0;

	calculate_gc_io_costs(&avg_gc_time_ms, &avg_io_time_ms, &device_active_time_ms);

	state_machine->fsm(&cur_state, &cur_action, avg_gc_time_ms, avg_io_time_ms,
			device_active_time_ms, h2_cand_size_in_bytes, eager_move);
	print_state_action(avg_gc_time_ms, avg_io_time_ms);

	switch (cur_action) {
	case SHRINK_H1:
		action_shrink_h1(need_full_gc);
		break;
	case GROW_H1:
		action_grow_h1(need_full_gc);
		break;
	case MOVE_H2:
		Universe::teraHeap()->get_policy()->set_direct_promotion(0, 0);
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
  prev_action = cur_action;
	reset_counters();
}

// Print states (for debugging and logging purposes)
void TeraDynamicResizingPolicy::print_state_action(double avg_gc_time_ms, double avg_io_time_ms) {
	if (!TeraHeapStatistics)
		return;

  thlog_or_tty->stamp(true);
  thlog_or_tty->print("REAL_GC_TIME_MS = %lf | ", gc_time);
  thlog_or_tty->print("GC_TIME_MS = %lf | ", avg_gc_time_ms);
  thlog_or_tty->print("IO_TIME_MS = %lf | ", avg_io_time_ms);
  thlog_or_tty->print("STATE = %s | ", state_name[cur_state]);
  thlog_or_tty->print("ACTION = %s\n", action_name[cur_action]);
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
void TeraDynamicResizingPolicy::calculate_gc_io_costs(double *avg_gc_time_ms,
                                                      double *avg_io_time_ms,
                                                      uint64_t *device_active_time_ms) {
  double iowait_time_ms = 0;
  uint64_t dev_time_end = 0;
  *device_active_time_ms = 0; 
	// Check if we are inside the window
	if (!is_window_limit_exeed()) {
		*avg_gc_time_ms = 0;
		*avg_io_time_ms = 0;
		return;
	}

  // Calculate the user and iowait time during the window interval
  cpu_usage->read_cpu_usage(STAT_END, MUTATOR_STAT);
  cpu_usage->calculate_iowait_time(interval, &iowait_time_ms, MUTATOR_STAT);

	iowait_time_ms -= gc_iowait_time_ms;
	dev_time_end = get_device_active_time(DEVICE_H2);
	*device_active_time_ms = (dev_time_end - dev_time_start) - gc_dev_time;

	assert(gc_time <= interval, "GC time should be less than the window interval");
	assert(iowait_time_ms <= interval, "GC time should be less than the window interval");
	assert(*device_active_time_ms <= interval, "GC time should be less than the window interval");

	if (iowait_time_ms < 0 || *device_active_time_ms <= 0)
		iowait_time_ms = 0;

	history(gc_time - gc_compaction_phase_ms, iowait_time_ms);

  *avg_io_time_ms = calc_avg_time(hist_iowait_time, HIST_SIZE);
  *avg_gc_time_ms = calc_avg_time(hist_gc_time, GC_HIST_SIZE);

  // if (TeraHeapStatistics)
  //   debug_print(*avg_io_time_ms, *avg_gc_time_ms, interval, iowait_time_ms, gc_time);
}

TeraDynamicResizingPolicy::TeraDynamicResizingPolicy() {
  cpu_usage = init_cpu_usage_stats();

  // window_start_time = get_cycles();
  window_start_time = os::elapsedTime();
  cpu_usage->read_cpu_usage(STAT_START, MUTATOR_STAT);
  gc_iowait_time_ms = 0;
  gc_time = 0;
  dev_time_start = get_device_active_time(DEVICE_H2);
  gc_dev_time = 0;
  cur_action = NO_ACTION;
  prev_action = NO_ACTION;
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

	// double elapsed_time = (double)(end_time - start_time) / CYCLES_PER_SECOND;
  double elapsed_time = end_time - start_time;
	return (elapsed_time * 1000.0);
}

// Set current time since last window
void TeraDynamicResizingPolicy::reset_counters() {
  // window_start_time = get_cycles();
  window_start_time = os::elapsedTime();
  cpu_usage->read_cpu_usage(STAT_START, MUTATOR_STAT);
  gc_time = 0;
  gc_dev_time = 0;
  gc_iowait_time_ms = 0;
  dev_time_start = get_device_active_time(DEVICE_H2);
  transfer_hint_enabled = false;
  gc_compaction_phase_ms = 0;
  window_interval = REGULAR_INTERVAL;
}

// Check if the window limit exceed time
bool TeraDynamicResizingPolicy::is_window_limit_exeed() {
	uint64_t window_end_time;
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

// Init the iowait timer at the begining of the major GC.
void TeraDynamicResizingPolicy::gc_start(double start_time) {
  cpu_usage->read_cpu_usage(STAT_START, GC_STAT);
  gc_dev_start = get_device_active_time(DEVICE_H2);
  last_full_gc_start = start_time;
}

// Count the iowait time during gc and update the gc_iowait_time_ms
// counter for the current window
void TeraDynamicResizingPolicy::gc_end(double gc_duration, double last_full_gc) {
	unsigned long long gc_iowait_end = 0, gc_cpu_end = 0, gc_blk_io_end = 0;
	uint64_t gc_dev_end = 0;
	double iowait_time = 0;
	static double last_gc_end = 0;

  cpu_usage->read_cpu_usage(STAT_END, GC_STAT);
  cpu_usage->calculate_iowait_time(gc_duration, &iowait_time, GC_STAT);
  gc_iowait_time_ms += iowait_time;

	gc_dev_end = get_device_active_time(DEVICE_H2);
	gc_dev_time += (gc_dev_end - gc_dev_start);

	gc_time += gc_duration;

	prev_full_gc_end = last_gc_end;
	last_gc_end = last_full_gc;
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
	calculate_gc_cost(0, true);
	// We grow the heap so, there is no need to perform the gc. We
	// postpone the gc.
	*need_full_gc = false;
}

void TeraDynamicResizingPolicy::action_shrink_h1(bool *need_full_gc) {
  // Check if the heap is full and you cannot reclaim space for page
  // cache. In that case we need to perform a full gc to free space
  // and then reclaim space for the page cache.
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  double old_gen_occupancy = (double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes();
  if (old_gen_occupancy >= 0.9) {
    *need_full_gc = true;
  }

	TeraHeap *th = Universe::teraHeap();
	th->set_shrink_h1();
	ParallelScavengeHeap::old_gen()->resize(10000);
	th->unset_shrink_h1();

	// Recalculate the GC cost
	calculate_gc_cost(0, true);
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
		// Reset the metrics for page cache because now we use the ioslack
		return cur_size + (ideal_page_cache - cur_page_cache);
	}

	new_size = cur_size + (adaptive_resizing_step(true) * state_machine->read_cgroup_mem_stats(true));

	return new_size;
}

// Shrink the capacity of H1 to increase the page cache size.
// This functions is called by the collector
size_t TeraDynamicResizingPolicy::shrink_h1() {
	PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
	size_t cur_size = old_gen->capacity_in_bytes();
	size_t used_size = old_gen->used_in_bytes();
	size_t free_space = cur_size - used_size;
	size_t new_size = used_size + (adaptive_resizing_step(false) * free_space); ;

	set_shrinked_bytes(cur_size - new_size);
	set_current_size_page_cache();

	return new_size;
}

// Decrease the size of H2 candidate objects that are in H1 and
// should be moved to H2. We measure only H2 candidates objects that
// are primitive arrays and leaf objects.
void TeraDynamicResizingPolicy::decrease_h2_candidate_size(size_t size) {
	size_t transfer_bytes = size * HeapWordSize;
	bool check = (h2_cand_size_in_bytes >= transfer_bytes);
	h2_cand_size_in_bytes = check ? (h2_cand_size_in_bytes - transfer_bytes) : 0;

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

	hist_gc_time[index % GC_HIST_SIZE] = calculate_gc_cost(gc_time_ms);
	hist_iowait_time[index % HIST_SIZE] = iowait_time_ms;
	index++;
}

// double TeraDynamicResizingPolicy::calculate_gc_cost(double gc_time_ms, bool recalculate_intervals) {
//   static double last_gc_time_ms = 0;
//   static double gc_ratio_per_interval = 0;
//   PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
//   PSAdaptiveSizePolicy *ergonomics = ParallelScavengeHeap::heap()->size_policy();
//   static int seen_intervals = 0;

//   if (gc_time_ms != 0) {
//     last_gc_time_ms = gc_time_ms;
//     seen_intervals = 1;

//     double cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
//     double intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes();
//     // We use std::ceil to round up because intervals need to be integers and
//     // not less than 1.
//     gc_ratio_per_interval = last_gc_time_ms / static_cast<int>(ceil(intervals_until_next_major_gc));

//     return gc_ratio_per_interval;
//   }

//   if (recalculate_intervals) {
//     double cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
//     double intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes();
//     // We use std::ceil to round up because intervals need to be integers and
//     // not less than 1.
//     gc_ratio_per_interval = last_gc_time_ms / (static_cast<int>(ceil(intervals_until_next_major_gc)) + seen_intervals);

//     return gc_ratio_per_interval;
//   } 

//   seen_intervals++;
//   return gc_ratio_per_interval;
// }

// Second function for calculating gc cost
double TeraDynamicResizingPolicy::calculate_gc_cost(double gc_time_ms, bool recalculate_intervals) {
  static double last_gc_time_ms = 0;
  static double gc_per_interval_ms = 0;
  
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  double cur_live_bytes_ratio = (double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes();
  double gc_cost_last_major_gc = 0;
  double intervals_until_next_major_gc = 0;
  double cur_free_bytes = 0;
  PSAdaptiveSizePolicy *ergonomics = nullptr;

  if (gc_time_ms == 0) {
    cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
    ergonomics = ParallelScavengeHeap::heap()->size_policy();

    intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes();
    gc_per_interval_ms = last_gc_time_ms / static_cast<int>(ceil(intervals_until_next_major_gc));

    return gc_per_interval_ms;
  }
  
  // Calculate the cost of current GC
  last_gc_time_ms = gc_time_ms;
  cur_free_bytes = old_gen->capacity_in_bytes() - old_gen->used_in_bytes();
  ergonomics = ParallelScavengeHeap::heap()->size_policy();

  intervals_until_next_major_gc = cur_free_bytes / ergonomics->average_promoted_in_bytes(); 
  gc_per_interval_ms = last_gc_time_ms / static_cast<int>(ceil(intervals_until_next_major_gc));

  return gc_per_interval_ms;
}

double TeraDynamicResizingPolicy::adaptive_resizing_step(bool should_grow) {
  static double grow_step = 0;
  static double shrink_step = 0;

  if (!AdaptiveResizingStep) {
    return should_grow ? ResizingStep : (1 - ResizingStep);
  }

  if (should_grow) {
    grow_step = (cur_action == prev_action) ? MIN(grow_step + 0.1, 0.8) : ResizingStep; 
    return grow_step;
  }

  shrink_step = (cur_action == prev_action) ? MAX(shrink_step - 0.1, 0.2) : (1 - ResizingStep); 
  return shrink_step;
}
