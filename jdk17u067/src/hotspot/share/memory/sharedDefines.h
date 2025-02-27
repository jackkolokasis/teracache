/**************************************************
 *
 * file: sharedDefines.h
 *
 * @Author:   Iacovos G. Kolokasis
 * @Version:  01-12-2022
 * @email:    kolokasis@ics.forth.gr
 *
 ***************************************************
 */

#ifndef SHARE_MEMORY_SHAREDDEFINES_H
#define SHARE_MEMORY_SHAREDDEFINES_H

/************************************
 * Source code that we need to remove after testing
 ************************************/
#define H1_CARD_SIZE ((size_t) (1 << 9))
#define H2_CARD_SIZE ((size_t) (1 << 13))
#define PAGE_SIZE ((size_t)sysconf(_SC_PAGESIZE))
#define H1_ALIGNMENT H1_CARD_SIZE * PAGE_SIZE
#define H2_ALIGNMENT H2_CARD_SIZE * PAGE_SIZE
#define CONVERT_TO_GB(bytes) bytes >> 30
#define CONVERT_TO_MB(bytes) bytes >> 20
#define CONVERT_TO_KB(bytes) bytes >> 10


#define TERA_LOG				         //< Define logging for TeraHeap

#define TERA_FLAG				         //< Define teraFlag word

#define TERA_MINOR_GC            //< Enable Teraheap code for minor GC

#define TERA_CARDS               //< Enable Teraheap card table

//#define BACK_REF_STAT          //< Collect statistics for backward
                                 //refenrences. Works only with -XX:GCThreads=1

//#define FMAP_HYBRID				       //< When we use fastmap hybrid version we
									               // employ huge pages for mutator threads and
									               // regular pages for GC 

#define MADVISE_ON				        //< During minor gc we advise kernel for
									                // random accesses. During mutator thread
									                // execution we advise kernel for sequential
									                // accesses

#define TERA_CARD_SIZE			 13   // This is the size of each card in
                                  // TeraCache card table. The size is in bit
                                  // e.g 9 = 512bytes

#define TERA_INTERPRETER	        //< Enable Interpreter to support TeraHeap
//#define TERA_PARALLEL_H2_SUMMARY_PHASE
#define TERA_C1				            //< Enable C1 to support TeraHeap

#define TERA_C2				            //< Enable C1 to support TeraHeap

//#define C2_ONLY_LEAF_CALL		    //< C2 Compiler version - Comparisons and
                                  // card marking are all implemented in the
                                  // make_leaf_call()

#define TERA_MAJOR_GC             // Enable TeraHeap in Parallel
                                  // Scavenge single threaded version
                                  // MajorGC

//#define TEST_CLONE                // Clone objects

#define DISABLE_TRAVERSE_OLD_GEN  //< Disable backward reference traversal
									                // from H2 to old generation (H1) during
									                // minor GC

//#define TERA_ASSERT               // Extended assertions for TeraHeap

//#define FASTMAP                 // Enable this define when you run
                                  // with fastmap with enabled
                                  // -XX:AllocateHeapAt="/mnt/dir"
                                  // or -XX:AllocateOldGenAt="/mnt/dir"

#define DO_NOT_UNLOAD_CLASSES     // Prevent klass unloading and their methods


/**********************************
 * Write Mode to H2
 **********************************/
//#define SYNC				            //< Enable explicit I/O path for the writes
                                  // in TeraHeap during major GC

#define ASYNC				              //< Asynchronous I/O path for the writes in
                                  // TeraHeap

#define PR_BUFFER			            //< Enable promotion buffer for async I/O to
                                  // reduce the number of system calls 

//#define FMAP				              //< When we use fastmap we need to ensure
                                  // that all the writes in buffered cached
                                  // will be flushed to the device because the
                                  // memory of fast map is different from
                                  // buffer cache. 

/**********************************
 * Statistics
 **********************************/
//#define FWD_REF_STAT               //< Collect statistics for class object

// #define TERA_TIMERS             //< Enable timers for performance
                                   // analysis

//#define TERA_STATS                //< Statistics for objects in H2

//#define OBJ_STATS                 //< Take object statistics about
                                  //primitive types and non primitive
                                  //types. This work with TERA_STATS

/**********************************
 * States of TeraFlag  
 **********************************/
#define MOVE_TO_TERA			255	    //< Move this object to tera cache

#define TERA_TO_OLD		    328	    //< Pointer from TeraCache to Old Gen. Move
                                  // this object to TeraCache

#define IN_TERA_HEAP     2147483561	//< This object is located in TeraCache

#define INIT_TF				    2035	  //< Initial object state

#define INIT_TF_HEX			  0x7f3U  //< Initial object state

#define LIVE_TERA_OBJ     202     //< Object marked as live during GC Analysis

#define VISITED_TERA_OBJ  203     //< Object visited during GC Analysis

#define PRIMITIVE_ARRAY   529     //< Object is primitive array

#define LEAF_OBJECT       535     //< Leaf object (with only primitive fields)

#define NON_PRIMITIVE     419     //< Non primitive object

#define STATIC_OBJ        938     //< Static field

/**********************************
 * Policies for TeraCache
 **********************************/
#define P_SD_EXCLUDE_CLOSURE	 	  //< Exclude objects from the closure

#define P_SD_REF_EXCLUDE_CLOSURE  //< Exclude reference objects from the closure

#define DYNAMIC_HEAP_RESIZING_TEST // Dynamic heap resizing

// State machine flags for DynaHeap
#define PER_MINOR_GC               //< Take decisions per minor GCs

#define GROW_STEP (0.2)            //< 0.2 (progressive) 0.8 (aggressive)

#define SHRINK_STEP (0.8)          //< 0.8 (progressive) 0.2 (aggressive)

#define LAZY_MOVE_H2               //< Wait the next GC for moving
                                   //objects to H2. Until next GC wait
                                   //and do not take new decissions.

#define TERA_CONTROLLER

#endif  // SHARE_MEMORY_SHAREDDEFINES_H
