/* adapted from https://www.tutorialspoint.com/data_structures_algorithms/hash_table_program_in_c.htm */

#ifndef EXT_MPI_HASH_TABLE_BLOCKING_H_

#define EXT_MPI_HASH_TABLE_BLOCKING_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_hash_search_blocking(MPI_Comm *key);
void ext_mpi_hash_insert_blocking(MPI_Comm *key, int data);
int ext_mpi_hash_delete_blocking(MPI_Comm *key);
int ext_mpi_hash_init_blocking();
int ext_mpi_hash_done_blocking();

#ifdef __cplusplus
}
#endif

#endif
