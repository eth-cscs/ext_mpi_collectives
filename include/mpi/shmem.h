#ifndef EXT_MPI_SHMEM_H_

#define EXT_MPI_SHMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

void * ext_mpi_init_shared_memory(MPI_Comm comm_world, size_t size_shared);
int ext_mpi_done_shared_memory(MPI_Comm comm_world);
int ext_mpi_destroy_shared_memory(int num_segments, int *ranks_node, int *sizes_shared, int *shmemid, char **shmem);
int ext_mpi_setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row, int num_sockets_per_node,
                                int size_shared, int **sizes_shared, int **shmemid, char ***shmem);

#ifdef __cplusplus
}
#endif

#endif
