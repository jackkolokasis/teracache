#ifndef SHARE_GC_FLEXHEAP_FLEXSTATEMACHINE_HPP
#define SHARE_GC_FLEXHEAP_FLEXSTATEMACHINE_HPP

#include "gc/flexHeap/flexEnum.h"
#include "gc/shared/collectedHeap.hpp"
#include "memory/allocation.hpp"
#include "memory/sharedDefines.h"

class FlexStateMachine : public CHeapObj<mtInternal> {
public:
  virtual void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
                   double io_time_ms) = 0;

  virtual void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                                     double gc_time_ms, double io_time_ms) = 0;

  virtual void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                                       double gc_time_ms, double io_time_ms) = 0;

  virtual void state_no_action(fh_states *cur_state, fh_actions *cur_action,
                               double gc_time_ms, double io_time_ms) = 0;

  virtual void state_stable(fh_states *cur_state, fh_actions *cur_action,
                            double gc_time_ms, double io_time_ms) = 0;
  
  // Read the memory statistics for the cgroup
  size_t read_cgroup_mem_stats(bool read_page_cache);

  // Read the process anonymous memory
  size_t read_process_anon_memory();
};

// class FlexSimpleStateMachine : public FlexStateMachine {
// public:
//   FlexSimpleStateMachine() {
//     tty->print_cr("Resizing Policy = FlexSimpleStateMacine\n");
//     tty->flush();
//   }
//   void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
//            double io_time_ms);
//   
//   void state_no_action(fh_states *cur_state, fh_actions *cur_action,
//                        double gc_time_ms, double io_time_ms);

//   void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
//                              double gc_time_ms, double io_time_ms) {
//     return;
//   }

//   void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
//                                double gc_time_ms, double io_time_ms) {
//     return;
//   }
//   
//   virtual void state_stable(fh_states *cur_state, fh_actions *cur_action,
//                             double gc_time_ms, double io_time_ms) {
//     return;
//   }

// };

// class FlexSimpleWaitStateMachine : public FlexSimpleStateMachine {
// public:
//   FlexSimpleWaitStateMachine() {
//     tty->print_cr("Resizing Policy = FlexSimpleWaitStateMacine\n");
//     tty->flush();
//   }

//   void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
//            double io_time_ms);

//   void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
//                              double gc_time_ms, double io_time_ms);

//   void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
//                                double gc_time_ms, double io_time_ms);
// };

// class FlexFullOptimizedStateMachine : public FlexSimpleWaitStateMachine {
// public:
//   FlexFullOptimizedStateMachine() {
//     tty->print_cr("Resizing Policy = FlexFullOptimizedStateMachine\n");
//     tty->flush();
//   }
//   
//   void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
//                              double gc_time_ms, double io_time_ms);

//   void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
//                              double gc_time_ms, double io_time_ms);
// };

class FlexStateMachineWithOptimalState : public FlexStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  fh_states last_state;
  fh_actions last_action;

public:
  FlexStateMachineWithOptimalState() {
    tty->print_cr("Resizing Policy = FlexStateMachineWithOptimalState\n");
    tty->flush();
  }

  void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
           double io_time_ms);

  void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                             double gc_time_ms, double io_time_ms);

  void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                               double gc_time_ms, double io_time_ms);

  void state_no_action(fh_states *cur_state, fh_actions *cur_action,
                       double gc_time_ms, double io_time_ms);

  void state_stable(fh_states *cur_state, fh_actions *cur_action,
                    double gc_time_ms, double io_time_ms);
};

// class FlexSimpleStateMachineOnlyDelay : public FlexStateMachine {
// private:
//   double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
//   fh_actions last_action = FH_GROW_HEAP;

// public:
//   FlexSimpleStateMachineOnlyDelay() {
//     tty->print_cr("Resizing Policy = FlexSimpleStateMacineOnlyDelay\n");
//     tty->flush();
//   }
//   void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
//            double io_time_ms);
//   
//   void state_no_action(fh_states *cur_state, fh_actions *cur_action,
//                        double gc_time_ms, double io_time_ms);

//   void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
//                              double gc_time_ms, double io_time_ms) {
//     return;
//   }

//   void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
//                                double gc_time_ms, double io_time_ms) {
//     return;
//   }
//   
//   virtual void state_stable(fh_states *cur_state, fh_actions *cur_action,
//                             double gc_time_ms, double io_time_ms) {
//     return;
//   }

// };


class FlexSimpleWaitStateMachineOnlyDelay : public FlexStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  fh_actions last_action = FH_GROW_HEAP;

public:
  FlexSimpleWaitStateMachineOnlyDelay() {
    tty->print_cr("Resizing Policy = FlexSimpleWaitStateMachineOnlyDelay\n");
    tty->flush();
  }
  void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
           double io_time_ms);
  
  void state_no_action(fh_states *cur_state, fh_actions *cur_action,
                       double gc_time_ms, double io_time_ms);

  void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                             double gc_time_ms, double io_time_ms);

  void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                               double gc_time_ms, double io_time_ms);
  
  virtual void state_stable(fh_states *cur_state, fh_actions *cur_action,
                            double gc_time_ms, double io_time_ms) {
    return;
  }

};

class FlexSimpleWaitStateMachineOnlyDelayWithOptimalState : public FlexStateMachine {
private:
  double delay_before_action = 0;         // Sum of gctime and iowait time before the last action
  fh_actions last_action = FH_GROW_HEAP;

public:
  FlexSimpleWaitStateMachineOnlyDelayWithOptimalState() {
    tty->print_cr("Resizing Policy = FlexSimpleWaitStateMachineOnlyDelay\n");
    tty->flush();
  }
  void fsm(fh_states *cur_state, fh_actions *cur_action, double gc_time_ms,
           double io_time_ms);
  
  void state_no_action(fh_states *cur_state, fh_actions *cur_action,
                       double gc_time_ms, double io_time_ms);

  void state_wait_after_grow(fh_states *cur_state, fh_actions *cur_action,
                             double gc_time_ms, double io_time_ms);

  void state_wait_after_shrink(fh_states *cur_state, fh_actions *cur_action,
                               double gc_time_ms, double io_time_ms);
  
  virtual void state_stable(fh_states *cur_state, fh_actions *cur_action,
                            double gc_time_ms, double io_time_ms) {
    return;
  }

};

#endif // SHARE_GC_FLEXHEAP_FLEXSTATEMACHINE_HPP
