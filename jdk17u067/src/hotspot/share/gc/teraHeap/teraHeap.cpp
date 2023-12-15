#include "gc/teraHeap/teraHeap.hpp"
#include "gc/parallel/psVirtualspace.hpp"
#include "gc/parallel/psVirtualspace.hpp"
#include "memory/memRegion.hpp"
#include "memory/sharedDefines.h"
#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/mutexLocker.hpp"
#include <mutex>
#include <tera_allocator.h>

std::mutex increment_lock;

char *TeraHeap::_start_addr = NULL;
char *TeraHeap::_stop_addr = NULL;

Stack<oop *, mtGC> TeraHeap::_tc_stack;
Stack<oop *, mtGC> TeraHeap::_tc_adjust_stack;

long int TeraHeap::cur_obj_group_id;
long int TeraHeap::cur_obj_part_id;

// Constructor of TeraHeap
TeraHeap::TeraHeap() {

  uint64_t align = CardTable::th_ct_max_alignment_constraint();
  init(align);

  _start_addr = start_addr_mem_pool();
  _stop_addr = stop_addr_mem_pool();

  cur_obj_group_id = 0;

  obj_h1_addr = NULL;
  obj_h2_addr = NULL;

  tera_policy = create_transfer_policy();

#ifdef TERA_TIMERS
  teraTimer = new TeraTimers();
#endif

#ifdef TERA_STATS
  tera_stats = new TeraStatistics();
#endif
}

TeraHeap::~TeraHeap() {
  delete tera_policy;
#ifdef TERA_TIMERS
  delete teraTimer;
#endif

#ifdef TERA_STATS
  delete tera_stats;
#endif
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
	return (cast_from_oop<HeapWord *>(ptr) >= (HeapWord *)_start_addr)     // if greater than start address
			&& (cast_from_oop<HeapWord *>(ptr) < (HeapWord *)_stop_addr);  // if smaller than stop address
}

// Check if an object `p` belongs to TeraHeap. If the object bolongs to
// TeraHeap then the function returns true, either it returns false.
bool TeraHeap::is_in_h2(HeapWord *p) {
	return p >= (HeapWord *)_start_addr && p < (HeapWord *)_stop_addr;
}

// Check if an object `p` belongs to TeraHeap. If the object bolongs to
// TeraHeap then the function returns true, either it returns false.
bool TeraHeap::is_in_h2(const void* p) {
	const char* cp = (char *)p;
	return cp >= _start_addr && cp < _stop_addr;
}

// Check if an object `p` belongs to TeraHeap. If the object bolongs to
// TeraHeap then the function returns true, either it returns false.
bool TeraHeap::is_field_in_h2(void *p) {
	char* const cp = (char *)p;
	return cp >= _start_addr && cp < _stop_addr;
}
  
// Deallocate the backward references stacks
void TeraHeap::h2_clear_back_ref_stacks() {
	_tc_adjust_stack.clear(true);
	_tc_stack.clear(true);
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
	
	assert(!_tc_stack.is_empty(), "Sanity Check");
	assert(!_tc_adjust_stack.is_empty(), "Sanity Check");
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
			obj = cast_to_oop(obj_addr);

			fprintf(stderr, "[PLACEMENT] OBJ = %p | RDD = %d | PART_ID = %lu\n", 
           cast_from_oop<HeapWord *>(obj), obj->get_obj_group_id(), obj->get_obj_part_id());

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
      obj = cast_to_oop(obj_addr);
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
      obj = cast_to_oop(obj_addr);
      obj->reset_live();
      if (!check_if_valid_object(obj_addr + obj->size()))
        break;
      obj_addr += obj->size();
    }
    next_region = (HeapWord *) get_next_region();
  }
}

// Create a transfer policy for moving ojects from H1 to H2
TransferPolicy* TeraHeap::create_transfer_policy() {
  if (!strcmp(TeraHeapPolicy, "SparkPrimitivePolicy"))
    return new SparkPrimitivePolicy();

  if (!strcmp(TeraHeapPolicy, "HintHighLowWatermarkPolicy"))
    return new HintHighLowWatermarkPolicy();

  if (!strcmp(TeraHeapPolicy, "HintHighLowWatermarkPrimitivePolicy"))
    return new HintHighLowWatermarkPrimitivePolicy();
  
  return new DefaultPolicy();
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
      obj = cast_to_oop(obj_addr);
      if (obj->is_live()) {
        //obj->h2_follow_contents();
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
        _start_array.th_region_reset((HeapWord*)ptr->start,(HeapWord*)ptr->end);
        prev = ptr;
        ptr = ptr->next;
        free(prev);
    }
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
oop* TeraHeap::h2_get_next_back_reference() {
  return (_tc_stack.is_empty() ? NULL : _tc_stack.pop());
}

// Prints all active regions
void TeraHeap::print_h2_active_regions(void){
    print_used_regions();
}

// Get the next backward reference from the stack to adjust
oop* TeraHeap::h2_adjust_next_back_reference() {
  return (!_tc_adjust_stack.is_empty() ? _tc_adjust_stack.pop() : NULL);
}

// Check if the collector transfers and adjust H2 candidate objects.
bool TeraHeap::compact_h2_candidate_obj_enabled() {
  return obj_h1_addr != NULL;
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
void TeraHeap::mark_used_region(HeapWord *obj, size_t thread_worker_no) {
  mark_used((char *) obj);
  increment_region_rc(obj, thread_worker_no);

#if H2_TRANSFER_STATS
  cast_to_oop(obj)->set_h2_dst_addr(544);
#endif

  if (H2LivenessAnalysis)
    cast_to_oop(obj)->set_live();
}

void TeraHeap::mark_used_region_no_increment(HeapWord *obj) {
  mark_used((char *) obj);

  if (H2LivenessAnalysis)
    cast_to_oop(obj)->set_live();
}

// Allocate new object 'obj' with 'size' in words in TeraHeap.
// Return the allocated 'pos' position of the object
char* TeraHeap::h2_add_object(oop obj, size_t size) {
	char *pos;			// Allocation position

#ifdef TERA_STATS
  tera_stats->add_object(size);
  tera_stats->update_object_distribution(size);
#endif

  pos = allocate(size, (uint64_t)obj->get_obj_group_id(), (uint64_t)obj->get_obj_part_id());

  mark_for_transfer_to_H2((HeapWord *)pos);

	_start_array.th_allocate_block((HeapWord *)pos);

	return pos;
}

// If obj is in a different H2 region than the region enabled, they
// are grouped 
void TeraHeap::group_region_enabled(HeapWord* obj, void *obj_field) {
	// Object is not going to be moved to TeraHeap
	if (obj_h2_addr == NULL) 
		return;

	if (is_obj_in_h2(cast_to_oop(obj))) {
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
	BarrierSet* bs = BarrierSet::barrier_set(); //refhere
  CardTableBarrierSet* ctbs = barrier_set_cast<CardTableBarrierSet>(bs);
  CardTable* ct = ctbs->card_table();

  size_t diff =  (HeapWord *)obj_field - obj_h1_addr;
  assert(diff > 0 && (diff <= (uint64_t) cast_to_oop(obj_h1_addr)->size()),
         "Diff out of range: %lu", diff);
  HeapWord *h2_obj_field = obj_h2_addr + diff;
  assert(is_field_in_h2((void *) h2_obj_field), "Shoud be in H2");

  ct->th_write_ref_field(h2_obj_field);
}
  
// Check if the object `obj` is an instance of the following
// metadata class:
// - Instance Mirror Klass
// - Instance Reference Klass
// - Instance Class Loader Klass
// If yes return true, otherwise false
bool TeraHeap::is_metadata(oop obj) {
  if (obj->klass()->is_instance_klass()) {
    InstanceKlass *ik = (InstanceKlass *) obj->klass();
    return (ik->is_mirror_instance_klass() || ik->is_reference_instance_klass() || ik->is_class_loader_instance_klass());
  }

  if (obj->is_objArray()) {
    objArrayOop arr = objArrayOop(obj);
    if (arr->element_klass()->is_instance_klass()) {
      InstanceKlass *ik = (InstanceKlass *) arr->element_klass();
      return (ik->is_mirror_instance_klass() || ik->is_reference_instance_klass() || ik->is_class_loader_instance_klass());
    }
  }

  return false;
}

int TeraHeap::h2_continuous_regions(HeapWord *addr){
  assert(is_in_h2(addr), "Error");
  return get_num_of_continuous_regions((char *)addr);
}

bool TeraHeap::h2_object_starts_in_region(HeapWord *obj) {
  return object_starts_from_region((char *)obj);
}

// Move object with size 'size' from source address 'src' to the h2
// destination address 'dst' 
void TeraHeap::h2_move_obj(HeapWord *src, HeapWord *dst, size_t size) {
  assert(src != NULL, "Src address should not be null");
  assert(dst != NULL, "Dst address should not be null");
  assert(size > 0, "Size should not be zero");

#if defined(SYNC)
  h2_write((char *)src, (char *)dst, size);
#elif defined(FMAP)
  h2_write((char *)src, (char *)dst, size);
#elif defined(ASYNC) && defined(PR_BUFFER)
  h2_promotion_buffer_insert((char *)src, (char *)dst, size);
#elif defined(ASYNC) && !defined(PR_BUFFER)
  h2_awrite((char *)src, (char *)dst, size);
#else
  // We use memcpy instead of memmove to avoid the extra copy of the
  // data in the buffer.
  memcpy(dst, src, size * 8);
#endif // SYNC
}

// Complete the transfer of the objects in H2
void TeraHeap::h2_complete_transfers() {
#if defined(ASYNC) && defined(PR_BUFFER)
  h2_free_promotion_buffers();
  while(!h2_areq_completed());
#elif defined(ASYNC) && !defined(PR_BUFFER)
  while(!h2_areq_completed());
#elif defined(FMAP)
  h2_fsync();
#endif
}
  
// Check if the group of regions in H2 is enabled
bool TeraHeap::is_h2_group_enabled() {
  return (obj_h1_addr != NULL  || obj_h2_addr != NULL);
}

//returns region size
//Wrapper for region_size
// file: project_dir/allocator/include/segment.h
uint64_t TeraHeap::get_region_size(){
  return region_size();
}

// Wrapper function for increment_ref_counter
// file: project_dir/allocator/include/segment.h
void TeraHeap::increment_region_rc(HeapWord* obj, unsigned worker_id){
	increment_ref_counter((char*)obj, worker_id);
}

// Wrapper function for get_ref_counter_sum
// file: project_dir/allocator/include/segment.h
long unsigned TeraHeap::get_region_rc(struct region* reg){
	return get_ref_counter_sum(reg);
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

// checks that ref counters have been reset
void TeraHeap::validate_rc_reset(){
  check_if_ref_reset();
}

// returns the H2 region number
size_t TeraHeap::get_region_number(char* obj){
  return get_region_index(obj);
}

// Reads from nvme the region and copies it to the BUFFER
// Returns 0 on success and negative int on error
int TeraHeap::get_region_copy(struct region* reg_meta, char* BUFFER){
  return copy_region(reg_meta, BUFFER);
}

void TeraHeap::mark_for_transfer_to_H2(HeapWord* h2_destination_address){
  mark_for_transfer((char*)h2_destination_address);
}

//prints ref_counters and destination addresses for
//all regions that are not empty
//Wrapper function for print_regions_metadata
// file: project_dir/allocator/include/segment.h
void TeraHeap::h2_print_reg_metadata(FILE* stream){
	print_regions_metadata(stream);
}

//returns the N most underpopulated regions
//Argument: the max amount of regions that will be returned
//Wrapper for get_underpopulated_regions
// file: project_dir/allocator/include/segment.h
struct underpopulated_regions* TeraHeap::get_underpopulated_regs(unsigned region_amount){
	return get_underpopulated_regions(region_amount);
}

void TeraHeap::free_uregions(struct underpopulated_regions* uregions){
  free_underpopulated_regions(uregions);
}

//sets the destination address of the region
//Argument: the region and the new destination address
//Wrapper for set_destination_address
// file: project_dir/allocator/include/segment.h
void TeraHeap::set_destination_addr(struct region* reg, uint64_t new_addr){
	set_destination_address(reg, new_addr);
}

//returns the objects offser from the start of the region
//Argument: the address of the object
//Wrapper for calculate_obj_offset
// file: project_dir/allocator/include/segment.h
off_t TeraHeap::h2_get_offset(char* obj){
	return calculate_obj_offset(obj);
}

#ifdef TERA_TIMERS
// Tera timers maintains timers for the different phases of the
// major GC
TeraTimers* TeraHeap::getTeraTimer() {
  return teraTimer;
}
#endif

#ifdef TERA_STATS
// Tera statistics for objects that we move to H2, forward references,
// and backward references.
TeraStatistics* TeraHeap::get_tera_stats() {
  return tera_stats;
}
#endif

// Init the allocator to reserve dram space for the allocation of
// forwarding tables 
void TeraHeap::init_tera_dram_allocator(uint64_t entries) {
  init_arena(entries);
}

// Destroy the reserved dram space
void TeraHeap::destroy_tera_dram_allocator() {
  free_arena();
}

