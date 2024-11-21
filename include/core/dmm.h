/* adjusted from https://github.com/sandmman/Heap-Manager
 */
#ifndef __CPS210_MM_H__ 	/* check if this header file is already defined elsewhere */
#define __CPS210_MM_H__


/* You do not need to change MAX_HEAP_SIZE
 */
//#define MAX_HEAP_SIZE	(1024*1024*32) /* max size restricted to 32 MB */
//#define MAX_HEAP_SIZE	(1024*1024*4) /* max size restricted to 4MB, recommended setting for test_stress2 */
//#define MAX_HEAP_SIZE	(1024) /* max size restricted to 1kB*/

/* On 32-bit machines, change this to 4 */
#define WORD_SIZE	8

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 	WORD_SIZE	/* typically, single word on 32-bit systems and double word on 64-bit systems */

/* rounds up to the nearest multiple of ALIGNMENT */
/* Q: what is the result of x & ~x? */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

#define SIZE_T_ALIGNED (ALIGN(sizeof(size_t)))

#define METADATA_T_ALIGNED (ALIGN(sizeof(metadata_t)))

#ifdef NDEBUG
	#define DEBUG(M, ...)
	#define PRINT_FREELIST print_freelist
#else
	#define DEBUG(M, ...) fprintf(stderr, "[DEBUG] %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
	#define PRINT_FREELIST
#endif

typedef enum{false, true} bool;

bool dmalloc_init(void *memory_chunk, long max_bytes);
void *dmalloc(size_t numbytes);
void dfree(void *allocptr);


void print_freelist(); /* optional for debugging */

#endif /* end of __CPS210_MM_H__ */
