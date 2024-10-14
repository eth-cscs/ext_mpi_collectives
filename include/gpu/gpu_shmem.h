#ifndef GPU_SHMEM_H_

#define GPU_SHMEM_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

int ext_mpi_gpu_sizeof_memhandle();
int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    int size_shared, int num_segments,
                                    int **shmemidi_gpu, char ***shmem_gpu);
int ext_mpi_gpu_destroy_shared_memory(int my_cores_per_node, int *shmemid_gpu, char **shmem_gpu, char *comm_code);
int ext_mpi_sendrecvbuf_init_gpu(MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs, int *mem_partners);
int ext_mpi_sendrecvbuf_done_gpu(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs);

#ifdef __cplusplus
}
#endif

#endif
