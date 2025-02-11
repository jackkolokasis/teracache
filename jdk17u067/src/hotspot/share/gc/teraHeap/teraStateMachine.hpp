#ifndef SHARE_GC_TERAHEAP_TERASTATEMACHINE_HPP
#define SHARE_GC_TERAHEAP_TERASTATEMACHINE_HPP

#include "gc/teraHeap/teraEnum.h"
#include "gc/parallel/parallelScavengeHeap.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "memory/allocation.hpp"
#include "memory/sharedDefines.h"

class TeraStateMachine : public CHeapObj<mtInternal> {
public:
  virtual void fsm(states *cur_state, actions *cur_action, double gc_time_ms,
                   double io_time_ms, uint64_t device_active_time_ms,
                   size_t h2_cand_size_in_bytes, bool *eager_move) = 0;

  virtual void state_wait_after_grow(states *cur_state, actions *cur_action,
                                     double gc_time_ms, double io_time_ms,
                                     size_t h2_cand_size_in_bytes) = 0;

  virtual void state_wait_after_shrink(states *cur_state, actions *cur_action,
                                       double gc_time_ms, double io_time_ms,
                                       size_t h2_cand_size_in_bytes);

  // We call this function after moving objects to H2 to reset the
  // state and the counters.
  virtual bool epilog_move_h2(bool full_gc_done, bool need_resizing,
                              actions *cur_action, states *cur_state) = 0;

  virtual void state_no_action(states *cur_state, actions *cur_action,
                               double gc_time_ms, double io_time_ms,
                               uint64_t device_active_time_ms,
                               size_t h2_cand_size_in_bytes);
  
  // Read the memory statistics for the cgroup
  size_t read_cgroup_mem_stats(bool read_page_cache);

  // Read the process anonymous memory
  size_t read_process_anon_memory();
};

class TeraStateMachineOnlyDelay: public TeraStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  actions last_action = GROW_H1;
  
  void state_wait_after_grow(states *cur_state, actions *cur_action,
                             double gc_time_ms, double io_time_ms,
                             size_t h2_cand_size_in_bytes);

  void state_wait_after_shrink(states *cur_state, actions *cur_action,
                               double gc_time_ms, double io_time_ms,
                               size_t h2_cand_size_in_bytes);
  
  void state_no_action(states *cur_state, actions *cur_action,
                       double gc_time_ms, double io_time_ms,
                       uint64_t device_active_time_ms,
                       size_t h2_cand_size_in_bytes);

public:
  TeraStateMachineOnlyDelay() {
    if (!TeraHeapStatistics) {
      return;
    }
    thlog_or_tty->print_cr("Resizing Policy = TeraStateMachineOnlyDelay\n");
    thlog_or_tty->flush();
  }
  
  void fsm(states *cur_state, actions *cur_action, double gc_time_ms,
           double io_time_ms, uint64_t device_active_time_ms,
           size_t h2_cand_size_in_bytes, bool *eager_move);


  bool epilog_move_h2(bool full_gc_done, bool need_resizing,
                      actions *cur_action, states *cur_state) {
    return false;
  }
};

class TeraStateMachineDelayAndCosts : public TeraStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  
  void state_wait_after_grow(states *cur_state, actions *cur_action,
                             double gc_time_ms, double io_time_ms,
                             size_t h2_cand_size_in_bytes);

  void state_wait_after_shrink(states *cur_state, actions *cur_action,
                               double gc_time_ms, double io_time_ms,
                               size_t h2_cand_size_in_bytes);
  
  void state_no_action(states *cur_state, actions *cur_action,
                       double gc_time_ms, double io_time_ms,
                       uint64_t device_active_time_ms,
                       size_t h2_cand_size_in_bytes);

public:
  TeraStateMachineDelayAndCosts() {
    tty->print_cr("Resizing Policy = TeraStateMachineDelayAndCosts\n");
    tty->flush();
  }
  
  void fsm(states *cur_state, actions *cur_action, double gc_time_ms,
           double io_time_ms, uint64_t device_active_time_ms,
           size_t h2_cand_size_in_bytes, bool *eager_move);


  bool epilog_move_h2(bool full_gc_done, bool need_resizing,
                      actions *cur_action, states *cur_state) {
    return false;
  }
};

class TeraStateMachineOptimalState : public TeraStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  actions last_action = GROW_H1;
  
  void state_wait_after_grow(states *cur_state, actions *cur_action,
                             double gc_time_ms, double io_time_ms,
                             size_t h2_cand_size_in_bytes);

  void state_wait_after_shrink(states *cur_state, actions *cur_action,
                               double gc_time_ms, double io_time_ms,
                               size_t h2_cand_size_in_bytes);
  
  void state_no_action(states *cur_state, actions *cur_action,
                       double gc_time_ms, double io_time_ms,
                       uint64_t device_active_time_ms,
                       size_t h2_cand_size_in_bytes);

public:
  TeraStateMachineOptimalState() {
    tty->print_cr("Resizing Policy = TeraStateMachineOptimalState\n");
    tty->flush();
  }
  
  void fsm(states *cur_state, actions *cur_action, double gc_time_ms,
           double io_time_ms, uint64_t device_active_time_ms,
           size_t h2_cand_size_in_bytes, bool *eager_move);


  bool epilog_move_h2(bool full_gc_done, bool need_resizing,
                      actions *cur_action, states *cur_state) {
    return false;
  }
};
 
// class TeraSimpleStateMachine : public TeraStateMachine {
// public:
//   TeraSimpleStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraSimpleStateMacine\n");
//     thlog_or_tty->flush();
//   }
//   void fsm(states *cur_state, actions *cur_action, double gc_time_ms,
//            double io_time_ms, uint64_t device_active_time_ms,
//            size_t h2_cand_size_in_bytes, bool *eager_move);

//   void state_wait_after_grow(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes) {
//     return;
//   }

//   void state_wait_after_shrink(states *cur_state, actions *cur_action,
//                                double gc_time_ms, double io_time_ms,
//                                size_t h2_cand_size_in_bytes) {
//     return;
//   }

//   bool epilog_move_h2(bool full_gc_done, bool need_resizing,
//                       actions *cur_action, states *cur_state);
// };

// class TeraSimpleWaitStateMachine : public TeraStateMachine {
// public:
//   TeraSimpleWaitStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraSimpleWaitStateMacine\n");
//     thlog_or_tty->flush();
//   }

//   void fsm(states *cur_state, actions *cur_action, double gc_time_ms,
//            double io_time_ms, uint64_t device_active_time_ms,
//            size_t h2_cand_size_in_bytes, bool *eager_move);

//   void state_wait_after_grow(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes);

//   void state_wait_after_shrink(states *cur_state, actions *cur_action,
//                                double gc_time_ms, double io_time_ms,
//                                size_t h2_cand_size_in_bytes);
//   
//   bool epilog_move_h2(bool full_gc_done, bool need_resizing,
//                       actions *cur_action, states *cur_state);
// };

// class TeraAggrGrowStateMachine : public TeraSimpleWaitStateMachine {
// public:
//   TeraAggrGrowStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraAggrGrowStateMachine\n");
//     thlog_or_tty->flush();
//   }

//   void state_wait_after_grow(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes);
// };

// class TeraAggrShrinkStateMachine : public TeraSimpleWaitStateMachine {
// public:
//   TeraAggrShrinkStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraAggrShrinkStateMachine\n");
//     thlog_or_tty->flush();
//   }

//   void state_wait_after_shrink(states *cur_state, actions *cur_action,
//                                double gc_time_ms, double io_time_ms,
//                                size_t h2_cand_size_in_bytes);
// };

// class TeraGrowAfterShrinkStateMachine : public TeraSimpleWaitStateMachine {
// public:
//   TeraGrowAfterShrinkStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraGrowAfterShrinkStateMachine\n");
//     thlog_or_tty->flush();
//   }

//   void state_wait_after_shrink(states *cur_state, actions *cur_action,
//                                double gc_time_ms, double io_time_ms,
//                                size_t h2_cand_size_in_bytes);
// };

// class TeraOptWaitAfterShrinkStateMachine : public TeraSimpleWaitStateMachine {
// public:
//   TeraOptWaitAfterShrinkStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraOptWaitAfterShrinkStateMachine\n");
//     thlog_or_tty->flush();
//   }

//   void state_wait_after_shrink(states *cur_state, actions *cur_action,
//                                double gc_time_ms, double io_time_ms,
//                                size_t h2_cand_size_in_bytes);
// };

// class TeraShrinkAfterGrowStateMachine : public TeraOptWaitAfterShrinkStateMachine {
// public:
//   TeraShrinkAfterGrowStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraShrinkAfterGrowStateMachine\n");
//     thlog_or_tty->flush();
//   }

//   void state_wait_after_grow(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes);
// };

// class TeraOptWaitAfterGrowStateMachine : public TeraOptWaitAfterShrinkStateMachine {
// public:
//   TeraOptWaitAfterGrowStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraOptWaitAfterGrowStateMachine\n");
//     thlog_or_tty->flush();
//   }

//   void state_wait_after_grow(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes);
// };

// class TeraFullOptimizedStateMachine : public TeraSimpleWaitStateMachine {
// public:
//   TeraFullOptimizedStateMachine() {
//     thlog_or_tty->print_cr("Resizing Policy = TeraFullOptimizedStateMachine\n");
//     thlog_or_tty->flush();
//   }
//   
//   void state_wait_after_shrink(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes);

//   void state_wait_after_grow(states *cur_state, actions *cur_action,
//                              double gc_time_ms, double io_time_ms,
//                              size_t h2_cand_size_in_bytes);
// };

#endif // SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERASTATEMACHINE_HPP
