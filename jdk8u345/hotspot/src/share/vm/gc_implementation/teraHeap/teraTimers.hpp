#ifndef SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERATIMERS_HPP
#define SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERATIMERS_HPP

#include "memory/allocation.hpp"

class TeraTimers: public CHeapObj<mtInternal> {
private:
  static const uint64_t CYCLES_PER_SECOND;
  uint64_t h2_scavenge_start_time;          //< Start time of finding H2 to H1 references
                                            //at the start of major GC
  uint64_t h2_scavenge_end_time;            //< End time of finding H2 to H1 references at 
                                            // the start of major GC

  uint64_t h1_marking_phase_start_time;     //< Start time of makring phase of major GC
  uint64_t h1_marking_phase_end_time;       //< End time of marking phase of major GC

  uint64_t h1_precompact_phase_start_time;  //< Start time of precompaction phase in major GC
  uint64_t h1_precompact_phase_end_time;    //< End time of precompaction phase in major GC
  
  uint64_t h1_adjust_phase_start_time;      //< Start time of adjust phase in major GC
  uint64_t h1_adjust_phase_end_time;        //< End time of adjust phase in major GC
  
  uint64_t h1_compact_start_time;           //< Start time of compaction phase in major GC
  uint64_t h1_compact_end_time;             //< End time of compaction phase in major GC
  
  uint64_t *h1_card_table_start_time;       //< Start time of scanning H1 card table
  uint64_t *h1_card_table_end_time;         //< End time of scanning H1 card table

  uint64_t *h2_card_table_start_time;       //< Start time of scanning H1 card table
  uint64_t *h2_card_table_end_time;         //< End time of scanning H1 card table

  void print_ellapsed_time(uint64_t start_time, uint64_t end_time, char* msg);

public:
  TeraTimers();
  ~TeraTimers();

  void h2_scavenge_start();
  void h2_scavenge_end();

  void h1_marking_phase_start();
  void h1_marking_phase_end();
  
  void h1_precompact_phase_start();
  void h1_precompact_phase_end();

  void h1_adjust_phase_start();
  void h1_adjust_phase_end();

  void h1_compact_start();
  void h1_compact_end();
  
  // Keep for each GC thread the time that need to traverse the H1
  // card table.
  // Each thread writes the time in a table based on each ID and then we
  // take the maximum time from all the threads as the total time.
  void h1_card_table_start(unsigned int worker_id);
  void h1_card_table_end(unsigned int worker_id);

  // Keep for each GC thread the time that need to traverse the H2
  // card table.
  // Each thread writes the time in a table based on each ID and then we
  // take the maximum time from all the threads as the total time.
  void h2_card_table_start(unsigned int worker_id);
  void h2_card_table_end(unsigned int worker_id);
  
  void print_card_table_scanning_time();
};


#endif // SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERATIMERS_HPP
