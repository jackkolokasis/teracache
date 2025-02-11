#ifndef SHARE_GC_FLEXHEAP_FLEXENUMS_HPP
#define SHARE_GC_FLEXHEAP_FLEXENUMS_HPP

enum fh_actions {
  FH_NO_ACTION,                      //< Do not perform any action
  FH_SHRINK_HEAP,                    //< Shrink H1 because the I/O is high
  FH_GROW_HEAP,                      //< Grow H1 because the GC is high
  FH_CONTINUE,                       //< Continue not finished interval
  FH_IOSLACK,                        //< IO slack for page cache to grow
  FH_WAIT_AFTER_GROW                 //< After grow wait to see the effect
};

enum fh_states {
  FHS_NO_ACTION,                    //< No action state
  FHS_WAIT_SHRINK,                  //< After shrinking h1 wait for the effect
  FHS_WAIT_GROW,                    //< After growing h1 wait for the effect
  FHS_STABLE
};

#endif // SHARE_GC_FLEXHEAP_FLEXENUMS_HPP
