#ifndef EXT_MPI_BYTE_CODE_H_

#define EXT_MPI_BYTE_CODE_H_

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

struct header_byte_code {
  int barrier_counter;
  char *barrier_shmem;
  int barrier_shmem_size;
  int *shmemid;
  char *locmem;
  char **shmem;
  int shmem_size;
  int buf_size;
  MPI_Comm comm_row;
  MPI_Comm comm_column;
  int node_num_cores_row;
  int node_num_cores_column;
  int num_cores;
  int node_rank;
  int tag;
#ifdef GPU_ENABLED
  char **shmem_gpu;
  char *gpu_byte_code;
#endif
};

int ext_mpi_generate_byte_code(char **shmem,
                               int barrier_shmem_size, int *barrier_shmemid,
                               char *buffer_in, char *sendbuf, char *recvbuf,
                               int my_size_shared_buf, int barriers_size, char *locmem,
                               int reduction_op, int *global_ranks,
                               char *code_out, MPI_Comm comm_row,
                               int node_num_cores_row, MPI_Comm comm_column,
                               int node_num_cores_column,
                               char **shmem_gpu, int *gpu_byte_code_counter, int tag);

#ifdef __cplusplus
}
#endif

#endif
