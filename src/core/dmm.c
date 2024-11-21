/* adjusted from https://github.com/sandmman/Heap-Manager
 */
#include <stdio.h> //needed for long
#include <unistd.h> //needed for sbrk
#include <assert.h> //For asserts
#include "dmm.h"

/* You can improve the below metadata structure using the concepts from Bryant
 * and OHallaron book (chapter 9).
 */

typedef struct metadata {
       /* long is the return type of the sizeof operator. Since the size of
 	* an object depends on the architecture and its implementation, long
	* is used to represent the maximum size of any object in the particular
 	* implementation. size contains the size of the data object or the
	* amount of free bytes
	*/
	size_t size;						//Size does not include the header or the footer
	struct metadata* next;
	struct metadata* prev; //What's the use of prev pointer?
} metadata_t;

typedef struct footer {
	size_t size;
} foot_t;

/* freelist maintains all the blocks which are not in use; freelist is kept
 * always sorted to improve the efficiency of coalescing
 */
static metadata_t *blocks[10] = { NULL }; //Each location is assigned a hex value 1,8,16,64,256
static void* slabStart = NULL;
static void* slabEnd = NULL;
static int unallocated = 1;
#define FOOT_T_ALIGNED (ALIGN(sizeof(foot_t)))

///////////////////////////
////Utility Functions//////
//////////////////////////

//Finds the index of appropriate freelist//
int convertToIndex(int numbytes){
	if(numbytes/WORD_SIZE < 8){ return 0;} //64 bytes
	if(numbytes/WORD_SIZE < 16){ return 1;}//128
	if(numbytes/WORD_SIZE < 64){ return 2;}//512
	if(numbytes/WORD_SIZE < 256){ return 3;}//2048
	if(numbytes/WORD_SIZE < 512){ return 4;} //64 bytes
	if(numbytes/WORD_SIZE < 1028){ return 5;}
	if(numbytes/WORD_SIZE < 2048){ return 6;}
	if(numbytes/WORD_SIZE < 4096){ return 7;}
	if(numbytes/WORD_SIZE < 9128){ return 8;}
	return 9;
}
// Removes the given block from blocks[index]//
void removeFromList(metadata_t* ptr,int index){
	if(ptr == blocks[index]){								//If its first in the list
		blocks[index] = blocks[index]->next;
	}
	else {																	//If its not first
		ptr->prev->next = ptr->next;
	}
	if(ptr->next != NULL){									//If its not last
		ptr->next->prev = ptr->prev;
	}
}
// First Fit Implementation //
metadata_t* firstFitblock(size_t numbytes,int index) {
	int i;
	for(i=index;i<10;i++){
		if(blocks[i] != NULL){
			metadata_t* itr = blocks[i];
			while(itr != NULL){
				if(itr->size >= numbytes){
					removeFromList(itr,i);
					return itr;
				}
				itr = itr->next;
			}
		}
	}
	return NULL;
}
// Best Fit Implementation //
/*metadata_t* bestFit(size_t numbytes,int index) {
	metadata_t* bestFit = NULL;
	int ind = 0;
	int i;
	for(i=index;i<10;i++){
			metadata_t* itr = blocks[i];
			while(itr != NULL){
				if(itr->size >= numbytes){
					if(bestFit == NULL){
						bestFit = itr;
						ind = i;
					}
					if(itr->size < bestFit->size){
						bestFit = itr;
						ind = i;
					}
				}
				itr = itr->next;
			}
	}

	if(bestFit != NULL){removeFromList(bestFit,ind);}
	return bestFit;
}*/

//////////////////////////////
////Allocation Functions//////
/////////////////////////////

void* dmalloc(size_t numbytes) {
	numbytes = ALIGN(numbytes);

	if(unallocated) { 			//Initialize through sbrk call first time
		unallocated = 0;
/*		if(!dmalloc_init()) */
		printf("memory manager not initalised\n");
			return NULL;
	}

	assert(numbytes > 0);

	//Find the first block that is of sufficient size.
	metadata_t* freeBlock = (metadata_t*) firstFitblock(numbytes,convertToIndex(numbytes));
	if(freeBlock == NULL){return NULL;}

	//Check if we shouldn't split the block - used 8 so we at worst the split has a leftover block of size 8
	if(freeBlock->size < numbytes + METADATA_T_ALIGNED + FOOT_T_ALIGNED + 16){//Consider expanding this size requirement
			freeBlock->next = (void*) -1;
			freeBlock->prev = (void*) -1;
			return ((void*)freeBlock) + METADATA_T_ALIGNED;
	}

	//Otherwise Split the Block - the retBlock: return block and the leftover is freeBlock
	metadata_t* retBlock = freeBlock;

	//Make freeBlock the leftover block and put it back in blocks
	freeBlock = (metadata_t*)(((void*)(freeBlock)) +  METADATA_T_ALIGNED + numbytes + FOOT_T_ALIGNED); //Move its address to front of metadata
	freeBlock->size = retBlock->size - numbytes - METADATA_T_ALIGNED - FOOT_T_ALIGNED; 								 // adjust size

	int i = convertToIndex(freeBlock->size);
	freeBlock->next = blocks[i];
	freeBlock->prev = NULL;
	foot_t* fbFoot = (foot_t*) (((void*) freeBlock) + METADATA_T_ALIGNED + freeBlock->size);
	fbFoot->size = freeBlock->size;

	//Adjust pointers at front of block
	if(blocks[i] != NULL){
			blocks[i]->prev = freeBlock;
	}
	blocks[i] = freeBlock;	//make blocks point to freeblock

	//Adjust return metadata/footer
	retBlock->size = numbytes;
	retBlock->next = (void*) - 1;
	retBlock->prev = (void*) - 1;
	foot_t* retFoot = (foot_t*)(((void*)(retBlock)) + retBlock->size + METADATA_T_ALIGNED);
	retFoot->size = retBlock->size;

	return (((void*)(retBlock)) + METADATA_T_ALIGNED);
}

void dfree(void* ptr) {

	metadata_t* myHead   = (metadata_t*) (((void*)(ptr)) - METADATA_T_ALIGNED); //front of header
	foot_t*     myFoot   = (foot_t*) 		 (((void*)(ptr)) + myHead->size);//footer

	metadata_t* prevHead = NULL;
	foot_t*     prevFoot = (foot_t*)     (((void*)(ptr)) - FOOT_T_ALIGNED - METADATA_T_ALIGNED);

	metadata_t* nextHead = (metadata_t*) (((void*)(myFoot)) + FOOT_T_ALIGNED);
	foot_t*     nextFoot = NULL;

	int prevFree = 0;
	int nextFree = 0;

	//Check if prev block can be coalesced
	if(prevFoot >= (foot_t*) slabStart){
		prevHead = (metadata_t*) (((void*)(ptr)) - FOOT_T_ALIGNED  - prevFoot->size - 2*METADATA_T_ALIGNED);
		if(prevHead->next != (void*) -1){ 					//If prev is free
			prevFree = 1;
			removeFromList(prevHead,convertToIndex(prevHead->size));
		}
	}
	//Check if next block can be coalesced
	if(nextHead < (metadata_t*) slabEnd){
		nextFoot = (foot_t*) (((void*)(nextHead)) + nextHead->size + METADATA_T_ALIGNED);
		if(nextHead->next != (void*) -1){					//If next is free
			nextFree = 1;
			removeFromList(nextHead,convertToIndex(nextHead->size));
		}
	}

	//Start coalescing prev
	if(prevFree){
		prevHead->size = prevHead->size + myHead->size + METADATA_T_ALIGNED + FOOT_T_ALIGNED;
		myFoot->size = prevHead->size;
		myHead = prevHead;
	}
	//Start coalescing next
	if(nextFree){
		myHead->size = myHead->size + nextHead->size + METADATA_T_ALIGNED + FOOT_T_ALIGNED;
		nextFoot->size = myHead->size;
	}
	//Put into appropriate freelist block
	int index = convertToIndex(myHead->size);
	myHead->next = blocks[index];
	blocks[index] = myHead;
	myHead->prev = NULL;

	if(myHead->next != NULL){
		myHead->next->prev = myHead;
	}

}

int dmalloc_init(void *memory_chunk, long max_bytes) {
	/* Two choices:
 	* 1. Append prologue and epilogue blocks to the start and the end of the freelist
 	* 2. Initialize freelist pointers to NULL
 	*
 	* Note: We provide the code for 2. Using 1 will help you to tackle the
 	* corner cases succinctly.
 	*/
/*	long max_bytes = ALIGN(MAX_HEAP_SIZE);
	blocks[9] = (metadata_t*) sbrk(max_bytes); */ // returns heap_region, which is initialized to freelist
	if (!memory_chunk)
	       	return 0;
	blocks[9] = (metadata_t*) memory_chunk;
	/* Q: Why casting is used? i.e., why (void*)-1? */
	if (blocks[9] == (void *)-1)
		return 0;
	slabStart = blocks[9];
	slabEnd = slabStart + max_bytes;
	blocks[9]->next = NULL;
	blocks[9]->size = max_bytes - METADATA_T_ALIGNED - FOOT_T_ALIGNED;
	foot_t* foot = 	(foot_t*) (slabEnd - FOOT_T_ALIGNED);
	foot->size = blocks[9]->size;
	unallocated = 0;
	return 1;
}

/*Only for debugging purposes; can be turned off through  flag*/
void print_freelist() {
	int i;
	for(i=0;i<10;i++){
		metadata_t *freelist_head = blocks[i];
		while(freelist_head != NULL) {
			DEBUG("Block: %x Size:%zx, Head:%p, Prev:%p Next:%p\t",i,freelist_head->size,freelist_head, freelist_head->prev, freelist_head->next);
			freelist_head = freelist_head->next;
		}
		DEBUG("\n");
	}
}
