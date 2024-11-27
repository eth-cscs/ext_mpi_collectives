#ifndef EXT_MPI_BYTE_CODE_H_

#define EXT_MPI_BYTE_CODE_H_

#ifdef GPU_ENABLED
#include "cuda_gemv.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct header_byte_code {
  int size_to_return;
  int barrier_counter_socket;
  int barrier_counter_node;
  char **barrier_shmem_socket;
  char **barrier_shmem_socket_small;
  char **barrier_shmem_node;
  char **shmem;
  int *shmemid;
  int *shmem_sizes;
  char *locmem;
  int node_num_cores_row;
  int node_num_cores_column;
  int num_cores;
  int num_cores_socket_barrier;
  int num_cores_socket_barrier_small;
  int socket_rank;
  int node_sockets;
  int tag;
  int num_sockets_per_node;
  char **sendbufs;
  char **recvbufs;
  void *mpi_user_function;
  void *function;
  int *ranks_node;
#ifdef GPU_ENABLED
  char **shmem_gpu;
  int *shmemid_gpu;
  int gpu_byte_code_size;
  char *gpu_byte_code;
  struct gemv_var gpu_gemv_var;
#endif
};

int ext_mpi_generate_byte_code(char **shmem,
                               int *shmemid, int *shmem_sizes,
                               char *buffer_in, char **sendbufs, char **recvbufs,
                               int barriers_size, char *locmem,
                               int reduction_op, void *func, int *global_ranks,
                               char *code_out, int size_request, int *ranks_node,
                               int node_num_cores_row, int node_num_cores_column,
                               int *shmemid_gpu, char **shmem_gpu, int *gpu_byte_code_counter, int gpu_fallback, int tag);

#ifdef __cplusplus
}
#endif

#endif
