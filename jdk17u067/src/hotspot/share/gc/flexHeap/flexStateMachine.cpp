#include "gc/flexHeap/flexStateMachine.hpp"
#include "gc/parallel/parallelScavengeHeap.hpp"

#define BUFFER_SIZE 1024
#define EPSILON 50

// void FlexSimpleStateMachine::state_no_action(fh_states *cur_state, fh_actions *cur_action,
//                                              double gc_time_ms, double io_time_ms) {

//   if (fabs(io_time_ms - gc_time_ms) <= EPSILON) {
//     *cur_state = FHS_NO_ACTION;
//     *cur_action = FH_NO_ACTION;
//     return;
//   }

//   PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
//   bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
//   if (gc_time_ms >= io_time_ms && under_h1_max_limit) {
//     *cur_state = FHS_WAIT_GROW;
//     *cur_action = FH_GROW_HEAP;
//     return;
//   }

//   if (io_time_ms > gc_time_ms) {
//     *cur_state = FHS_WAIT_SHRINK;
//     *cur_action = FH_SHRINK_HEAP;
//     return;
//   }

//   *cur_state = FHS_NO_ACTION;
//   *cur_action = FH_NO_ACTION;
// }

// Read the process anonymous memory
size_t FlexStateMachine::read_process_anon_memory() {
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
size_t FlexStateMachine::read_cgroup_mem_stats(bool read_page_cache) {
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

// void FlexSimpleStateMachine::fsm(fh_states *cur_state, fh_actions *cur_action,
//                                  double gc_time_ms, double io_time_ms) {
//   state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms);
//   *cur_state = FHS_NO_ACTION;
// }

// void FlexSimpleWaitStateMachine::fsm(fh_states *cur_state, fh_actions *cur_action,
//                                      double gc_time_ms, double io_time_ms) {
//   switch (*cur_state) {
//     case FHS_WAIT_GROW:
//       state_wait_after_grow(cur_state, cur_action, gc_time_ms, io_time_ms);
//       break;
//     case FHS_WAIT_SHRINK:
//       state_wait_after_shrink(cur_state, cur_action, gc_time_ms, io_time_ms);
//       break;
//     case FHS_NO_ACTION:
//       state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms);
//       break;
//     default:
//     break;
//   }
// }

// void FlexSimpleWaitStateMachine::state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
//                                                        double gc_time_ms, double io_time_ms) {
//   if (fabs(io_time_ms - gc_time_ms) <= EPSILON) {
//     *cur_state = FHS_WAIT_GROW;
//     *cur_action =  FH_WAIT_AFTER_GROW;
//     return;
//   }

//   PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
//   size_t cur_size = old_gen->capacity_in_bytes();
//   size_t used_size = old_gen->used_in_bytes();
//   // Occupancy of the old generation is higher than 70%
//   bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
//   
//   if ((gc_time_ms > io_time_ms) && !high_occupancy) {
//     *cur_state = FHS_WAIT_GROW;
//     *cur_action = FH_WAIT_AFTER_GROW;
//     return;
//   }

//   *cur_state = FHS_NO_ACTION;
//   *cur_action = FH_NO_ACTION; // remember to change that for optimization in S_GROW_H1
// }

// void FlexSimpleWaitStateMachine::state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
//                                                          double gc_time_ms, double io_time_ms) {
//   if (fabs(io_time_ms - gc_time_ms) <= EPSILON) {
//     *cur_state = FHS_WAIT_SHRINK;
//     *cur_action = FH_IOSLACK;
//     return;
//   }

//   size_t cur_rss = read_cgroup_mem_stats(false);
//   size_t cur_cache = read_cgroup_mem_stats(true);
//   bool ioslack = ((cur_rss + cur_cache) < (FlexDRAMLimit * 0.8));

//   if (io_time_ms > gc_time_ms && ioslack) {
//     *cur_state = FHS_WAIT_SHRINK;
//     *cur_action = FH_IOSLACK;
//     return;
//   }

//   *cur_state = FHS_NO_ACTION;
//   *cur_action = FH_NO_ACTION;
// }

// void FlexFullOptimizedStateMachine::state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
//                                                             double gc_time_ms, double io_time_ms) {
//   if (fabs(io_time_ms - gc_time_ms) <= EPSILON) {
//     // *cur_state = FHS_WAIT_SHRINK;
//     // *cur_action = FH_IOSLACK;
//     *cur_state = FHS_NO_ACTION;
//     *cur_action =  FH_NO_ACTION;
//     return;
//   }
//   
//   if (gc_time_ms > io_time_ms) {
//     *cur_state = FHS_WAIT_GROW;
//     *cur_action = FH_GROW_HEAP;
//     return;
//   }
//   
//   //size_t cur_rss = read_cgroup_mem_stats(false);
//   size_t cur_rss = read_process_anon_memory();
//   size_t cur_cache = read_cgroup_mem_stats(true);
//   bool ioslack = ((cur_rss + cur_cache) < (FlexDRAMLimit * 0.8));

//   if (io_time_ms > gc_time_ms && !ioslack) {
//     *cur_state = FHS_WAIT_SHRINK;
//     *cur_action = FH_SHRINK_HEAP;
//     return;
//   }
//   
//   if (io_time_ms > gc_time_ms && ioslack) {
//     *cur_state = FHS_WAIT_SHRINK;
//     *cur_action = FH_IOSLACK;
//     return;
//   }
//   
//   *cur_state = FHS_NO_ACTION;
//   *cur_action = FH_NO_ACTION;

// }

// void FlexFullOptimizedStateMachine::state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
//                                                           double gc_time_ms, double io_time_ms) {
//   
//   if (fabs(io_time_ms - gc_time_ms) <= EPSILON) {
//     // *cur_state = FHS_WAIT_GROW;
//     // *cur_action =  FH_WAIT_AFTER_GROW;
//     *cur_state = FHS_NO_ACTION;
//     *cur_action =  FH_NO_ACTION;
//     return;
//   }

//   if (io_time_ms > gc_time_ms) {
//     *cur_state = FHS_WAIT_SHRINK;
//     *cur_action = FH_SHRINK_HEAP;
//     return;
//   }
//   
//   PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
//   size_t cur_size = old_gen->capacity_in_bytes();
//   size_t used_size = old_gen->used_in_bytes();
//   // Occupancy of the old generation is higher than 70%
//   bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
//   bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
//   
//   if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit) {
//     *cur_state = FHS_WAIT_GROW;
//     *cur_action = FH_GROW_HEAP;
//     return;
//   }

//   if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit) {
//     *cur_state = FHS_WAIT_GROW;
//     *cur_action = FH_WAIT_AFTER_GROW;
//     return;
//   }

//   *cur_state = FHS_NO_ACTION;
//   *cur_action = FH_NO_ACTION; // remember to change that for optimization in S_GROW_H1
// }
  
void FlexStateMachineWithOptimalState::fsm(fh_states *cur_state,
                                           fh_actions *cur_action,
                                           double gc_time_ms,
                                           double io_time_ms) {
  switch (*cur_state) {
    case FHS_WAIT_GROW:
      state_wait_after_grow(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    case FHS_WAIT_SHRINK:
      state_wait_after_shrink(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    case FHS_NO_ACTION:
      state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms);
    case FHS_STABLE:
      state_stable(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
  }
}

void FlexStateMachineWithOptimalState::state_wait_after_grow(fh_states *cur_state,
                                                             fh_actions *cur_action,
                                                             double gc_time_ms,
                                                             double io_time_ms) {
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
  
  if (gc_time_ms > io_time_ms && high_occupancy && under_h1_max_limit && is_delay_decreased) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    return;
  }
  
  if (gc_time_ms > io_time_ms && !is_delay_decreased) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    return;
  }

  if (gc_time_ms > io_time_ms && !high_occupancy && under_h1_max_limit && is_delay_decreased) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_WAIT_AFTER_GROW;
    return;
  }

  if (io_time_ms > gc_time_ms || !is_delay_decreased) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    return;
  }
  
  // if (io_time_ms > gc_time_ms && is_delay_decreased) {
  //   *cur_state = FHS_WAIT_SHRINK;
  //   *cur_action = FH_SHRINK_HEAP;
  //   return;
  // }

  // if (io_time_ms > gc_time_ms && !is_delay_decreased) {
  //   *cur_state = FHS_STABLE;
  //   *cur_action = FH_SHRINK_HEAP;
  //   return;
  // }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void FlexStateMachineWithOptimalState::state_wait_after_shrink(fh_states *cur_state,
                                                               fh_actions *cur_action,
                                                               double gc_time_ms,
                                                               double io_time_ms) {

  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  //size_t cur_rss = read_cgroup_mem_stats(false);
  size_t cur_rss = read_process_anon_memory();
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (FlexDRAMLimit * 0.8));
  
  if (io_time_ms > gc_time_ms && ioslack && is_delay_decreased) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_IOSLACK;
    return;
  }

  if (io_time_ms > gc_time_ms && !ioslack && is_delay_decreased) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    return;
  }
  
  if (gc_time_ms > io_time_ms || !is_delay_decreased) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    return;
  }
  
  // if (gc_time_ms > io_time_ms && is_delay_decreased) {
  //   *cur_state = FHS_WAIT_GROW;
  //   *cur_action = FH_GROW_HEAP;
  //   return;
  // }

  // if (!is_delay_decreased) {
  //   *cur_state = FHS_STABLE;
  //   *cur_action = FH_GROW_HEAP;
  //   return;
  // }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void FlexStateMachineWithOptimalState::state_no_action(fh_states *cur_state,
                                                       fh_actions *cur_action,
                                                       double gc_time_ms,
                                                       double io_time_ms) {

  // The state mahcine will perform grow or shrink the heap. Thus we
  // need to save the cur_delay as the delay before the next action.
  // delay_before_action = cur_delay;
  delay_before_action = gc_time_ms + io_time_ms;
  
  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
  if (gc_time_ms >= io_time_ms && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    return;
  }

  if (io_time_ms > gc_time_ms) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    return;
  }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

void FlexStateMachineWithOptimalState::state_stable(fh_states *cur_state,
                                                    fh_actions *cur_action,
                                                    double gc_time_ms,
                                                    double io_time_ms) {

  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (is_delay_decreased) {
    *cur_state = FHS_STABLE;
    *cur_action = FH_NO_ACTION;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
  if (gc_time_ms >= io_time_ms && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    return;
  }

  if (io_time_ms > gc_time_ms) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    return;
  }
  
  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

// void FlexSimpleStateMachineOnlyDelay::fsm(fh_states *cur_state, fh_actions *cur_action,
//                                           double gc_time_ms, double io_time_ms) {
//   state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms);
// }


// void FlexSimpleStateMachineOnlyDelay::state_no_action(fh_states *cur_state, fh_actions *cur_action,
//                                                       double gc_time_ms, double io_time_ms) {

//   bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
//   delay_before_action = gc_time_ms + io_time_ms;

//   if (is_delay_decreased) {
//     *cur_state = FHS_NO_ACTION;
//     *cur_action = last_action;
//     return;
//   }
//   
//   PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
//   bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
//   if (!is_delay_decreased && last_action == FH_SHRINK_HEAP && under_h1_max_limit) {
//     *cur_state = FHS_NO_ACTION;
//     *cur_action = FH_GROW_HEAP;
//     last_action = FH_GROW_HEAP;
//     return;
//   }

//   if (!is_delay_decreased && last_action == FH_GROW_HEAP) {
//     *cur_state = FHS_NO_ACTION;
//     *cur_action = FH_SHRINK_HEAP;
//     last_action = FH_SHRINK_HEAP;
//     return;
//   }

//   *cur_state = FHS_NO_ACTION;
//   *cur_action = FH_NO_ACTION;
// }

void FlexSimpleWaitStateMachineOnlyDelay::fsm(fh_states *cur_state,
                                              fh_actions *cur_action,
                                              double gc_time_ms,
                                              double io_time_ms) {
  switch (*cur_state) {
    case FHS_WAIT_GROW:
      state_wait_after_grow(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    case FHS_WAIT_SHRINK:
      state_wait_after_shrink(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    case FHS_NO_ACTION:
      state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    default:
    break;
  }
}

void FlexSimpleWaitStateMachineOnlyDelay::state_no_action(fh_states *cur_state, fh_actions *cur_action,
                                                          double gc_time_ms, double io_time_ms) {
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (is_delay_decreased) {
    *cur_state = FHS_NO_ACTION;
    *cur_action = FH_NO_ACTION;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
  if (!is_delay_decreased && last_action == FH_SHRINK_HEAP && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }
  
  if (!is_delay_decreased && last_action == FH_GROW_HEAP) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }
  
  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

void FlexSimpleWaitStateMachineOnlyDelay::state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                                                                double gc_time_ms, double io_time_ms) {
  
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (!is_delay_decreased) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
  
  if (high_occupancy && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }
  
  if (!high_occupancy) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_WAIT_AFTER_GROW;
    return;
  }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

void FlexSimpleWaitStateMachineOnlyDelay::state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                                                                  double gc_time_ms, double io_time_ms) {
  
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (!is_delay_decreased) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }

  size_t cur_rss = read_process_anon_memory();
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (FlexDRAMLimit * 0.8));

  if (ioslack) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_IOSLACK;
    return;
  }

  if (!ioslack) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION; // remember to change that for optimization in S_GROW_H1
}

void FlexSimpleWaitStateMachineOnlyDelayWithOptimalState::fsm(fh_states *cur_state,
                                                              fh_actions *cur_action,
                                                              double gc_time_ms,
                                                              double io_time_ms) {
  switch (*cur_state) {
    case FHS_WAIT_GROW:
      state_wait_after_grow(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    case FHS_WAIT_SHRINK:
      state_wait_after_shrink(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    case FHS_NO_ACTION:
      state_no_action(cur_state, cur_action, gc_time_ms, io_time_ms);
      break;
    default:
    break;
  }
}

void FlexSimpleWaitStateMachineOnlyDelayWithOptimalState::state_no_action(fh_states *cur_state, fh_actions *cur_action,
                                                                          double gc_time_ms, double io_time_ms) {
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (is_delay_decreased) {
    *cur_state = FHS_NO_ACTION;
    *cur_action = FH_NO_ACTION;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  bool under_h1_max_limit = old_gen->capacity_in_bytes() < old_gen->max_gen_size();
  if (!is_delay_decreased && last_action == FH_SHRINK_HEAP && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }
  
  if (!is_delay_decreased && last_action == FH_GROW_HEAP) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }
  
  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

void FlexSimpleWaitStateMachineOnlyDelayWithOptimalState::state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                                                                                double gc_time_ms, double io_time_ms) {
  
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (!is_delay_decreased) {
    *cur_state = FHS_NO_ACTION;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }

  PSOldGen *old_gen = ParallelScavengeHeap::old_gen();
  size_t cur_size = old_gen->capacity_in_bytes();
  size_t used_size = old_gen->used_in_bytes();
  // Occupancy of the old generation is higher than 70%
  bool high_occupancy = (((double)(used_size) / cur_size) > 0.70);
  bool under_h1_max_limit = cur_size < old_gen->max_gen_size();
  
  if (high_occupancy && under_h1_max_limit) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }
  
  if (!high_occupancy) {
    *cur_state = FHS_WAIT_GROW;
    *cur_action = FH_WAIT_AFTER_GROW;
    return;
  }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION;
}

void FlexSimpleWaitStateMachineOnlyDelayWithOptimalState::state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                                                                  double gc_time_ms, double io_time_ms) {
  
  bool is_delay_decreased = (gc_time_ms + io_time_ms) < delay_before_action;
  delay_before_action = gc_time_ms + io_time_ms;

  if (!is_delay_decreased) {
    *cur_state = FHS_NO_ACTION;
    *cur_action = FH_GROW_HEAP;
    last_action = FH_GROW_HEAP;
    return;
  }

  size_t cur_rss = read_process_anon_memory();
  size_t cur_cache = read_cgroup_mem_stats(true);
  bool ioslack = ((cur_rss + cur_cache) < (FlexDRAMLimit * 0.8));

  if (ioslack) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_IOSLACK;
    return;
  }

  if (!ioslack) {
    *cur_state = FHS_WAIT_SHRINK;
    *cur_action = FH_SHRINK_HEAP;
    last_action = FH_SHRINK_HEAP;
    return;
  }

  *cur_state = FHS_NO_ACTION;
  *cur_action = FH_NO_ACTION; // remember to change that for optimization in S_GROW_H1
}
