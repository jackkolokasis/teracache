#ifndef SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERAHEAP_HPP
#define SHARE_VM_GC_IMPLEMENTATION_TERAHEAP_TERAHEAP_HPP

#include "gc_implementation/parallelScavenge/objectStartArray.hpp"
#include "gc_implementation/teraHeap/teraDynamicResizingPolicy.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.hpp"

#include <regions.h>
#ifdef BACK_REF_STAT
#include <map>
#include <tr1/tuple>
#endif

class TeraHeap: public CHeapObj<mtInternal> {
private:
  static char *_start_addr; // TeraHeap start address of mmap region
  static char *_stop_addr;  // TeraHeap ends address of mmap region

  ObjectStartArray _start_array; // Keeps track of where objects
                                        // start in a 2^CARD_SEGMENT_SIZE block

  /*-----------------------------------------------
   * Stacks
   *---------------------------------------------*/
  // Stack to keep back pointers (Objects that are pointed out of
  // TeraHeap objects) to mark them as alive durin mark_and_push phase of
  // the Full GC.
  static Stack<oop *, mtGC> _tc_stack;

  // Stack to keep the element addresses of objects that are located in
  // TeraHeap and point to objects in the heap. We adjust these pointers
  // during adjust phase of the Full GC.
  static Stack<oop *, mtGC> _tc_adjust_stack;

  /*-----------------------------------------------
   * Statistics of TeraHeap
   *---------------------------------------------*/
  static uint64_t total_objects; //< Total number of objects located in TeraHeap
  static uint64_t total_objects_size; //< Total number of objects size

  static uint64_t fwd_ptrs_per_fgc;    //< Total number of forward ptrs per FGC
  static uint64_t back_ptrs_per_fgc;   //< Total number of back ptrs per FGC
  static uint64_t trans_per_fgc;       //< Total number of objects transfered to
                                       //< TeraHeap per FGC
  static uint64_t tc_ct_trav_time[16]; //< Time to traverse TeraCards card table
  static uint64_t heap_ct_trav_time[16]; //< Time to traverse heap card tables

  static uint64_t back_ptrs_per_mgc; //< Total number of back ptrs per MGC

  static uint64_t
      obj_distr_size[3]; //< Object size distribution between B, KB, MB

  static long int cur_obj_group_id; //<We save the current object
                                    // group id for tera-marked
                                    // object to promote this id
                                    // to their reference objects
  static long int cur_obj_part_id;  //<We save the current object
                                    // partition id for tera-marked
                                    // object to promote this id
                                    // to their reference objects

  HeapWord *obj_h1_addr;            // We need to check this
                                    // object that will be moved
                                    // to H2 if it has back ptrs
                                    // to H1

  HeapWord *obj_h2_addr;            // We need to check this
                                    // object that will be moved
                                    // to H2 if it has back ptrs
                                    // to H1
  bool shrink_h1;                   //< This flag indicate that H1
                                    // should be shrinked
  bool grow_h1;                     //< This flag indicate that H1
                                    // should be grown
  
  TeraDynamicResizingPolicy* dynamic_resizing_policy; 

#if defined(HINT_HIGH_LOW_WATERMARK) || defined(NOHINT_HIGH_LOW_WATERMARK)
  size_t total_marked_obj_for_h2;   // Total marked objects to be moved in H2

  size_t h2_low_promotion_threshold;    // Promotion threshold
#endif
  
  long non_promote_tag;             // Object with this label cannot be promoted to H2

  long promote_tag;                 // Objects with labels less than
                                    // the promote_tag can be moved to
                                    // H2 during major GC

  bool direct_promotion;            // Indicate to move tagged objects
                                    // to H2 without waiting any hint
                                    // from the framework

#ifdef OBJ_STATS
  size_t primitive_arrays_size;     //< Total size of promitive arrays
  size_t primitive_obj_size;        //< Total size of objects with ONLY primitive type fields
  size_t non_primitive_obj_size;    //< Total size of objects with non primitive type fields

  size_t num_primitive_arrays;      //< Total number of promitive arrays instances
  size_t num_primitive_obj;         //< Total number of objects with ONLY primitive type fields
  size_t num_non_primitive_obj;     //< Total size of objects with non primitive type fields

  size_t h2_primitive_array_size;   //< Total size of H2 objects that are primitive arrays 
  size_t num_h2_primitive_array;    //< Total number of H2 objects that are primitive arrays
#endif
  bool traced_obj_has_ref_field;    //< Indicate that the object we
                                    // scan in the marking phase of the
                                    // major gc has references to other objects 
#ifdef BACK_REF_STAT
  // This histogram keeps internally statistics for the backward
  // references (H2 to H1)
  std::map<oop *, std::tr1::tuple<int, int, int> > histogram;
  oop *back_ref_obj;
#endif // BACK_REF_STAT

#ifdef FWD_REF_STAT
  // This histogram keeps internally statistics for the forward references
  // (H1 to H2) per object
  std::map<oop, int> fwd_ref_histo;
  
  // Print the histogram
  void h2_print_fwd_ref_stat();
#endif

  void h2_count_marked_objects();

  void h2_reset_marked_objects();

public:
  // Constructor
  TeraHeap();
  
  // Get object start array for h2
  ObjectStartArray *start_array() { return &_start_array; }
  
  // Return H2 start address
  char *h2_start_addr(void);

  // Return H2 stop address
  char *h2_end_addr(void);
  
  // Get the top allocated address of the H2. This address depicts the
  // end address of the last allocated object in the last region of
  // H2.
  char *h2_top_addr(void);
  
  // Check if H2 is empty.
  // Return true if H2 is empty, false otherwise
  bool h2_is_empty(void);
  
  // Check if an object `ptr` belongs to the TeraHeap. If the object belongs
  // then the function returns true, otherwise it returns false.
  bool is_obj_in_h2(oop ptr);

  // Check if reference `p` which depicts the field of the object
  // belongs to TeraHeap. If the object belongs then the function
  // returns true, otherwise it returns false.
  bool is_field_in_h2(void *p);
  
  // Deallocate the backward references stacks
  void h2_clear_back_ref_stacks();
  
  // Keep for each thread with 'tid' the 'total time' that needed to
  // traverse the TeraHeap card table.
  // Each thread writes the time in a table based on each ID and then we
  // take the maximum time from all the threads as the total time.
  void h2_back_ref_traversal_time(unsigned int tid, uint64_t total_time);

  // Keep for each thread with 'tid' the 'total time' that needed to
  // traverse the Heap card table.
  // Each thread writes the time in a table based on each ID and then we
  // take the maximum time from all the threads as the total time.
  void h1_old_to_young_traversal_time(unsigned int tid, uint64_t total_time);
  
  // Print the statistics of TeraHeap at the end of each minorGC
  // Will print:
  //	- the time to traverse the TeraHeap dirty card tables
  //	- the time to traverse the Heap dirty card tables
  //	- TODO number of dirty cards in TeraHeap
  //	- TODO number of dirty cards in Heap
  void print_minor_gc_statistics();

  // Give advise to kernel to expect page references in sequential order
  void h2_enable_seq_faults();

  // Give advise to kernel to expect page references in random order
  void h2_enable_rand_faults();
  
  // Check if the first object `obj` in the H2 region is valid. If not
  // that depicts that the region is empty
  bool check_if_valid_object(HeapWord *obj);

  // Get the ending address of the last object of the region obj
  // belongs to.
  HeapWord *get_last_object_end(HeapWord *obj);

  // Checks if the address of obj is the beginning of a region
  bool is_start_of_region(HeapWord *obj);
  
  // Retrurn the start address of the first object of the secific region
  HeapWord *get_first_object_in_region(HeapWord *addr);

  // Add new object in the region
  char *h2_add_object(oop obj, size_t size);

  // Pop the objects that are in `_tc_stack` and mark them as live
  // object. These objects are located in the Java Heap and we need to
  // ensure that they will be kept alive.
  void h2_mark_back_references();

  // Increase forward ptrs from JVM heap to TeraHeap
  void h2_increase_fwd_ref();

  // Update backward reference stacks that we use in marking and
  // pointer adjustment phases of major GC.
  void h2_push_backward_reference(void *p, oop o);

  // Adjust backwards pointers during major GC.
  void h2_adjust_back_references();

  // Init the statistics counters of TeraHeap to zero when a Full GC
  // starts
  void h2_init_stats_counters();

  // Print the statistics of TeraHeap at the end of each FGC
  // Will print:
  //	- the total forward pointers from the JVM heap to the
  // TeraHeap
  //	- the total back pointers from TeraHeap to the JVM heap
  //	- the total objects that has been transfered to the TeraHeap
  //	- the current total size of objects in TeraHeap
  //	- the current total objects that are located in TeraHeap
  void h2_print_stats();

  // Explicit (using systemcall) write 'data' with 'size' to the specific
  // 'offset' in the file.
  void h2_write(char *data, char *offset, size_t size);

  // Explicit (using systemcall) asynchronous write 'data' with 'size' to
  // the specific 'offset' in the file.
  void h2_awrite(char *data, char *offset, size_t size);

  // We need to ensure that all the writes in TeraHeap using asynchronous
  // I/O have been completed succesfully.
  int h2_areq_completed();

  // Fsync writes in TeraHeap
  // We need to make an fsync when we use fastmap
  void h2_fsync();

#ifdef PR_BUFFER
  // Add an object 'obj' with size 'size' to the promotion buffer. 'New_adr' is
  // used to know where the object will move to H2. We use promotion buffer to
  // reduce the number of system calls for small sized objects.
  void h2_promotion_buffer_insert(char *obj, char *new_adr, size_t size);

  // At the end of the major GC flush and free all the promotion
  // buffers.
  void h2_free_promotion_buffers();
#endif

  // Resets the used field of all regions
  void h2_reset_used_field(void);

  // Marks the region containing obj as used
  void mark_used_region(HeapWord *obj);

  // Wrapper function for get_region_metadata
  // file: project_dir/allocator/include/segment.h
  struct region* get_region_meta(char* obj);

  char* get_h2_first_obj();

  size_t get_h2_region_no();

  //returns region size
  //Wrapper for region_size
  // file: project_dir/allocator/include/segment.h
  uint64_t get_region_size();

  // Prints all active regions
  void print_h2_active_regions(void);

  // Groups the region of obj with the previously enabled region
  void group_region_enabled(HeapWord *obj, void *obj_field);

  // Frees all unused regions
  void free_unused_regions(void);

  // Prints all the region groups
  void print_region_groups(void);

  // Enables groupping with region of obj
  void enable_groups(HeapWord *old_addr, HeapWord *new_addr);

  // Disables region groupping
  void disable_groups(void);

  void print_object_name(HeapWord *obj, const char *name);

  // Add a new entry to `obj1` region dependency list that reference
  // `obj2` region
  void group_regions(HeapWord *obj1, HeapWord *obj2);

  // We save the current object group 'id' for tera-marked object to
  // promote this 'id' to its reference objects
  void set_cur_obj_group_id(long int id);

  // Get the saved current object group id
  long int get_cur_obj_group_id(void);

  // We save the current object partition 'id' for tera-marked object to
  // promote this 'id' to its reference objects
  void set_cur_obj_part_id(long int id);

  // Get the saved current object partition id
  long int get_cur_obj_part_id(void);

  // Iterate over all objects in each region and print their states
  // This function is for debugging purposes to understand and fix the
  // locality in regions
  void h2_print_objects_per_region(void);

  void mark_live(HeapWord *p);

  void h2_mark_live_objects_per_region();

  // Check if backward adjust stack is empty
  bool h2_is_empty_back_ref_stacks();

  // Get the group Id of the objects that belongs to this region. We
  // locate the objects of the same group to the same region. We use the
  // field 'p' of the object to identify in which region the object
  // belongs to.
  uint64_t h2_get_region_groupId(void *p);

  // Get the partition Id of the objects that belongs to this region. We
  // locate the objects of the same group to the same region. We use the
  // field 'p' of the object to identify in which region the object
  // belongs to.
  uint64_t h2_get_region_partId(void *p);

#ifdef BACK_REF_STAT
  // Add a new entry to the histogram for back reference that start from
  // 'obj' and results in H1 (new or old generation).
  // Use this function with a single GC thread
  void h2_update_back_ref_stats(bool is_old, bool is_tera_cache);

  void h2_enable_back_ref_traversal(oop *obj);

  // Print the histogram
  void h2_print_back_ref_stats();
#endif

#ifdef FWD_REF_STAT
  // Add a new entry to the histogram for forward reference that start from
  // H1 and results in 'obj' in H2
  void h2_add_fwd_ref_stat(oop obj);
#endif
		
  // Set non promote label value
  void set_non_promote_tag(long val);

  // Set promote label value
  void set_promote_tag(long val);

  // Get non promote label value
  long get_non_promote_tag();

  // Get promote label value
  long get_promote_tag();

  // This function determines which of the H2 candidate objects found
  // during marking phase we are going to move to H2. According to the
  // policy that we enabled in the sharedDefines.h file we do the
  // appropriate action. This function is used only in the
  // precompaction phase.
  bool h2_transfer_policy(oop obj);
  
  // Promotion policy for H2 candidate objects. This function is used
  // during the marking phase of the major GC. According to the policy
  // that we enabled in the sharedDefines.h file we do the appropriate
  // action.
  bool h2_promotion_policy(oop obj);

  void set_direct_promotion(size_t old_live, size_t max_old_gen_size);

  bool is_direct_promote();

#if defined(NOHINT_HIGH_LOW_WATERMARK) || defined(HINT_HIGH_LOW_WATERMARK)
  void h2_incr_total_marked_obj_size(size_t size);

  void h2_reset_total_marked_obj_size();

  bool check_low_promotion_threshold(size_t sz);

  void set_low_promotion_threshold();
#endif // NOHINT_HIGH_LOW_WATERMARK || HINT_HIGH_LOW_WATERMARK

  // Check if the object with `addr` span multiple regions
  int h2_continuous_regions(HeapWord *addr);

  // Check where the object starts
  bool h2_object_starts_in_region(HeapWord *obj);

  // Reset object ref field flag
  void reset_obj_ref_field_flag() { traced_obj_has_ref_field = false;}
  
  // Enable the flag if the object has reference fields
  void set_obj_ref_field_flag() { traced_obj_has_ref_field = true; }

  // We set the object type based on the 'traced_obj_has_ref_field
  // flag value. We divide objects into three categories
  // - primitive arrays
  // - leaf objects which are the objects with only primitive type fields
  // - non-primitive objets which are the objects with reference fields
  void set_obj_primitive_state(oop obj);

#ifdef OBJ_STATS
  // Update counter for objects. We divide objects into three categories
  // - primitive arrays
  // - leaf objects which are the objects with only primitive type fields
  // - non-primitive objets which are the objects with reference fields
  void update_obj_stats(int type, size_t size);

  // Update counter for object H2 objects 
  void update_stats_h2_primitive_arrays(size_t size);
#endif // OBJ_STATS

  // The state machine uses this function to set if the GC should
  // shrink H1 as a result to free the physical pages. Then the OS
  // will reclaim the physical pahges and will use them as part of the
  // buffer cache.
  void set_shrink_h1() { shrink_h1 = true; }
  // Reinitalize the shrink_h1 flag
  void unset_shrink_h1() { shrink_h1 = false; }
  // Check if the state machine identifies that we need to shrink H1.
  bool  need_to_shink_h1() { return shrink_h1; }
  
  // The state machine uses this function to set if the GC should
  // grow H1.
  void set_grow_h1() { grow_h1 = true; }
  // Reinitalize the grow_h1 flag to the default value
  void unset_grow_h1() { grow_h1 = false; }
  // Check if the state machine identifies that we need to grow H1.
  bool  need_to_grow_h1() { return grow_h1; }

  TeraDynamicResizingPolicy* get_resizing_policy() {
    return dynamic_resizing_policy;
  }

  void set_direct_promotion() {
    direct_promotion = true;
  }

  void unset_direct_promotion() {
    direct_promotion = false;
  }

  
};

#endif
