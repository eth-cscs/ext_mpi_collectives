/* adapted from https://www.tutorialspoint.com/data_structures_algorithms/hash_table_program_in_c.htm */

#ifndef EXT_MPI_HASH_TABLE_H_

#define EXT_MPI_HASH_TABLE_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_hash_search(MPI_Request *key);
void ext_mpi_hash_insert(MPI_Request *key, int data);
int ext_mpi_hash_delete(MPI_Request *key);
int ext_mpi_hash_init();
int ext_mpi_hash_done();

#ifdef __cplusplus
}
#endif

#endif
