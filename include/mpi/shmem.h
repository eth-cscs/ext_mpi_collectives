#ifndef EXT_MPI_SHMEM_H_

#define EXT_MPI_SHMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

void ext_mpi_node_barrier_mpi(MPI_Comm shmem_comm_node_row,
                              MPI_Comm shmem_comm_node_column, char *comm_code);

int ext_mpi_destroy_shared_memory(int size_shared, int num_segments, int *shmemid,
                                  char **shmem, char *comm_code);

int ext_mpi_setup_shared_memory(MPI_Comm *shmem_comm_node_row,
                                MPI_Comm *shmem_comm_node_column,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *size_shared,
                                int num_segments, int **shmemid,
                                char ***shmem, char fill, int numfill,
                                char **comm_code);

#ifdef __cplusplus
}
#endif

#endif
