#ifndef GPU_SHMEM_H_

#define GPU_SHMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int gpu_sizeof_memhandle();
int gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                            MPI_Comm comm_column, int my_cores_per_node_column,
                            int size_shared, void *shmemid_gpu,
                            char volatile **shmem_gpu);
int gpu_destroy_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                              MPI_Comm comm_column,
                              int my_cores_per_node_column,
                              char volatile **shmem_gpu);

#ifdef __cplusplus
}
#endif

#endif
