#ifndef GPU_SHMEM_H_

#define GPU_SHMEM_H_

#include <mpi.h>
#ifdef __cplusplus
#include <cuda_runtime.h>
#else
#include <cuda_runtime_api.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void ** ext_mpi_get_shmem_root_gpu();
int ext_mpi_gpu_sizeof_memhandle();
void *ext_mpi_init_shared_memory_gpu(MPI_Comm comm_world, size_t size_shared);
int ext_mpi_done_shared_memory_gpu(MPI_Comm comm_world);
int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    int size_shared, int num_segments,
                                    int **shmemidi_gpu, char ***shmem_gpu);
int ext_mpi_gpu_destroy_shared_memory(int my_cores_per_node, int *ranks_node, int *shmemid_gpu, char **shmem_gpu);
int ext_mpi_sendrecvbuf_init_gpu(MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs, int *mem_partners);
int ext_mpi_sendrecvbuf_done_gpu(int my_cores_per_node, char **sendrecvbufs);
int ext_mpi_init_gpu_blocking(MPI_Comm comm_world);
void ext_mpi_done_gpu_blocking();
int ext_mpi_sendrecvbuf_init_gpu_blocking(int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, int *ranks_node, char **sendbufs, char **recvbufs);

#ifdef __cplusplus
}
#endif

#endif
