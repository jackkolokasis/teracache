#ifndef __SHAREDDEFINES_H__
#define __SHAREDDEFINES_H__

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

//#define DEV "/mnt/fmap/file.txt"	     //< Device name
//#define DEV_SIZE (700*1024LU*1024*1024)  //< Device size (in bytes)
extern char dev[150];
extern uint64_t dev_size;

//#define ASSERT

#ifdef ASSERT
#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_error(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define assertf(A, M, ...) if(!(A)) {log_error(M, ##__VA_ARGS__); assert(A);}
#else
#define assertf(A, M, ...) ;
#endif

//#define ANONYMOUS             //< Set to 1 for small mmaps

#define MAX_REQS	 64				  //< Maximum requests

#define BUFFER_SIZE  (8*1024LU*1024)  //< Buffer Size (in bytes) for async I/O

#define MALLOC_ON	1				  //< Allocate buffers dynamically

#define REGION_SIZE	(256*1024LU*1024) //< Region size (in bytes) for allignment
									                    // version

extern uint64_t region_array_size;

#define MAX_PARTITIONS 256			  // Maximum partitions per RDD, affects 
									  // id array size
extern uint64_t max_rdd_id;

extern uint64_t group_array_size;

#define STATISTICS 0				  //< Enable allocator to print statistics

#define DEBUG_PRINT 0			      //< Enable debug prints

#endif
