#ifndef SHARE_GC_FLEXHEAP_FLEXHEAP_HPP
#define SHARE_GC_FLEXHEAP_FLEXHEAP_HPP

#include "gc/flexHeap/flexEnum.h"
#include "gc/flexHeap/flexCPUUsage.hpp"
#include "gc/flexHeap/flexStateMachine.hpp"
#include "memory/allocation.hpp"
#include "memory/sharedDefines.h"
#include <stdlib.h>
#include <string.h>

#define FH_HIST_SIZE 1
#define FH_GC_HIST_SIZE 1
#define FH_NUM_ACTIONS 6
#define FH_NUM_STATES 4
#define FH_NAME_LEN 20

class FlexHeap : public CHeapObj<mtInternal> {
private:
  static const uint64_t CYCLES_PER_SECOND;
  char state_name[FH_NUM_STATES][FH_NAME_LEN]; //< Define state names
  char action_name[FH_NUM_ACTIONS][FH_NAME_LEN]; //< Define state names
  double window_start_time;         //< Window start time

  double gc_time;                     //< Total gc time for the
                                      // interval of the window
  double interval;                    //< Interval of the window

  fh_actions cur_action;              //< Current action
  
  fh_actions prev_action;             //< Previous action

  fh_states cur_state;                //< Current state

  double prev_full_gc_end;            //< Track when the previous full
                                      // gc has been ended
  double last_full_gc_start;          //< Track when the last gc

  double last_minor_gc;               //< Time happened the last minor
                                      // gc

  bool need_action;                   //< Flag that indicate if we
                                      // need to perform an action.
                                      // This flag is set by mutator
                                      // threads.
  double hist_gc_time[FH_GC_HIST_SIZE];  //< History of the gc time in
                                      // previous intervals
  double hist_iowait_time[FH_HIST_SIZE]; //< History of the iowait time in
                                      // previous intervals

  size_t shrinked_bytes;              //< Number of hrinked bytes
  
  size_t prev_page_cache_size;        //< Current size of page cache
                                      // before shrink operation
  
  uint64_t window_interval;           //< Window_interval;
  
  FlexStateMachine *state_machine;    //< FSM
  FlexCPUUsage *cpu_usage;            // Cpu utilization for
                                      // estimating iowait
  bool should_grow_heap;              //< Indicate grow heap action
  bool should_shrink_heap;            //< Indicate shrink action

  int num_major_gc;                   //< Counting the number of major GC cycles
  
  // double gc_time_accum_ms = 0;
  
  // Check if the window limit exceed time
  bool is_window_limit_exeed();

  // Calculate ellapsed time
  double ellapsed_time(double start_time, double end_time);

  // Find the average of the array elements
  double calc_avg_time(double *arr, int size);

  // After each growing operation of H1 we wait to see the effect of
  // the action. If we reach a gc or the io cost is higher than gc
  // cost then we go to no action state. 
  void state_wait_after_grow(double io_time_ms, double gc_time_ms);

  // Initialize the array of state names
  void init_state_actions_names();

  // Intitilize the policy of the state machine.
  FlexStateMachine* init_state_machine_policy();
  
  // Intitilize the cpu usage statistics
  FlexCPUUsage* init_cpu_usage_stats();
  
  // Calculation of the GC cost prediction.
  double calculate_gc_cost(double gc_time_ms, bool recalculate_intervals=false);
  
  // double calculate_gc_cost_accumulated(double gc_time_ms, bool recalculate_intervals=false);

  // Print states (for debugging and logging purposes)
  void print_state_action(double avg_gc_time_ms, double avg_io_time_ms);

  // GrowH1 action
  void action_grow_heap(bool *need_full_gc);
  
  // ShrinkH1 action
  void action_shrink_heap(bool *need_full_gc);

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

  double adaptive_resizing_step(bool should_grow);

public:
  // Constructor
  FlexHeap();

  // Destructor
  ~FlexHeap() {}

  // Init the iowait timer at the begining of the major GC.
  void gc_start(double start_time);
  
  // Count the iowait time during gc and update the gc_iowait_time_ms
  // counter for the current window
  void gc_end(double gctime, double last_full_gc);

  // We set the time when the last minor gc happened. For cases that
  // IO is high (the whole computation happens in H2) and low number
  // of objects are created in the young generation, we have to check
  // through the VMThread if we need to perfom an action. 
  void set_last_minor_gc(double time) {
    last_minor_gc = time;
  }
  
  // Get when the last minor GC happened
  double get_last_minor_gc(void) {
    return last_minor_gc;
  }

  // VMThread enables DynaHeap to perform action. For cases that
  // IO is high (the whole computation happens in H2) and low number
  // of objects are created in the young generation.
  void action_enabled(void) {
    need_action = true; 
  }

  // Reset the action enabled by VMThread.
  void action_disabled(void) {
    need_action = false; 
  }

  // Check if the VMThread enable an action.
  bool is_action_enabled() {
    return need_action;
  }
  
  // Set the number of shrinked bytes when a SHRINK_H1 operation
  // occured.
  void set_shrinked_bytes(size_t bytes) {
    shrinked_bytes = bytes;
  }

  // Before as shrinking operation we save the size of the page cache.
  void set_current_size_page_cache() {
    prev_page_cache_size = state_machine->read_cgroup_mem_stats(true);
  }

  // Get the window interval time.
  uint64_t get_window_interval() {
    // Transform milliseconds to seconds
    return window_interval / 1000;
  }

  // Grow the capacity of H1.
  size_t grow_heap();

  // Shrink the capacity of H1 to increase the page cache size.
  size_t shrink_heap();

  // We call this function after moving objects to H2 to reste the
  // state and the counters.
  void epilog_move_h2(bool full_gc_done, bool need_resizing);

  void dram_repartition(bool *need_full_gc);
  
  // Check if the old generation should grow
  bool need_to_grow_heap() {
    return should_grow_heap;
  };

  // Check if the old generation should shrink
  bool need_to_shrink_heap() {
    return should_shrink_heap;
  }
};

#endif // SHARE_GC_FLEXHEAP_FLEXHEAP_HPP
