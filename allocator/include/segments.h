#ifndef __SEGMENTS_H__
#define __SEGMENTS_H__

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>

#define ANONYMOUS 0
#define PR_BUFFER 1						
#define PR_BUFFER_SIZE (2*1024LU*1024) /* Promotion buffer size */
#define HeapWordSize 8				   /* Java heap allignment */
/* Objects that are grater than this threshold we write them directly using
 * async I/O. For objects less than this threshold we use the promotion buffer.
 * THRESHOLD should always be less than the PR_BUFFER_SIZE*/
#define THRESHOLD (1*1024LU*1024)	   
#define WORKER_THREADS_NUM 8

struct offset{
  uint64_t offset;
  struct offset *next;
};

#if PR_BUFFER
/* We use promotion buffer in each region to reduce the number of system calls
 * for small sized objects.
 */
struct pr_buffer {
	char *buffer;					 /* Allocation buffer */
	char *first_obj_addr;			 /* First object address in region */
	char *alloc_ptr;			     /* Allocation pointer for the buffer */
	size_t size;					 /* Current size of the buffer */
};
#endif

/*
 * The struct for group array
 */
struct group{
    struct region *region;
    struct group *next;
};

/*
 * The struct for regions
 */
struct region{
    char *start_address;
    char *last_allocated_end;
    char *last_allocated_start;
    char *first_allocated_start;
    struct group *dependency_list;
    long unsigned ref_counter[8];
    uint64_t destination_address;
    uint64_t diff;
    bool underTransfer;
    bool move_flg;
#if ANONYMOUS
  struct offset *offset_list;
  size_t size_mapped; 
#endif
#if PR_BUFFER
    struct pr_buffer *pr_buffer;
#endif
    int8_t used;
    uint32_t rdd_id;
    uint32_t part_id;
};

/*
 * An array of N pointers to underpopulated regions that should be moved to H1
 */
struct underpopulated_regions{
    struct region **move_regions_list;
    size_t size;
    size_t capacity;
    long unsigned max_references;
    long unsigned min_references;
};

/*
 * Will return an underpopulated_regions struct that will
 * contain the 'amount_of_regions' most underpopulated regions
 * Arguments: amount_of_regions: the struct will cointain at most
 * the 'amount_of_regions' most underpopulated regions
 */
struct underpopulated_regions* get_underpopulated_regions(long unsigned amount_of_regions);

/*
 * Frees underpopulated_regions struct
 */
void free_underpopulated_regions(struct underpopulated_regions *ptr);

/*
 * Initialize region array, group array and their fields
 */
void init_regions();

/*
 * Finds an empty regions and returns its starting address
 * Arguments: size: the size of the object we want to allocate in
 * Bytes
 */
char* new_region(size_t size);

/*
 * Returns the address of the allocated object
 * Arguments: size: the size of the object in Bytes
 * rdd_id: The id of the rdd which the object belongs
 * part_id: The id of the partition that the object belongs
 */
char* allocate_to_region(size_t size, uint64_t rdd_id, uint64_t partition_id);

uint64_t get_id(uint64_t rdd_id, uint64_t partition_id);

/*
 * Returns an empty position of the group_array
 */
int new_group();

/*
 * Merges two groups of regions that already exist
 * Arguments: group1: the id of the first group
 * group2:the id of the second group
 */
void merge_groups(int group1, int group2);

/*
 * Connects two regions in a group
 * Arguments: obj1: the object that references the other
 * obj2: the object that is referenced (order does not matter)
 */
void references(char *obj1, char *obj2);

/*
 * Returns the size of the region
 */
uint64_t region_size();

/*
 * Returns the index of the region of the object
 */
size_t get_region_index(char* obj);

/*
 * Used for debugging, checks that all ref counters are 0
 */
void check_if_ref_reset();

/*
 * Sums the ref_counter array  of a region and returns it
 * Arguments: region whose ref_counter sum is needed
 */
long unsigned get_ref_counter_sum(struct region* reg);

/*
 * Increases the objects's region reference counter
 * Arguments: obj: Object whose region reference counter will be incremented
 */
void increment_ref_counter(char *obj, unsigned worker_id);

/*
 * Returns the offset of the object from ste start of the region
 */
off_t calculate_obj_offset(char* obj);

/*
 * Returns the metadata of the objects's region
 * Arguments: obj: Object whose region metadata  will be returned
 */
struct region* get_region_metadata(char *obj);

/*
 * Sets the destination address of the object's region,
 * if it is not already set
 * Arguments: obj: Object whose region destination address will be set
 */
void set_destination_address(struct region* reg, uint64_t destination_address);

/*
 * Returns the destination address of the object's region,
 * Arguments: obj: Object whose region destination address will be returned
 */
uint64_t get_destination_address(struct region *reg);

/*
 * Prints all regions and certain metadata
 */
void print_regions_metadata(FILE* stream);

/*
 * Prints all the region groups that contain something
 */
void print_groups();

/*
 * Resets the used field of all regions, and groups
 */
void reset_used();

/*
 * Marks the region that contains this obj as used and increases group
 * counter (if it belongs to a group)
 * Arguments: obj:the object that is alive
 */
void mark_used(char *obj);

/*
 * Frees all unused regions
 */
struct region_list* free_regions();

/**
 * Get the total number of allocated regions
 * Return the total number of allocated regions or zero, otherwise
 */
long total_allocated_regions();

/*
 * Prints all the allocated regions
 */
void print_regions();

/*
 * Get the total number of used regions
 * Return the number of used regions or zero, otherwise
 */
long total_used_regions();

/*
 * Prints all the used regions
 */
void print_used_regions();

/*
 * Causes I/O operation to nvme device and copies a region from H2 to the buffer
 * Arguments: Region to be copied, and a BUFFER of size REGION_SIZE
 * Return:    zero on success, negative on failure 
 */
int copy_region(struct region* reg, char* BUFFER);

/*
 * Marks a region where objects will arrive in the current gc from the H1 Heap
 */
void mark_for_transfer(char* destination_address);

/*
 * Checks if address of obj is before last object
 */
bool is_before_last_object(char *obj);

/*
 * Returns last object of region
 */
char* get_last_object(char *obj);

/*
 * Returns true if object is first of its region, false otherwise
 */
bool is_region_start(char *obj);

/*
 * Enables groupping with the region in which obj belongs to
 */
void enable_region_groups(char *obj);

/*
 * Disables groupping with the region previously enabled
 */
void disable_region_groups(void);

/*
 * function that connects two regions in a group
 * arguments: obj: the object that must be checked to be groupped with the region_enabled
 */
void check_for_group(char *obj);

void print_objects_temporary_function(char *obj,const char *string);

/*
 * Start iteration over all active regions to print their object state
 */
void start_iterate_regions(void);

/*
 * Get the next active region
 */
char* get_next_region(void);

char *get_first_object(char *addr);

/*
 * Get objects 'obj' region start address
 */
char* get_region_start_addr(char *obj, uint64_t rdd_id, uint64_t part_id);

/*
 * Get object 'groupId' (RDD Id). Each object is allocated based on a group Id
 * and the partition Id that locates in teraflag.
 *
 * @obj: address of the object
 *
 * Return: the object partition Id
 */
uint64_t get_obj_group_id(char *obj);

/*
 * Get object 'groupId'. Each object is allocated based on a group Id
 * and the partition Id that locates in teraflag.
 *
 * obj: address of the object
 *
 * returns: the object partition Id
 */
uint64_t get_obj_part_id(char *obj);

/*
 * Check if these two objects belong to the same group
 *
 * obj1: address of the object
 * obj2: address of the object
 *
 * returns: 1 if objects are in the same group, 0 otherwise
 */
int is_in_the_same_group(char *obj1, char *obj2);

/*
 * Get number of continious regions in H2
 *
 * addr: address of the object
 */
int get_num_of_continuous_regions(char *addr);

/*
 * Check if the objects starts from existing region
 * 
 * obj: object address
 *
 * returns: true if starts and false, otherwise
 *
 */
bool object_starts_from_region(char *obj);

#if PR_BUFFER
/*
 * Add an obect to the promotion buffer. We use promotion buffer to avoid write
 * system calls for small sized objects.
 *
 * obj: Object that will be writter in the promotion buffer
 * new_adr: Is used to know where the first object in the promotion buffer will
 *			be move to H2
 * size: Size of the object
 */
void buffer_insert(char* obj, char* new_adr, size_t size);

/*
 * Flush all active buffers and free each buffer memory. We need to free their
 * memory to limit waste space.
 */
void free_all_buffers();

#endif

#endif
