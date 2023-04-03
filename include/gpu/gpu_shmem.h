#ifndef GPU_SHMEM_H_

#define GPU_SHMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_gpu_sizeof_memhandle();
int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    MPI_Comm comm_column, int my_cores_per_node_column,
                                    int size_shared, int num_segments, int **shmemid_gpu,
                                    char ***shmem_gpu);
int ext_mpi_gpu_destroy_shared_memory(int num_segments, int *shmemid_gpu, char **shmem_gpu, char *comm_code);

#ifdef __cplusplus
}
#endif

#endif
