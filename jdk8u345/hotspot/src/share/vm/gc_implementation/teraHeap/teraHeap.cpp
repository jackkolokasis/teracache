#include "gc_implementation/parallelScavenge/psVirtualspace.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "memory/memRegion.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/mutexLocker.hpp"
#include "gc_implementation/teraHeap/teraHeap.hpp"

char *TeraHeap::_start_addr = NULL;
char *TeraHeap::_stop_addr = NULL;

Stack<oop *, mtGC> TeraHeap::_tc_stack;
Stack<oop *, mtGC> TeraHeap::_tc_adjust_stack;

uint64_t TeraHeap::total_objects;
uint64_t TeraHeap::total_objects_size;
uint64_t TeraHeap::fwd_ptrs_per_fgc;
uint64_t TeraHeap::back_ptrs_per_fgc;
uint64_t TeraHeap::trans_per_fgc;

uint64_t TeraHeap::tc_ct_trav_time[16];
uint64_t TeraHeap::heap_ct_trav_time[16];

uint64_t TeraHeap::back_ptrs_per_mgc;

uint64_t TeraHeap::obj_distr_size[3];
long int TeraHeap::cur_obj_group_id;
long int TeraHeap::cur_obj_part_id;

// Constructor of TeraHeap
TeraHeap::TeraHeap() {

  uint64_t align = CardTableModRefBS::th_ct_max_alignment_constraint();
  init(align);

  _start_addr = start_addr_mem_pool();
  _stop_addr = stop_addr_mem_pool();

  // Initilize counters for TeraHeap
  // These counters are used for experiments
  total_objects = 0;
  total_objects_size = 0;

  // Initialize arrays for the next minor collection
  for (unsigned int i = 0; i < ParallelGCThreads; i++) {
    tc_ct_trav_time[i] = 0;
    heap_ct_trav_time[i] = 0;
  }

  back_ptrs_per_mgc = 0;

  for (unsigned int i = 0; i < 3; i++) {
    obj_distr_size[i] = 0;
  }

  cur_obj_group_id = 0;

  obj_h1_addr = NULL;
  obj_h2_addr = NULL;

  non_promote_tag = 0;
  promote_tag = -1;
  direct_promotion = false;

#if defined(HINT_HIGH_LOW_WATERMARK) || defined(NOHINT_HIGH_LOW_WATERMARK)
	total_marked_obj_for_h2 = 0;
#endif

#ifdef OBJ_STATS
  primitive_arrays_size = 0;
  primitive_obj_size = 0;
  non_primitive_obj_size = 0;

  num_primitive_arrays = 0;
  num_primitive_obj = 0;
  num_non_primitive_obj = 0;

  traced_obj_has_ref_field = false;
  
  num_h2_primitive_array = 0;
  h2_primitive_array_size = 0;
#endif
  shrink_h1 = false;
  grow_h1 = false;
  dynamic_resizing_policy = new TeraDynamicResizingPolicy();
}

// Return H2 start address
char* TeraHeap::h2_start_addr(void) {
	assert((char *)(_start_addr) != NULL, "H2 allocator is not initialized");
	return _start_addr;
}

// Return H2 stop address
char* TeraHeap::h2_end_addr(void) {
	assert((char *)(_start_addr) != NULL, "H2 allocator is not initialized");
	assert((char *)(_stop_addr) != NULL, "H2 allocator is not initialized");
	return _stop_addr;
}

// Get the top allocated address of the H2. This address depicts the
// end address of the last allocated object in the last region of
// H2.
char* TeraHeap::h2_top_addr(void) {
	return cur_alloc_ptr();
}

// Check if the TeraHeap is empty. If yes, return 'true', 'false' otherwise
bool TeraHeap::h2_is_empty() {
	return r_is_empty();
}

// Check if an object `ptr` belongs to the TeraHeap. If the object belongs
// then the function returns true, either it returns false.
bool TeraHeap::is_obj_in_h2(oop ptr) {
	return ((HeapWord *)ptr >= (HeapWord *) _start_addr)     // if greater than start address
			&& ((HeapWord *) ptr < (HeapWord *)_stop_addr);  // if smaller than stop address
}

// Check if an object `p` belongs to TeraHeap. If the object bolongs to
// TeraHeap then the function returns true, either it returns false.
bool TeraHeap::is_field_in_h2(void *p) {
	char* const cp = (char *)p;
	return cp >= _start_addr && cp < _stop_addr;
}

void TeraHeap::h2_clear_back_ref_stacks() {
	if (TeraHeapStatistics)
		back_ptrs_per_mgc = 0;
		
	_tc_adjust_stack.clear(true);
	_tc_stack.clear(true);
}

// Keep for each thread the time that need to traverse the TeraHeap
// card table.
// Each thread writes the time in a table based on each ID and then we
// take the maximum time from all the threads as the total time.
void TeraHeap::h2_back_ref_traversal_time(unsigned int tid, uint64_t total_time) {
	if (tc_ct_trav_time[tid]  < total_time)
		tc_ct_trav_time[tid] = total_time;
}

// Keep for each thread the time that need to traverse the Heap
// card table
// Each thread writes the time in a table based on each ID and then we
// take the maximum time from all the threads as the total time.
void TeraHeap::h1_old_to_young_traversal_time(unsigned int tid, uint64_t total_time) {
	if (heap_ct_trav_time[tid]  < total_time)
		heap_ct_trav_time[tid] = total_time;
}

// Print the statistics of TeraHeap at the end of each minorGC
// Will print:
//	- the time to traverse the TeraHeap dirty card tables
//	- the time to traverse the Heap dirty card tables
//	- TODO number of dirty cards in TeraHeap
//	- TODO number of dirty cards in Heap
void TeraHeap::print_minor_gc_statistics() {
	uint64_t max_tc_ct_trav_time = 0;		//< Maximum traversal time of
											// TeraHeap card tables from all
											// threads
	uint64_t max_heap_ct_trav_time = 0;     //< Maximum traversal time of Heap
											// card tables from all the threads

	for (unsigned int i = 0; i < ParallelGCThreads; i++) {
		if (max_tc_ct_trav_time < tc_ct_trav_time[i])
			max_tc_ct_trav_time = tc_ct_trav_time[i];
		
		if (max_heap_ct_trav_time < heap_ct_trav_time[i])
			max_heap_ct_trav_time = heap_ct_trav_time[i];
	}

	thlog_or_tty->print_cr("[STATISTICS] | TC_CT_TIME = %lu\n", max_tc_ct_trav_time);
	thlog_or_tty->print_cr("[STATISTICS] | HEAP_CT_TIME = %lu\n", max_heap_ct_trav_time);
	thlog_or_tty->print_cr("[STATISTICS] | BACK_PTRS_PER_MGC = %lu\n", back_ptrs_per_mgc);

#ifdef BACK_REF_STAT
	h2_print_back_ref_stats();
#endif
	
	// Initialize arrays for the next minor collection
	for (unsigned int i = 0; i < ParallelGCThreads; i++) {
		tc_ct_trav_time[i] = 0;
		heap_ct_trav_time[i] = 0;
	}

	// Initialize counters
	back_ptrs_per_mgc = 0;
}

// Give advise to kernel to expect page references in sequential order
void TeraHeap::h2_enable_seq_faults() {
#if defined(FMAP_HYBRID)
	r_enable_huge_flts();
#elif defined(MADVISE_ON)
	r_enable_seq();
#endif
}

// Give advise to kernel to expect page references in random order
void TeraHeap::h2_enable_rand_faults() {
#if defined(FMAP_HYBRID)
	r_enable_regular_flts();
#elif defined(MADVISE_ON)
	r_enable_rand();
#endif
}

// Check if the first object `obj` in the H2 region is valid. If not
// that depicts that the region is empty
bool TeraHeap::check_if_valid_object(HeapWord *obj) {
    return is_before_last_object((char *)obj);
}

// Returns the ending address of the last object in the region obj
// belongs to
HeapWord* TeraHeap::get_last_object_end(HeapWord *obj) {
    return (HeapWord*)get_last_object((char *) obj);
}

// Checks if the address of obj is the beginning of a region
bool TeraHeap::is_start_of_region(HeapWord *obj) {
    return is_region_start((char *) obj);
}

// Retrurn the start address of the first object of the secific region
HeapWord *TeraHeap::get_first_object_in_region(HeapWord *addr){
    return (HeapWord*) get_first_object((char*)addr);
}

#ifdef BACK_REF_STAT
// Add a new entry to the histogram for 'obj'
void TeraHeap::h2_update_back_ref_stats(bool is_old, bool is_tera_cache) {
	std::tr1::tuple<int, int, int> val;
	std::tr1::tuple<int, int, int> new_val;

	val = histogram[back_ref_obj];
	
	if (is_old) {                         // Reference is in the old generation  
		new_val = std::tr1::make_tuple(
				std::tr1::get<0>(val),
				std::tr1::get<1>(val) + 1,
				std::tr1::get<2>(val));
	}
	else if (is_tera_cache) {             // Reference is in the tera cache
		new_val = std::tr1::make_tuple(
				std::tr1::get<0>(val),
				std::tr1::get<1>(val),
				std::tr1::get<2>(val) + 1);
	} else {                              // Reference is in the new generation
		new_val = std::tr1::make_tuple(
				std::tr1::get<0>(val) + 1,
				std::tr1::get<1>(val),
				std::tr1::get<2>(val));
	}
	
	histogram[back_ref_obj] = new_val;
}
		
// Enable traversal `obj` for backward references.
void TeraHeap::h2_enable_back_ref_traversal(oop* obj) {
	std::tr1::tuple<int, int, int> val;

	val = std::tr1::make_tuple(0, 0, 0);

	back_ref_obj = obj;
  // Add entry to the histogram if does not exist
	histogram[obj] = val;
}

// Print the histogram
void TeraHeap::h2_print_back_ref_stats() {
	std::map<oop *, std::tr1::tuple<int, int, int> >::const_iterator it;
	
	thlog_or_tty->print_cr("Start_Back_Ref_Statistics\n");

	for(it = histogram.begin(); it != histogram.end(); ++it) {
		if (std::tr1::get<0>(it->second) > 1000 || std::tr1::get<1>(it->second) > 1000) {
			thlog_or_tty->print_cr("[HISTOGRAM] ADDR = %p | NAME = %s | NEW = %d | OLD = %d | TC = %d\n",
					it->first, oop(it->first)->klass()->internal_name(), std::tr1::get<0>(it->second),
					std::tr1::get<1>(it->second), std::tr1::get<2>(it->second));
		}
	}
	
	thlog_or_tty->print_cr("End_Back_Ref_Statistics\n");

	// Empty the histogram at the end of each minor gc
	histogram.clear();
}
#endif

// Add a new entry to `obj1` region dependency list that reference
// `obj2` region
void TeraHeap::group_regions(HeapWord *obj1, HeapWord *obj2){
	if (is_in_the_same_group((char *) obj1, (char *) obj2)) 
		return;
	MutexLocker x(tera_heap_group_lock);
    references((char*) obj1, (char*) obj2);
}

// Update backward reference stacks that we use in marking and pointer
// adjustment phases of major GC.
void TeraHeap::h2_push_backward_reference(void *p, oop o) {
	MutexLocker x(tera_heap_lock);
	_tc_stack.push((oop *)p);
	_tc_adjust_stack.push((oop *)p);
	
	back_ptrs_per_mgc++;

	assert(!_tc_stack.is_empty(), "Sanity Check");
	assert(!_tc_adjust_stack.is_empty(), "Sanity Check");
}

// Init the statistics counters of TeraHeap to zero when a Full GC
// starts
void TeraHeap::h2_init_stats_counters() {
	fwd_ptrs_per_fgc  = 0;	
	back_ptrs_per_fgc = 0;
	trans_per_fgc     = 0;
}

// Resets the used field of all regions in H2
void TeraHeap::h2_reset_used_field(void) {
  reset_used();
}

// Prints all the region groups
void TeraHeap::print_region_groups(void){
  print_groups();
}

void TeraHeap::h2_print_objects_per_region() {
	HeapWord *next_region;
	HeapWord *obj_addr;
	oop obj;

	start_iterate_regions();

	next_region = (HeapWord *) get_next_region();

	while(next_region != NULL) {
		obj_addr = next_region;

		while (1) {
			obj = oop(obj_addr);

			fprintf(stderr, "[PLACEMENT] OBJ = %p | RDD = %d | PART_ID = %lu\n", 
           (HeapWord *) obj, obj->get_obj_group_id(), obj->get_obj_part_id());

			if (!check_if_valid_object(obj_addr + obj->size()))
				break;

			obj_addr += obj->size();
		}

		next_region = (HeapWord *) get_next_region();
	}
}

void TeraHeap::h2_count_marked_objects(){
  HeapWord *next_region;
  HeapWord *obj_addr;
  oop obj;

  start_iterate_regions();

  next_region = (HeapWord *) get_next_region();
  int region_num = 0;
  unsigned int live_objects = 0;
  unsigned int total_objects = 0;
  while(next_region != NULL) {
    int r_live_objects = 0;
    int r_total_objects = 0;
    size_t r_live_objects_size = 0;
    size_t r_total_objects_size = 0;

    obj_addr = next_region;

    while (1) {
      obj = oop(obj_addr);
      r_total_objects++;
      r_total_objects_size += obj->size();
      total_objects++;
      if (obj->is_live()) {
        r_live_objects++;
        r_live_objects_size += obj->size();
        live_objects++;
      } 

      if (!check_if_valid_object(obj_addr + obj->size()))
        break;

      obj_addr += obj->size();
    }
    fprintf(stderr, "Region %d has %d live objects out of a total of %d\n", region_num, r_live_objects, r_total_objects);
    fprintf(stderr, "Region %d has %ld GB live objects out of a total of %ld GB\n", region_num, r_live_objects_size, r_total_objects_size);
    region_num++;
    next_region = (HeapWord *) get_next_region();
  }
  fprintf(stderr, "GLOBAL: %d live objects out of a total of %d\n", live_objects, total_objects);
}

void TeraHeap::h2_reset_marked_objects() {
  HeapWord *next_region;
  HeapWord *obj_addr;
  oop obj;

  start_iterate_regions();

  next_region = (HeapWord *) get_next_region();

  while(next_region != NULL) {
    obj_addr = next_region;

    while (1) {
      obj = oop(obj_addr);
      obj->reset_live();
      if (!check_if_valid_object(obj_addr + obj->size()))
        break;
      obj_addr += obj->size();
    }
    next_region = (HeapWord *) get_next_region();
  }
}

void TeraHeap::h2_mark_live_objects_per_region() {
  HeapWord *next_region;
  HeapWord *obj_addr;
  oop obj;

  start_iterate_regions();

  next_region = (HeapWord *) get_next_region();
  while(next_region != NULL) {
    obj_addr = next_region;

    while (1) {
      obj = oop(obj_addr);
      if (obj->is_live()) {
        obj->h2_follow_contents();
      }
      if (!check_if_valid_object(obj_addr + obj->size()))
        break;
      obj_addr += obj->size();
    }
    next_region = (HeapWord *) get_next_region();
  }
  h2_count_marked_objects();
  h2_reset_marked_objects();
}

// Frees all unused regions
void TeraHeap::free_unused_regions(void){
    struct region_list *ptr = free_regions();
    struct region_list *prev = NULL;
    while (ptr != NULL){
        start_array()->th_region_reset((HeapWord*)ptr->start,(HeapWord*)ptr->end);
        prev = ptr;
        ptr = ptr->next;
        free(prev);
    }
}

// Print the statistics of TeraHeap at the end of each FGC
// Will print:
//	- the total forward pointers from the JVM heap to the TeraHeap
//	- the total back pointers from TeraHeap to the JVM heap
//	- the total objects that has been transfered to the TeraHeap
//	- the current total size of objects in TeraHeap until
//	- the current total objects that are located in TeraHeap
void TeraHeap::h2_print_stats() {
	thlog_or_tty->print_cr("[STATISTICS] | TOTAL_FORWARD_PTRS = %lu\n", fwd_ptrs_per_fgc);
	thlog_or_tty->print_cr("[STATISTICS] | TOTAL_BACK_PTRS = %lu\n", back_ptrs_per_fgc);
	thlog_or_tty->print_cr("[STATISTICS] | TOTAL_TRANS_OBJ = %lu\n", trans_per_fgc);

	thlog_or_tty->print_cr("[STATISTICS] | TOTAL_OBJECTS  = %lu\n", total_objects);
	thlog_or_tty->print_cr("[STATISTICS] | TOTAL_OBJECTS_SIZE = %lu\n", total_objects_size);
	thlog_or_tty->print_cr("[STATISTICS] | DISTRIBUTION | B = %lu | KB = %lu | MB = %lu\n",
			obj_distr_size[0], obj_distr_size[1], obj_distr_size[2]);

#ifdef OBJ_STATS
	thlog_or_tty->print_cr("[STATISTICS] | NUM_PRIMITIVE_ARRAYS = %lu\n", num_primitive_arrays);
	thlog_or_tty->print_cr("[STATISTICS] | PRIMITIVE_ARRAYS_SIZE = %lu\n", primitive_arrays_size);
	thlog_or_tty->print_cr("[STATISTICS] | NUM_PRIMITIVE_OBJ = %lu\n", num_primitive_obj);
	thlog_or_tty->print_cr("[STATISTICS] | PRIMITIVE_OBJ_SIZE = %lu\n", primitive_obj_size);
	thlog_or_tty->print_cr("[STATISTICS] | NUM_NON_PRIMITIVE_OBJ = %lu\n", num_non_primitive_obj);
	thlog_or_tty->print_cr("[STATISTICS] | NON_PRIMITIVE_OBJ_SIZE = %lu\n", non_primitive_obj_size);
	thlog_or_tty->print_cr("[STATISTICS] | NUM_H2_PRIMITIVE_ARRAYS = %lu\n", num_h2_primitive_array);
	thlog_or_tty->print_cr("[STATISTICS] | H2_PRIMITIVE_ARRAYS_SIZE = %lu\n", h2_primitive_array_size);
  
  // Reinitializwe counters
  primitive_arrays_size = 0;
  primitive_obj_size = 0;
  non_primitive_obj_size = 0;
  num_primitive_arrays = 0;
  num_primitive_obj = 0;
  num_non_primitive_obj = 0;
  num_h2_primitive_array = 0;
  h2_primitive_array_size = 0;
  thlog_or_tty->flush();
#endif

#ifdef FWD_REF_STAT
	h2_print_fwd_ref_stat();
#endif

}

#ifdef FWD_REF_STAT
// Add a new entry to the histogram for forward reference that start from
// H1 and results in 'obj' in H2 
void TeraHeap::h2_add_fwd_ref_stat(oop obj) {
	fwd_ref_histo[obj] ++;
}

// Print the histogram
void TeraHeap::h2_print_fwd_ref_stat() {
	std::map<oop,int>::const_iterator it;

	thlog_or_tty->print_cr("Start_Fwd_Ref_Statistics\n");

	for(it = fwd_ref_histo.begin(); it != fwd_ref_histo.end(); ++it) {
		thlog_or_tty->print_cr("[FWD HISTOGRAM] ADDR = %p | NAME = %s | REF = %d\n",
				(HeapWord *)it->first, oop(it->first)->klass()->internal_name(), it->second);
	}
	
	thlog_or_tty->print_cr("End_Fwd_Ref_Statistics\n");

	// Empty the histogram at the end of each major gc
	fwd_ref_histo.clear();
}
#endif

// Pop the objects that are in `_tc_stack` and mark them as live
// object. These objects are located in the Java Heap and we need to
// ensure that they will be kept alive.
void TeraHeap::h2_mark_back_references()
{
	struct timeval start_time;
	struct timeval end_time;

	gettimeofday(&start_time, NULL);

	while (!_tc_stack.is_empty()) {
		oop* obj = _tc_stack.pop();

		if (TeraHeapStatistics)
			back_ptrs_per_fgc++;

#if defined(P_SD_BACK_REF_CLOSURE)
		MarkSweep::tera_back_ref_mark_and_push(obj);
#else
		MarkSweep::mark_and_push(obj);
#endif
	}
	
	gettimeofday(&end_time, NULL);

	if (TeraHeapStatistics)
		thlog_or_tty->print_cr("[STATISTICS] | TC_MARK = %llu\n", 
				(unsigned long long)((end_time.tv_sec - start_time.tv_sec) * 1000) + // convert to ms
				(unsigned long long)((end_time.tv_usec - start_time.tv_usec) / 1000)); // convert to ms
}

// Prints all active regions
void TeraHeap::print_h2_active_regions(void){
    print_used_regions();
}

// Adjust backwards pointers during Full GC.  
void TeraHeap::h2_adjust_back_references() {
	struct timeval start_time;
	struct timeval end_time;

	gettimeofday(&start_time, NULL);

	while (!_tc_adjust_stack.is_empty()) {
		oop* obj = _tc_adjust_stack.pop();
    enable_groups(NULL, (HeapWord*) obj);
		MarkSweep::adjust_pointer(obj);
    disable_groups();
	}
	
	gettimeofday(&end_time, NULL);

	if (TeraHeapStatistics)
		thlog_or_tty->print_cr("[STATISTICS] | TC_ADJUST %llu\n",
				(unsigned long long)((end_time.tv_sec - start_time.tv_sec) * 1000) + // convert to ms
				(unsigned long long)((end_time.tv_usec - start_time.tv_usec) / 1000)); // convert to ms
}

// Enables groupping with region of obj
void TeraHeap::enable_groups(HeapWord *old_addr, HeapWord* new_addr){
    enable_region_groups((char*) new_addr);

	obj_h1_addr = old_addr;
	obj_h2_addr = new_addr;
}

// Disables region groupping
void TeraHeap::disable_groups(void){
    disable_region_groups();

	obj_h1_addr = NULL;
	obj_h2_addr = NULL;
}

#ifdef PR_BUFFER

// Add an object 'obj' with size 'size' to the promotion buffer. 'New_adr' is
// used to know where the object will move to H2. We use promotion buffer to
// reduce the number of system calls for small sized objects.
void  TeraHeap::h2_promotion_buffer_insert(char* obj, char* new_adr, size_t size) {
	buffer_insert(obj, new_adr, size);
}

// At the end of the major GC flush and free all the promotion buffers.
void TeraHeap::h2_free_promotion_buffers() {
	free_all_buffers();
}
#endif

// Explicit (using systemcall) write 'data' with 'size' to the specific
// 'offset' in the file.
void TeraHeap::h2_write(char *data, char *offset, size_t size) {
	r_write(data, offset, size);
}

// Explicit (using systemcall) asynchronous write 'data' with 'size' to
// the specific 'offset' in the file.
void TeraHeap::h2_awrite(char *data, char *offset, size_t size) {
	r_awrite(data, offset, size);
}
		
// We need to ensure that all the writes in TeraHeap using asynchronous
// I/O have been completed succesfully.
int TeraHeap::h2_areq_completed() {
	return r_areq_completed();
}
		
// Fsync writes in TeraHeap
// We need to make an fsync when we use fastmap
void TeraHeap::h2_fsync() {
	r_fsync();
}

// Check if backward adjust stack is empty
bool TeraHeap::h2_is_empty_back_ref_stacks() {
	return _tc_adjust_stack.is_empty();
}

// Increase the number of forward references from H1 to H2
void TeraHeap::h2_increase_fwd_ref() {
	fwd_ptrs_per_fgc++;
}

// Get the group Id of the objects that belongs to this region. We
// locate the objects of the same group to the same region. We use the
// field 'p' of the object to identify in which region the object
// belongs to.
uint64_t TeraHeap::h2_get_region_groupId(void* p) {
	assert((char *) p != NULL, "Sanity check");
	return get_obj_group_id((char *) p);
}

// Get the partition Id of the objects that belongs to this region. We
// locate the objects of the same group to the same region. We use the
// field 'p' of the object to identify in which region the object
// belongs to.
uint64_t TeraHeap::h2_get_region_partId(void* p) {
	assert((char *) p != NULL, "Sanity check");
	return get_obj_part_id((char *) p);
}

// Marks the region containing obj as used
void TeraHeap::mark_used_region(HeapWord *obj) {
    mark_used((char *) obj);
#if H2_TRANSFER_STATS
  cast_to_oop(obj)->set_h2_dst_addr(544);
#endif
}

// Allocate new object 'obj' with 'size' in words in TeraHeap.
// Return the allocated 'pos' position of the object
char* TeraHeap::h2_add_object(oop obj, size_t size) {
	char *pos;			// Allocation position

	// Update Statistics
	total_objects_size += size;
	++total_objects;
	++trans_per_fgc;

	if (TeraHeapStatistics) {
		size_t obj_size = (size * HeapWordSize) / 1024UL;
		int count = 0;

		while (obj_size > 0) {
			count++;
			obj_size/=1024UL;
		}

		assert(count <=2, "Array out of range");

		++obj_distr_size[count];
	}

  pos = allocate(size, (uint64_t)obj->get_obj_group_id(), (uint64_t)obj->get_obj_part_id());

	_start_array.th_allocate_block((HeapWord *)pos);

	return pos;
}

// Wrapper function for get_region_metadata
// file: project_dir/allocator/include/segment.h
struct region* TeraHeap::get_region_meta(char* obj){
	return get_region_metadata(obj);
}

char* TeraHeap::get_h2_first_obj(){
  return get_h2_first_object();
}

size_t TeraHeap::get_h2_region_no(){
  return get_h2_region_number();
}

//returns region size
//Wrapper for region_size
// file: project_dir/allocator/include/segment.h
uint64_t TeraHeap::get_region_size(){
  return region_size();
}

// We save the current object group 'id' for tera-marked object to
// promote this 'id' to its reference objects
void TeraHeap::set_cur_obj_group_id(long int id) {
	cur_obj_group_id = id;
}

// Get the saved current object group id 
long int TeraHeap::get_cur_obj_group_id(void) {
	return cur_obj_group_id;
}

// We save the current object partition 'id' for tera-marked object to
// promote this 'id' to its reference objects
void TeraHeap::set_cur_obj_part_id(long int id) {
	cur_obj_part_id = id;
}

// Get the saved current object partition id 
long int TeraHeap::get_cur_obj_part_id(void) {
	return cur_obj_part_id;
}

// If obj is in a different H2 region than the region enabled, they
// are grouped 
void TeraHeap::group_region_enabled(HeapWord* obj, void *obj_field) {
	// Object is not going to be moved to TeraHeap
	if (obj_h2_addr == NULL) 
		return;

	if (is_obj_in_h2(oop(obj))) {
		check_for_group((char*) obj);
		return;
	}

  // If it is an already backward pointer popped from tc_adjust_stack
  // then do not mark the card as dirty because it is already marked
  // from minor gc.
	if (obj_h1_addr == NULL) 
		return;
	
  // Mark the H2 card table as dirty if obj is in H1 (backward
  // reference)
	BarrierSet* bs = Universe::heap()->barrier_set();

	if (bs->is_a(BarrierSet::ModRef)) {
		ModRefBarrierSet* modBS = (ModRefBarrierSet*)bs;

		size_t diff =  (HeapWord *)obj_field - obj_h1_addr;
		assert(diff > 0 && (diff <= (uint64_t) oop(obj_h1_addr)->size()), 
				err_msg("Diff out of range: %lu", diff));
		HeapWord *h2_obj_field = obj_h2_addr + diff;
		assert(is_field_in_h2((void *) h2_obj_field), "Shoud be in H2");

		modBS->th_write_ref_field(h2_obj_field);
	}
}

// Set non promote label value
void TeraHeap::set_non_promote_tag(long val) {
  non_promote_tag = val;
}

// Set promote tag value
void TeraHeap::set_promote_tag(long val) {
  promote_tag = val;
}

// Get non promote tag value
long TeraHeap::get_non_promote_tag() {
  return non_promote_tag;
}

// Get promote tag value
long TeraHeap::get_promote_tag() {
  return promote_tag;
}

// Promotion policy for H2 candidate objects. This function is used
// during the marking phase of the major GC. According to the policy
// that we enabled in the sharedDefines.h file we do the appropriate
// action
bool TeraHeap::h2_promotion_policy(oop obj) {
#ifdef P_NO_TRANSFER
  return false;

#elif defined(SPARK_POLICY)
  return obj->is_marked_move_h2();

#elif defined(HINT_HIGH_LOW_WATERMARK)
  // We detect high memory presure in H1 heap and we are going to find
  // the transitive closure for all marked objects
  //if (direct_promotion)
  //  return obj->is_marked_move_h2();

  //return (obj->is_marked_move_h2() && obj->get_obj_group_id() <=  promote_tag);
  return obj->is_marked_move_h2();

#elif defined(NOHINT_HIGH_WATERMARK) || defined(NOHINT_HIGH_LOW_WATERMARK)
  // We detect high memory presure in H1 heap and we are going to find
  // the transitive closure for all marked objects
  if (direct_promotion)
    return obj->is_marked_move_h2();

  return false;

#else
  return obj->is_marked_move_h2();

#endif
}

// This function determines which of the H2 candidate objects found
// during marking phase we are going to move to H2. According to the
// policy that we enabled in the sharedDefines.h file we do the
// appropriate action. This function is used only in the
// precompaction phase.
bool TeraHeap::h2_transfer_policy(oop obj) {
#ifdef P_NO_TRANSFER
	return false;

#elif defined(SPARK_POLICY)
	return obj->is_marked_move_h2();

#elif defined(HINT_HIGH_LOW_WATERMARK)
  // We detect high memory presure in H1 heap and we are going to find
  // the transitive closure for all marked objects
  if (direct_promotion) {
    if (!obj->is_marked_move_h2()) {
      return false;
    }

#ifdef P_PRIMITIVE
    if (!obj->is_primitive())
      return false;
#endif

    return check_low_promotion_threshold(obj->size());
  }

#ifdef P_PRIMITIVE
  return (obj->is_marked_move_h2() && obj->is_primitive() && obj->get_obj_group_id() <=  promote_tag);
#else
  return (obj->is_marked_move_h2() && obj->get_obj_group_id() <=  promote_tag);
#endif

#elif defined(NOHINT_HIGH_WATERMARK)
  // We detect high memory presure in H1 heap and we are going to find
  // the transitive closure for all marked objects
	if (direct_promotion)
		return obj->is_marked_move_h2();

	return false;

#elif defined(NOHINT_HIGH_LOW_WATERMARK)
  // We detect high memory presure in H1 heap and we are going to find
  // the transitive closure for all marked objects
	if (direct_promotion)
		return check_low_promotion_threshold(obj->size());

	return false;

#else
	return obj->is_marked_move_h2();
#endif
}
		
void TeraHeap::set_direct_promotion(size_t old_live, size_t max_old_gen_size) {
	direct_promotion = ((float) old_live / (float) max_old_gen_size) >= 0.85 ? true : false;
}

bool TeraHeap::is_direct_promote() {
	return direct_promotion;
}

#if defined(NOHINT_HIGH_LOW_WATERMARK) || defined(HINT_HIGH_LOW_WATERMARK)
void TeraHeap::h2_incr_total_marked_obj_size(size_t sz) {
	total_marked_obj_for_h2 += sz;
}
		
void TeraHeap::h2_reset_total_marked_obj_size() {
	total_marked_obj_for_h2 = 0;
}
		
bool TeraHeap::check_low_promotion_threshold(size_t sz) {
	if (h2_low_promotion_threshold == 0 || sz > h2_low_promotion_threshold)
		return false;

	h2_low_promotion_threshold -= sz;
	return true;
}

void TeraHeap::set_low_promotion_threshold() {
  //h2_low_promotion_threshold = total_marked_obj_for_h2 * 0.5;
  h2_low_promotion_threshold = dynamic_resizing_policy->get_h2_candidate_size() * 0.5;
}
#endif

int TeraHeap::h2_continuous_regions(HeapWord *addr){
  return get_num_of_continuous_regions((char *)addr);
}

bool TeraHeap::h2_object_starts_in_region(HeapWord *obj) {
  return object_starts_from_region((char *)obj);
}

void TeraHeap::set_obj_primitive_state(oop obj) {
  // Object is a non-primitive object. Its fields are references. Thus
  // the object has references to other objects in the heap.
  if (traced_obj_has_ref_field) {
#ifdef OBJ_STATS
    update_obj_stats(0, obj->size());
#endif
    obj->set_non_primitive();
    return;
  }

  // Track the size of H2 candidate objects in H1. Based on this size
  // we determine when to move objects to H2.
  if (DynamicHeapResizing && obj->is_marked_move_h2()) {
    dynamic_resizing_policy->increase_h2_candidate_size(obj->size());
  }

  // Object is a prrimitive array
  if (obj->is_typeArray()) {
#ifdef OBJ_STATS
    update_obj_stats(1, obj->size());
#endif
    obj->set_primitive(true);
    return;
  }
  
  // Object is a leaf object
#ifdef OBJ_STATS
    update_obj_stats(2, obj->size());
#endif
  obj->set_primitive(false);
}

#ifdef OBJ_STATS

// Update counter for objects. We divide objects into three categories
// - primitive arrays
// - leaf objects which are the objects with only primitive type fields
// - non-primitive objets which are the objects with reference fields
void TeraHeap::update_obj_stats(int type, size_t size) {
  if (!TeraHeapStatistics)
    return;

  switch (type) {
    case 0: // Non-primitive objects
      non_primitive_obj_size += size;
      num_non_primitive_obj++;
    break;

    case 1: // Primitive array obects
      primitive_arrays_size += size;
      num_primitive_arrays++;
    break;

    case 2: // Leaf objects
      primitive_obj_size += size;
      num_primitive_obj++;
    break;
  }
}

// Update counter for object H2 objects 
void TeraHeap::update_stats_h2_primitive_arrays(size_t size) {
  num_h2_primitive_array++;
  h2_primitive_array_size += size;
}
#endif
