/* adapted from https://www.tutorialspoint.com/data_structures_algorithms/hash_table_program_in_c.htm */

#include <stdlib.h>
#include <mpi.h>
#include "constants.h"
#include "hash_table.h"

#define SIZE 50

struct DataItem {
   int data;   
   MPI_Request key;
};

static struct DataItem** hashArray;
static struct DataItem* dummyItem;

static int hashCode(MPI_Request *key) {
   int value = 0, i;
   for (i = 0; i < sizeof(MPI_Request); i++){
     value += ((char *)(key))[i];
   }
   return value % SIZE;
}

int ext_mpi_hash_search(MPI_Request *key) {
   int hashIndex = hashCode(key);  
	
   while(hashArray[hashIndex] != NULL) {
	
      if(hashArray[hashIndex]->key == *key)
         return hashArray[hashIndex]->data;
			
      ++hashIndex;
		
      hashIndex %= SIZE;
   }        
	
   return -1;
}

void ext_mpi_hash_insert(MPI_Request *key, int data) {

   struct DataItem *item = (struct DataItem*) malloc(sizeof(struct DataItem));
   item->data = data;  
   item->key = *key;

   int hashIndex = hashCode(key);

   while(hashArray[hashIndex] != NULL && hashArray[hashIndex]->key != MPI_REQUEST_NULL) {
      ++hashIndex;
		
      hashIndex %= SIZE;
   }
	
   hashArray[hashIndex] = item;
}

int ext_mpi_hash_delete(MPI_Request *key) {
   int hashIndex = hashCode(key);

   while(hashArray[hashIndex] != NULL) {
	
      if(hashArray[hashIndex]->key == *key) {
         int temp = hashArray[hashIndex]->data;
         hashArray[hashIndex] = dummyItem;
         return temp;
      }
		
      ++hashIndex;
		
      hashIndex %= SIZE;
   }      
	
   return -1;
}

int ext_mpi_hash_init(){
  int i;
  hashArray = (struct DataItem**) malloc(SIZE*sizeof(struct DataItem*));
  for (i=0; i<SIZE; i++){
    hashArray[i] = NULL;
  }
  dummyItem = (struct DataItem*) malloc(sizeof(struct DataItem));
  if (dummyItem==NULL){
    return ERROR_MALLOC;
  }
  dummyItem->data = -1;  
  dummyItem->key = MPI_REQUEST_NULL; 
  return 0;
}
