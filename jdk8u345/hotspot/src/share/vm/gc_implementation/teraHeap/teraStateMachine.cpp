#include "gc_implementation/teraHeap/teraStateMachine.hpp"

#define BUFFER_SIZE 1024
#define EPSILON 100
#define TRANSFER_THRESHOLD 0.4f

void TeraStateMachine::state_no_action(states *cur_state, actions *cur_action,
                                       double gc_time_ms, double io_time_ms,
                                       uint64_t device_active_time_ms,
                                       size_t h2_cand_size_in_bytes) {

  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_NO_ACTION;
    *cur_action = NO_ACTION;
    return;
  }

  // We calculate the ratio of the h2 candidate objects in H1 
  double h2_candidate_ratio = (double) h2_cand_size_in_bytes / Universe::heap()->capacity();
  if (gc_time_ms >= io_time_ms &&  h2_candidate_ratio >= TRANSFER_THRESHOLD) {
    *cur_state = S_WAIT_MOVE;
    *cur_action = MOVE_H2;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
  if (gc_time_ms >= io_time_ms && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1;
    return;
  }

  bool is_old_gen_full = ((double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes()) >= 0.75;
  if (io_time_ms > gc_time_ms && device_active_time_ms > 0 && !is_old_gen_full) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

// Read the memory statistics for the cgroup
size_t TeraStateMachine::read_cgroup_mem_stats(bool read_page_cache) {
  // Define the path to the memory.stat file
  const char* file_path = "/sys/fs/cgroup/memory/memlim/memory.stat";

  // Open the file for reading
  FILE* file = fopen(file_path, "r");

  if (file == NULL) {
    fprintf(stderr, "Failed to open memory.stat\n");
    return 0;
  }

  char line[BUFFER_SIZE];
  size_t res = 0;

  // Read each line in the file
  while (fgets(line, sizeof(line), file)) {
    if (read_page_cache && strncmp(line, "cache", 5) == 0) {
      // Extract the cache value
      res = atoll(line + 6);
      break;
    }

    if (strncmp(line, "rss", 3) == 0) {
      // Extract the rss value
      res = atoll(line + 4);
      break;
    }
  }

  // Close the file
  fclose(file);
  return res;
}

void TeraSimpleStateMachine::fsm(states *cur_state, actions *cur_action,
                                 double gc_time_ms, double io_time_ms,
                                 uint64_t device_active_time_ms,
                                 size_t h2_candidate_size_in_bytes,
                                 bool *eager_move) {
  state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms,
                  device_active_time_ms, h2_candidate_size_in_bytes);
  *eager_move = (*cur_action == MOVE_H2) ? true : false;
  *cur_state = S_NO_ACTION;
}

bool TeraSimpleStateMachine::epilog_move_h2(bool full_gc_done,
                                            bool need_resizing,
                                            actions *cur_action,
                                            states *cur_state) {
  if (!(need_resizing && *cur_action == MOVE_H2))
    return false;

  *cur_action = NO_ACTION;
  *cur_state = S_NO_ACTION;
  return true;
}

void TeraSimpleWaitStateMachine::fsm(states *cur_state, actions *cur_action,
                                     double gc_time_ms, double io_time_ms,
                                     uint64_t device_active_time_ms,
                                     size_t h2_candidate_size_in_bytes,
                                     bool *eager_move) {
  switch (*cur_state) {
    case S_WAIT_GROW:
      state_wait_after_grow(cur_state, cur_action, gc_time_ms, io_time_ms,
                            h2_candidate_size_in_bytes);
      break;
    case S_WAIT_SHRINK:
      state_wait_after_shrink(cur_state, cur_action, gc_time_ms,
                              io_time_ms, h2_candidate_size_in_bytes);
      break;
    case S_WAIT_MOVE:
      *cur_state = S_WAIT_MOVE;
      *cur_action = MOVE_H2;
      *eager_move = false;
      break;
    case S_NO_ACTION:
      state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms,
                      device_active_time_ms, h2_candidate_size_in_bytes);
      break;
  }
}

void TeraSimpleWaitStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                       double gc_time_ms, double io_time_ms,
                                                       size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_GROW;
    *cur_action =  WAIT_AFTER_GROW;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  
  if ((gc_time_ms > io_time_ms) && !high_occupancy) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void TeraSimpleWaitStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                         double gc_time_ms, double io_time_ms,
                                                         size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }

  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));

  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

bool TeraSimpleWaitStateMachine::epilog_move_h2(bool full_gc_done,
                                            bool need_resizing,
                                            actions *cur_action,
                                            states *cur_state) {

  if (!(full_gc_done && *cur_state == S_WAIT_MOVE))
    return false;

  *cur_action = NO_ACTION;
  *cur_state = S_NO_ACTION;
  return true;
}

void TeraAggrGrowStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                     double gc_time_ms, double io_time_ms,
                                                     size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_GROW;
    *cur_action =  WAIT_AFTER_GROW;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();

  if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1; // remember to change that for optimization in S_GROW_H1
    return;
  }

  if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void TeraAggrShrinkStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                         double gc_time_ms, double io_time_ms,
                                                         size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));
  if (io_time_ms > gc_time_ms && !ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}
  
void TeraGrowAfterShrinkStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                              double gc_time_ms, double io_time_ms,
                                                              size_t h2_cand_size_in_bytes) {

  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));
  if (io_time_ms > gc_time_ms && !ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }

  if (gc_time_ms > io_time_ms) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1;
    return;
  }
    
  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

void TeraOptWaitAfterShrinkStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                                 double gc_time_ms, double io_time_ms,
                                                                 size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  if (gc_time_ms > io_time_ms) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1;
    return;
  }
  
  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));
  if (io_time_ms > gc_time_ms && !ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

void TeraShrinkAfterGrowStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                            double gc_time_ms, double io_time_ms,
                                                            size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_GROW;
    *cur_action =  WAIT_AFTER_GROW;
    return;
  }

  if (io_time_ms > gc_time_ms) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  //bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
  
  //if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
  //  *cur_state = S_WAIT_GROW;
  //  *cur_action = GROW_H1;
  //  return;
  //}

  if (gc_time_ms > io_time_ms && !high_occupancy) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void TeraOptWaitAfterGrowStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                             double gc_time_ms, double io_time_ms,
                                                             size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_GROW;
    *cur_action =  WAIT_AFTER_GROW;
    return;
  }

  if (io_time_ms > gc_time_ms) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  // We calculate the ratio of the h2 candidate objects in H1 
  double h2_candidate_ratio = (double) h2_cand_size_in_bytes / Universe::heap()->capacity();
  if (gc_time_ms >= io_time_ms &&  h2_candidate_ratio >= TRANSFER_THRESHOLD) {
    *cur_state = S_WAIT_MOVE;
    *cur_action = MOVE_H2;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
  
  if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1;
    return;
  }

  if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}
  
void TeraFullOptimizedStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                            double gc_time_ms, double io_time_ms,
                                                            size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_NO_ACTION;
    *cur_action = NO_ACTION;
    return;
  }
  
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
  if (gc_time_ms >= io_time_ms && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1;
    return;
  }
  
  // We calculate the ratio of the h2 candidate objects in H1 
  double h2_candidate_ratio = (double) h2_cand_size_in_bytes / Universe::heap()->capacity();
  if (gc_time_ms >= io_time_ms &&  h2_candidate_ratio >= TRANSFER_THRESHOLD) {
    *cur_state = S_WAIT_MOVE;
    *cur_action = MOVE_H2;
    return;
  }
  
  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));
  bool is_old_gen_full = ((double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes()) >= 0.75;
  if (io_time_ms > gc_time_ms && !ioslack && !is_old_gen_full) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;

}

void TeraFullOptimizedStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                          double gc_time_ms, double io_time_ms,
                                                          size_t h2_cand_size_in_bytes) {
  
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_NO_ACTION;
    *cur_action =  NO_ACTION;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool is_old_gen_full = ((double) old_gen->used_in_bytes() / old_gen->capacity_in_bytes()) >= 0.75;
  if (io_time_ms > gc_time_ms && !is_old_gen_full) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  // We calculate the ratio of the h2 candidate objects in H1 
  double h2_candidate_ratio = (double) h2_cand_size_in_bytes / Universe::heap()->capacity();
  if (gc_time_ms >= io_time_ms &&  h2_candidate_ratio >= TRANSFER_THRESHOLD) {
    *cur_state = S_WAIT_MOVE;
    *cur_action = MOVE_H2;
    return;
  }

  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
  
  if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1;
    return;
  }

  if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}
  
void TeraDirectGrowShrinkStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                               double gc_time_ms, double io_time_ms,
                                                               size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));
  if (io_time_ms > gc_time_ms && !ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

void TeraDirectGrowShrinkStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                             double gc_time_ms, double io_time_ms,
                                                             size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_GROW;
    *cur_action =  WAIT_AFTER_GROW;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();

  if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1; // remember to change that for optimization in S_GROW_H1
    return;
  }

  if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void TeraFullDirectGrowShrinkStateMachine::state_wait_after_shrink(states *cur_state, actions *cur_action,
                                                               double gc_time_ms, double io_time_ms,
                                                               size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }

  if (gc_time_ms > io_time_ms) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1; // remember to change that for optimization in S_GROW_H1
    return;
  }
  
  size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (TeraDRAMLimit * 0.97));
  if (io_time_ms > gc_time_ms && !ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = SHRINK_H1;
    return;
  }
  
  if (io_time_ms > gc_time_ms && ioslack) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }
  
  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION;
}

void TeraFullDirectGrowShrinkStateMachine::state_wait_after_grow(states *cur_state, actions *cur_action,
                                                             double gc_time_ms, double io_time_ms,
                                                             size_t h2_cand_size_in_bytes) {
  if (abs(io_time_ms - gc_time_ms) <= EPSILON) {
    *cur_state = S_WAIT_GROW;
    *cur_action =  WAIT_AFTER_GROW;
    return;
  }
  
  if (io_time_ms > gc_time_ms) {
    *cur_state = S_WAIT_SHRINK;
    *cur_action = IOSLACK;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();

  if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = GROW_H1; // remember to change that for optimization in S_GROW_H1
    return;
  }

  if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit) {
    *cur_state = S_WAIT_GROW;
    *cur_action = WAIT_AFTER_GROW;
    return;
  }

  *cur_state = S_NO_ACTION;
  *cur_action = NO_ACTION; // remember to change that for optimization in S_GROW_H1
}
