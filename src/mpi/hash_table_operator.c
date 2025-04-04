/* adapted from https://www.tutorialspoint.com/data_structures_algorithms/hash_table_program_in_c.htm */

#include <stdlib.h>
#include <mpi.h>
#include "constants.h"
#include "hash_table_operator.h"

#define SIZE 256

struct DataItem {
   MPI_User_function *data;   
   MPI_Op key;
};

static struct DataItem** hashArray;
static struct DataItem* dummyItem;

static int hashCode(MPI_Op *key) {
   unsigned char value = 0;
   int i;
   for (i = 0; i < sizeof(MPI_Op); i++){
     value += ((unsigned char *)(key))[i];
   }
   return value;
}

MPI_User_function * ext_mpi_hash_search_operator(MPI_Op *key) {
   int hashIndex = hashCode(key);  

   while(hashArray[hashIndex] != NULL) {

      if(hashArray[hashIndex]->key == *key)
         return hashArray[hashIndex]->data;

      ++hashIndex;

      hashIndex %= SIZE;
   }        

   return NULL;
}

void ext_mpi_hash_insert_operator(MPI_Op *key, MPI_User_function *data) {

   struct DataItem *item = (struct DataItem*) malloc(sizeof(struct DataItem));
   item->data = data;  
   item->key = *key;

   int hashIndex = hashCode(key);

   while(hashArray[hashIndex] != NULL && hashArray[hashIndex]->key != MPI_OP_NULL) {
      ++hashIndex;
		
      hashIndex %= SIZE;
   }
	
   hashArray[hashIndex] = item;
}

MPI_User_function * ext_mpi_hash_delete_operator(MPI_Op *key) {
   int hashIndex = hashCode(key);

   while(hashArray[hashIndex] != NULL) {
	
      if(hashArray[hashIndex]->key == *key) {
         void *temp = hashArray[hashIndex]->data;
	 free(hashArray[hashIndex]);
         hashArray[hashIndex] = dummyItem;
         return temp;
      }
		
      ++hashIndex;
		
      hashIndex %= SIZE;
   }      
	
   return NULL;
}

int ext_mpi_hash_init_operator(){
  int i;
  hashArray = (struct DataItem**) malloc(SIZE*sizeof(struct DataItem*));
  for (i=0; i<SIZE; i++){
    hashArray[i] = NULL;
  }
  dummyItem = (struct DataItem*) malloc(sizeof(struct DataItem));
  if (dummyItem==NULL){
    return ERROR_MALLOC;
  }
  dummyItem->data = NULL;  
  dummyItem->key = MPI_OP_NULL; 
  return 0;
}

int ext_mpi_hash_done_operator() {
  int i;
  for (i = 0; i < SIZE; i++) {
    if (hashArray[i] != dummyItem) {
      free(hashArray[i]);
    }
  }
  free(hashArray);
  free(dummyItem);
  return 0;
}
