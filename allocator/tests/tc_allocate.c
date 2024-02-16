
/***************************************************
*
* file: tc_allocate.c
*
* @Author:   Iacovos G. Kolokasis
* @Author:   Giannos Evdorou
* @Version:  09-03-2021
* @email:    kolokasis@ics.forth.gr
*
* Test to verify:
*       - allocator initialization
*       - object allocation in the correct positions
***************************************************/

#include <stdint.h>
#include <stdio.h>
#include "../include/sharedDefines.h"
#include "../include/regions.h"
#include "../include/segments.h"

#define CARD_SIZE ((uint64_t) (1 << 9))
#define PAGE_SIZE ((uint64_t) (1 << 12))

void print_ref_counter(char *obj, int obj_num){
	struct region* curr_region = get_region_metadata(obj);
	printf("Region of obj%-3d :: region_reference_counter = %lu\n", obj_num, get_ref_counter_sum(curr_region->start_address));
	printf("---for each worker\n");
	for (unsigned i=0; i<WORKER_THREADS_NUM; i++){
		printf("\tWorker %u: refs = %lu\n", i, curr_region->ref_counter[i]);
	}
  printf("Offset of obj%-3d :: %" PRIu64 "\n", obj_num, calculate_obj_offset(obj));
}

//this test needs 256MB region size
int main() {
    char *obj1, *obj2, *obj3, *obj4, *obj5, *obj6, *obj7, *obj8, *obj9;

    // Init allocator
    init(CARD_SIZE * PAGE_SIZE);

    //obj1 should be in region 0
    obj1 = allocate(1, 0, 0);
    fprintf(stderr, "Allocate: %p\n", obj1);
    assertf((obj1 - start_addr_mem_pool()) == 0, "Object start position");

    //obj2 should be in region 1 
    obj2 = allocate(200, 1, 0);
    fprintf(stderr, "Allocate: %p\n", obj2);
//    assertf((obj2 - obj1)/8 == 33554432, "Object start position %zu", (obj2 - obj1) / 8); 

    //obj3 should be in region 0
    obj3 = allocate(12020, 0, 0);
    fprintf(stderr, "Allocate: %p\n", obj3);
  //  assertf((obj3 - obj1)/8 == 1, "Object start position");

    //obj4 should be in region 2 
    obj4 = allocate(262140, 2, 0);
    fprintf(stderr, "Allocate: %p\n", obj4);
    //assertf((obj4 - obj2)/8 == 33554432, "Object start position %zu", (obj4 - obj2) / 8);

    //obj5 should be in region 1
    obj5 = allocate(4, 1, 0);
    fprintf(stderr, "Allocate: %p\n", obj5);
    //assertf((obj5 - obj2)/8 == 200, "Object start position");

    //obj6 should be in region 0 
    obj6 = allocate(200, 0, 0);
    fprintf(stderr, "Allocate: %p\n", obj6);
   // assertf((obj6 - obj3)/8 == 12020, "Object start position");
	
    //obj7 should be in region 3 
    obj7 = allocate(262140, 3, 0);
    fprintf(stderr, "Allocate: %p\n", obj7);
    //assertf((obj7 - obj5)/8 == 4, "Object start position %zu", (obj7 - obj5) / 8);

    //obj8 should be in region 4 
    obj8 = allocate(500, 4, 0);
    fprintf(stderr, "Allocate: %p\n", obj8);
   // assertf((obj8 - obj4)/8 == 33554432, "Object start position");

    //obj9 should be in region 5 
    obj9 = allocate(500, 5, 0);
    fprintf(stderr, "Allocate: %p\n", obj9);
   // assertf((obj9 - obj4)/8 == 262140, "Object start position");

	
	printf("--------------------------------------\n");
	printf("TC_Allocate:\t\t\t\033[1;32m[PASS]\033[0m\n");
	printf("--------------------------------------\n");

  
  print_regions_metadata(stdout);

  printf("REGION_SIZE = %" PRIu64 "\n", REGION_SIZE);

	return 0;
}


