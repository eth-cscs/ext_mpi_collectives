#ifndef EXT_MPI_MESSAGES_SHARED_MEMORY_H_

#define EXT_MPI_MESSAGES_SHARED_MEMORY_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C"
{
#endif

int ext_mpi_messages_shared_memory(char *buffer_in, char *buffer_out, MPI_Comm comm_row, int node_num_cores_row, MPI_Comm comm_column, int node_num_cores_column);

#ifdef __cplusplus
}
#endif

#endif
