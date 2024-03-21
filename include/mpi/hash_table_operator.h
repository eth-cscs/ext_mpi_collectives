/* adapted from https://www.tutorialspoint.com/data_structures_algorithms/hash_table_program_in_c.htm */

#ifndef EXT_MPI_HASH_TABLE_OPERATOR_H_

#define EXT_MPI_HASH_TABLE_OPERATOR_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

MPI_User_function * ext_mpi_hash_search_operator(MPI_Op *key);
void ext_mpi_hash_insert_operator(MPI_Op *key, MPI_User_function *data);
MPI_User_function * ext_mpi_hash_delete_operator(MPI_Op *key);
int ext_mpi_hash_init_operator();
int ext_mpi_hash_done_operator();

#ifdef __cplusplus
}
#endif

#endif
