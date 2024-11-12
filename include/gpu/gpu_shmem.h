#ifndef GPU_SHMEM_H_

#define GPU_SHMEM_H_

#include <mpi.h>

struct address_registration {
  struct address_registration *left, *right;
  char *address;
  size_t size;
};

struct address_lookup {
  struct address_lookup *left, *right;
  char *address_key, *address_value;
  size_t size_key;
};

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
void ext_mpi_done_gpu_blocking(int num_tasks, struct address_registration *address_registration_root, struct address_lookup **address_lookup_root);
int ext_mpi_sendrecvbuf_init_gpu_blocking(struct address_registration **address_registration_root, struct address_lookup **address_lookup_root, int my_mpi_rank, int my_cores_per_node, int num_sockets, char *sendbuf, char *recvbuf, size_t size, int *mem_partners_send, int *mem_partners_recv, char ***shmem, int **shmem_node, int *counter, char **sendbufs, char **recvbufs);
int ext_mpi_sendrecvbuf_done_gpu_blocking(char **sendbufs, char **recvbufs, int *mem_partners_send, int *mem_partners_recv);

#ifdef __cplusplus
}
#endif

#endif
