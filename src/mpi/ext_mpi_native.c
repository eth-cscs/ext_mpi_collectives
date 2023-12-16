#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "prime_factors.h"
#include "allreduce.h"
#include "allreduce_recursive.h"
#include "allreduce_short.h"
#include "alltoall.h"
#include "backward_interpreter.h"
#include "buffer_offset.h"
#include "byte_code.h"
#include "clean_barriers.h"
#include "constants.h"
#include "dummy.h"
#include "ext_mpi_native.h"
#include "forward_interpreter.h"
#include "no_first_barrier.h"
#include "no_offset.h"
#include "optimise_buffers.h"
#include "optimise_buffers2.h"
#include "parallel_memcpy.h"
#include "ports_groups.h"
#include "rank_permutation.h"
#include "raw_code.h"
#include "raw_code_merge.h"
#include "raw_code_tasks_node.h"
#include "raw_code_tasks_node_master.h"
#include "reduce_copyin.h"
#include "reduce_copyout.h"
#include "use_recvbuf.h"
#include "use_sendbuf_recvbuf.h"
#include "swap_copyin.h"
#include "no_socket_barriers.h"
#include "waitany.h"
#include "messages_shared_memory.h"
#include "optimise_multi_socket.h"
#include "reduce_scatter_single_node.h"
#include "padding_factor.h"
#include "shmem.h"
#include "ext_mpi_native_exec.h"
#include <mpi.h>
#ifdef GPU_ENABLED
#include "gpu_core.h"
#include "gpu_shmem.h"
#include "cuda_gemv.h"
#endif
#ifdef NCCL_ENABLED
#include <nccl.h>
ncclComm_t ext_mpi_nccl_comm;
#endif

#define NUM_BARRIERS 4

#ifndef GPU_ENABLED
#define SEND_PTR_CPU 0x80000000
#define RECV_PTR_CPU 0x90000000
#else
#define SEND_PTR_GPU 0x80000000
#define SEND_PTR_CPU 0x90000000
#define RECV_PTR_GPU 0xa0000000
#define RECV_PTR_CPU 0xb0000000
#endif

static int handle_code_max = 100;
static char **comm_code = NULL;
static char **execution_pointer = NULL;
static int *active_wait = NULL;
static int is_initialised = 0;
static int tag_max = 0;

MPI_Comm ext_mpi_COMM_WORLD_dup = MPI_COMM_NULL;

struct comm_comm_blocking {
  int mpi_size_blocking;
  int mpi_rank_blocking;
  char ***send_pointers_allreduce_blocking;
  int *padding_factor_allreduce_blocking;
  int *padding_factor_reduce_scatter_block_blocking;
  int *count_allreduce_blocking;
  int *count_reduce_scatter_block_blocking;
  int *count_allgather_blocking;
  char **shmem_blocking1;
  char **shmem_blocking2;
//  int shmem_blocking_num_segments;
  int *shmem_blocking_shmemid1;
  int *shmem_blocking_shmemid2;
  char *locmem_blocking;
  char **comm_code_allreduce_blocking;
  char **comm_code_reduce_scatter_block_blocking;
  char **comm_code_allgather_blocking;
  char *shmem_socket_blocking;
  int shmem_socket_blocking_shmemid;
  int counter_socket_blocking;
  int socket_rank_blocking;
  int num_cores_blocking;
  char **shmem_node_blocking;
  int shmem_node_blocking_num_segments;
  int *shmem_node_blocking_shmemid;
  int counter_node_blocking;
  int num_sockets_per_node_blocking;
  MPI_Comm comm_blocking;
  MPI_Comm comm_row_blocking;
  MPI_Comm comm_column_blocking;
  void *p_dev_temp;
};

/*static int global_min(int i, MPI_Comm comm_row, MPI_Comm comm_column) {
  PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_column);
  }
  return (i);
}*/

#ifdef GPU_ENABLED
static int gpu_is_device_pointer(void *buf) {
#ifdef GPU_ENABLED
  if ((void *)(SEND_PTR_CPU) == buf || (void *)(RECV_PTR_CPU) == buf) return 0;
  if ((void *)(SEND_PTR_GPU) == buf || (void *)(RECV_PTR_GPU) == buf) return 1;
  return ext_mpi_gpu_is_device_pointer(buf);
#else
  return 0;
#endif
}
#endif

static int setup_rank_translation(MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column,
                                  int *global_ranks) {
  MPI_Comm my_comm_node;
  int my_mpi_size_row, grank, my_mpi_size_column, my_mpi_rank_column,
      *lglobal_ranks = NULL;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
    if (PMPI_Comm_split(comm_column,
                        my_mpi_rank_column / my_cores_per_node_column,
                        my_mpi_rank_column % my_cores_per_node_column,
                        &my_comm_node) == MPI_ERR_INTERN)
      goto error;
    MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &grank);
    lglobal_ranks = (int *)malloc(sizeof(int) * my_cores_per_node_column);
    if (!lglobal_ranks)
      goto error;
    MPI_Gather(&grank, 1, MPI_INT, lglobal_ranks, 1, MPI_INT, 0, my_comm_node);
    MPI_Bcast(lglobal_ranks, my_cores_per_node_column, MPI_INT, 0,
              my_comm_node);
    MPI_Barrier(my_comm_node);
    PMPI_Comm_free(&my_comm_node);
    MPI_Gather(lglobal_ranks, my_cores_per_node_column, MPI_INT, global_ranks,
               my_cores_per_node_column, MPI_INT, 0, comm_row);
    free(lglobal_ranks);
  } else {
    MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &grank);
    MPI_Gather(&grank, 1, MPI_INT, global_ranks, 1, MPI_INT, 0, comm_row);
  }
  MPI_Bcast(global_ranks, my_mpi_size_row * my_cores_per_node_column, MPI_INT,
            0, comm_row);
  return 0;
error:
  free(lglobal_ranks);
  return ERROR_MALLOC;
}

static int get_handle() {
  char **comm_code_old = NULL, **execution_pointer_old = NULL;
  int *active_wait_old = NULL, handle, i;
  if (comm_code == NULL) {
    comm_code = (char **)malloc(sizeof(char *) * handle_code_max);
    if (!comm_code)
      goto error;
    execution_pointer = (char **)malloc(sizeof(char *) * handle_code_max);
    if (!execution_pointer)
      goto error;
    active_wait = (int *)malloc(sizeof(int) * handle_code_max);
    if (!active_wait)
      goto error;
    for (i = 0; i < (handle_code_max); i++) {
      comm_code[i] = NULL;
      execution_pointer[i] = NULL;
      active_wait[i] = 0;
    }
  }
  handle = 0;
  while ((comm_code[handle] != NULL) && (handle < handle_code_max - 2)) {
    handle += 2;
  }
  if (handle >= handle_code_max - 2) {
    if (comm_code[handle] != NULL) {
      comm_code_old = comm_code;
      execution_pointer_old = execution_pointer;
      active_wait_old = active_wait;
      handle_code_max *= 2;
      comm_code = (char **)malloc(sizeof(char *) * handle_code_max);
      if (!comm_code)
        goto error;
      execution_pointer = (char **)malloc(sizeof(char *) * handle_code_max);
      if (!execution_pointer)
        goto error;
      active_wait = (int *)malloc(sizeof(int) * handle_code_max);
      if (!active_wait)
        goto error;
      for (i = 0; i < handle_code_max; i++) {
        comm_code[i] = NULL;
        execution_pointer[i] = NULL;
        active_wait[i] = 0;
      }
      for (i = 0; i < handle_code_max / 2; i++) {
        comm_code[i] = comm_code_old[i];
        execution_pointer[i] = execution_pointer_old[i];
        active_wait[i] = active_wait_old[i];
      }
      free(active_wait_old);
      free(execution_pointer_old);
      free(comm_code_old);
      handle += 2;
    }
  }
  return handle;
error:
  free(active_wait);
  active_wait = NULL;
  free(execution_pointer);
  execution_pointer = NULL;
  free(comm_code);
  comm_code = NULL;
  return ERROR_MALLOC;
}

int EXT_MPI_Start_native(int handle) {
  char *hcomm;
  if (comm_code[handle + 1]) {
    hcomm = comm_code[handle];
    comm_code[handle] = comm_code[handle + 1];
    comm_code[handle + 1] = hcomm;
    hcomm = execution_pointer[handle];
    execution_pointer[handle] = execution_pointer[handle + 1];
    execution_pointer[handle + 1] = hcomm;
  }
  execution_pointer[handle] =
      comm_code[handle] + sizeof(struct header_byte_code);
  active_wait[handle] = 0;
  return (0);
}

int EXT_MPI_Test_native(int handle) {
  active_wait[handle] = 1;
  if (execution_pointer[handle]) {
    ext_mpi_exec_native(comm_code[handle], &execution_pointer[handle],
                        active_wait[handle]);
  }
  return (execution_pointer[handle] == NULL);
}

int EXT_MPI_Progress_native() {
  int ret = 0, handle;
  for (handle = 0; handle < handle_code_max; handle += 2) {
    if (execution_pointer[handle]) {
      ret += ext_mpi_exec_native(comm_code[handle], &execution_pointer[handle],
                                 active_wait[handle]);
    }
  }
  return (ret);
}

int EXT_MPI_Wait_native(int handle) {
  if (!active_wait[handle]) {
    active_wait[handle] = 3;
  } else {
    active_wait[handle] = 2;
  }
  if (execution_pointer[handle]) {
    ext_mpi_exec_native(comm_code[handle], &execution_pointer[handle],
                        active_wait[handle]);
  }
  active_wait[handle] = 0;
  return (0);
}

int EXT_MPI_Done_native(int handle) {
  char **shmem;
  char *ip, *locmem;
  int shmem_size, *shmemid, i;
  MPI_Comm shmem_comm_node_row, shmem_comm_node_column, shmem_comm_node_row2,
      shmem_comm_node_column2;
  struct header_byte_code *header;
  EXT_MPI_Wait_native(handle);
  ip = comm_code[handle];
  header = (struct header_byte_code *)ip;
  shmem = header->shmem;
  shmem_size = header->barrier_shmem_size;
  shmemid = header->shmemid;
  locmem = header->locmem;
  if ((MPI_Comm *)(ip + header->size_to_return)) {
    shmem_comm_node_row = *((MPI_Comm *)(ip + header->size_to_return));
  } else {
    shmem_comm_node_row = MPI_COMM_NULL;
  }
  if ((MPI_Comm *)(ip + header->size_to_return + sizeof(MPI_Comm))) {
    shmem_comm_node_column = *((MPI_Comm *)(ip + header->size_to_return + sizeof(MPI_Comm)));
  } else {
    shmem_comm_node_column = MPI_COMM_NULL;
  }
  ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code[handle]);
#ifdef GPU_ENABLED
  if (header->shmem_gpu) {
    if (header->shmemid_gpu) {
      ext_mpi_gpu_destroy_shared_memory(header->num_sockets_per_node, header->shmemid_gpu, header->shmem_gpu, comm_code[handle]);
    }
    header->shmem_gpu = NULL;
    header->shmemid_gpu = NULL;
  }
  if (header->gpu_gemv_var.handle) {
    ext_mpi_gemv_done(&header->gpu_gemv_var);
  }
  if (ext_mpi_gpu_is_device_pointer(header->gpu_byte_code)) {
    ext_mpi_gpu_free(header->gpu_byte_code);
  } else {
    free(header->gpu_byte_code);
  }
#endif
  ext_mpi_destroy_shared_memory(shmem_size, header->num_sockets_per_node, shmemid, shmem, comm_code[handle]);
  ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code[handle]);
  free(locmem);
  free(((struct header_byte_code *)comm_code[handle])->barrier_shmem_node);
  free(comm_code[handle]);
  comm_code[handle] = NULL;
  ip = comm_code[handle + 1];
  if (ip) {
    header = (struct header_byte_code *)ip;
    shmem = header->shmem;
    shmem_size = header->barrier_shmem_size;
    shmemid = header->shmemid;
    locmem = header->locmem;
    if ((MPI_Comm *)(ip + header->size_to_return)) {
      shmem_comm_node_row2 = *((MPI_Comm *)(ip + header->size_to_return));
    } else {
      shmem_comm_node_row2 = MPI_COMM_NULL;
    }
    if ((MPI_Comm *)(ip + header->size_to_return + sizeof(MPI_Comm))) {
      shmem_comm_node_column2 = *((MPI_Comm *)(ip + header->size_to_return + sizeof(MPI_Comm)));
    } else {
      shmem_comm_node_column2 = MPI_COMM_NULL;
    }
    ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code[handle + 1]);
#ifdef GPU_ENABLED
    if (header->shmem_gpu) {
      if (header->shmemid_gpu) {
        ext_mpi_gpu_destroy_shared_memory(header->num_sockets_per_node, header->shmemid_gpu, header->shmem_gpu, comm_code[handle]);
      }
      header->shmem_gpu = NULL;
      header->shmemid_gpu = NULL;
    }
    if (header->gpu_gemv_var.handle) {
      ext_mpi_gemv_done(&header->gpu_gemv_var);
    }
    if (ext_mpi_gpu_is_device_pointer(header->gpu_byte_code)) {
      ext_mpi_gpu_free(header->gpu_byte_code);
    } else {
      free(header->gpu_byte_code);
    }
#endif
    ext_mpi_destroy_shared_memory(shmem_size, header->num_sockets_per_node, shmemid, shmem, comm_code[handle + 1]);
    ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code[handle + 1]);
    free(locmem);
    free(((struct header_byte_code *)comm_code[handle + 1])->barrier_shmem_node);
    free(comm_code[handle + 1]);
    comm_code[handle + 1] = NULL;
  }
  for (i = 0; i < handle_code_max; i++) {
    if (comm_code[i] != NULL) {
      return (0);
    }
  }
  if (shmem_comm_node_row != MPI_COMM_NULL) {
    PMPI_Comm_free(&shmem_comm_node_row);
  }
  if (shmem_comm_node_column != MPI_COMM_NULL) {
    PMPI_Comm_free(&shmem_comm_node_column);
  }
  if (ip) {
    if (shmem_comm_node_row2 != MPI_COMM_NULL) {
      PMPI_Comm_free(&shmem_comm_node_row2);
    }
    if (shmem_comm_node_column2 != MPI_COMM_NULL) {
      PMPI_Comm_free(&shmem_comm_node_column2);
    }
  }
  free(active_wait);
  free(execution_pointer);
  free(comm_code);
  comm_code = NULL;
  execution_pointer = NULL;
  active_wait = NULL;
  return 0;
}

static int init_epilogue(char *buffer_in, const void *sendbuf, void *recvbuf,
                         int reduction_op, MPI_Comm comm_row,
                         int my_cores_per_node_row, MPI_Comm comm_column,
                         int my_cores_per_node_column, int alt, int shmem_zero, char *locmem) {
  int i, num_comm_max = -1, my_size_shared_buf = -1, barriers_size,
         nbuffer_in = 0, tag, not_locmem;
  char *ip;
  int handle, *global_ranks = NULL, code_size, my_mpi_size_row;
  int locmem_size, shmem_size = 0, *shmemid = NULL, num_sockets_per_node = 1;
  char **shmem = NULL;
  MPI_Comm shmem_comm_node_row, shmem_comm_node_column;
  int gpu_byte_code_counter = 0;
  char **shmem_gpu = NULL;
  int *shmemid_gpu = NULL;
  struct parameters_block *parameters;
  not_locmem = (locmem == NULL);
  handle = get_handle();
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  num_comm_max = parameters->locmem_max;
  my_size_shared_buf = parameters->shmem_max;
  num_sockets_per_node = parameters->num_sockets_per_node;
  ext_mpi_delete_parameters(parameters);
  locmem_size = num_comm_max * sizeof(MPI_Request);
  if (not_locmem) {
    locmem = (char *)malloc(locmem_size);
  }
  if (!locmem)
    goto error;
  barriers_size = (num_sockets_per_node < my_cores_per_node_row * my_cores_per_node_column? my_cores_per_node_row * my_cores_per_node_column : num_sockets_per_node) *
                   (NUM_BARRIERS + 1) * CACHE_LINE_SIZE * sizeof(int);
  barriers_size += NUM_BARRIERS * CACHE_LINE_SIZE * sizeof(int);
  barriers_size = (barriers_size/(CACHE_LINE_SIZE * sizeof(int)) + 1) * (CACHE_LINE_SIZE * sizeof(int));
  shmem_size = my_size_shared_buf + barriers_size * 4;
  if (shmem_zero) {
    shmem = (char **)malloc(sizeof(char *) * 8);
    for (i = 0; i < 8; i++) {
      shmem[i] = (char *)(((long int)i) << 28);
    }
    shmemid = NULL;
  } else {
    if (ext_mpi_setup_shared_memory(&shmem_comm_node_row, &shmem_comm_node_column,
                                    comm_row, my_cores_per_node_row, comm_column,
                                    my_cores_per_node_column, &shmem_size, num_sockets_per_node,
                                    &shmemid, &shmem, 0, barriers_size * 4, comm_code) < 0)
      goto error_shared;
  }
  my_size_shared_buf = shmem_size - barriers_size * 4;
  shmem_size -= barriers_size;
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(recvbuf)) {
    if (shmem_zero) {
      shmem_gpu = shmem;
      shmemid_gpu = NULL;
    } else {
      ext_mpi_gpu_setup_shared_memory(comm_row, my_cores_per_node_row,
                                      comm_column, my_cores_per_node_column,
                                      shmem_size - barriers_size, num_sockets_per_node, &shmemid_gpu, &shmem_gpu);
    }
  }
#endif
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  if (!global_ranks)
    goto error;
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
  if (comm_row != MPI_COMM_NULL) {
    tag = tag_max;
    PMPI_Allreduce(MPI_IN_PLACE, &tag, 1, MPI_INT, MPI_MAX, comm_row);
    if (comm_column != MPI_COMM_NULL) {
      PMPI_Allreduce(MPI_IN_PLACE, &tag, 1, MPI_INT, MPI_MAX, comm_column);
    }
    tag_max = tag + 1;
  } else {
    tag = 0;
  }
  code_size = ext_mpi_generate_byte_code(
      shmem, shmem_size, shmemid, buffer_in, (char *)sendbuf, (char *)recvbuf,
      my_size_shared_buf, barriers_size, locmem, reduction_op, global_ranks, NULL,
      sizeof(MPI_Comm), sizeof(MPI_Request), &shmem_comm_node_row, my_cores_per_node_row, &shmem_comm_node_column,
      my_cores_per_node_column, shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, tag);
  if (code_size < 0)
    goto error;
  ip = comm_code[handle] = (char *)malloc(code_size);
  if (!ip)
    goto error;
  if (ext_mpi_generate_byte_code(
          shmem, shmem_size, shmemid, buffer_in, (char *)sendbuf,
          (char *)recvbuf, my_size_shared_buf, barriers_size, locmem, reduction_op,
          global_ranks, ip, sizeof(MPI_Comm), sizeof(MPI_Request), &shmem_comm_node_row, my_cores_per_node_row,
          &shmem_comm_node_column, my_cores_per_node_column,
          shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, tag) < 0)
    goto error;
  if (alt) {
    ip = comm_code[handle + 1] = (char *)malloc(code_size);
    if (!ip)
      goto error;
    shmem_size = my_size_shared_buf + barriers_size * 4;
    if (shmem_zero) {
      shmemid = NULL;
    } else {
      if (ext_mpi_setup_shared_memory(&shmem_comm_node_row, &shmem_comm_node_column,
                                      comm_row, my_cores_per_node_row, comm_column,
                                      my_cores_per_node_column, &shmem_size, num_sockets_per_node,
                                      &shmemid, &shmem, 0, barriers_size * 4, comm_code) < 0)
        goto error_shared;
    }
    my_size_shared_buf = shmem_size - barriers_size * 4;
    shmem_size -= barriers_size;
    if (not_locmem) {
      locmem = (char *)malloc(locmem_size);
    }
    if (!locmem)
      goto error;
#ifdef GPU_ENABLED
    if (gpu_is_device_pointer(recvbuf)) {
      if (shmem_zero) {
        shmem_gpu = shmem;
        shmemid_gpu = NULL;
      } else {
        ext_mpi_gpu_setup_shared_memory(comm_row, my_cores_per_node_row,
                                        comm_column, my_cores_per_node_column,
                                        shmem_size - barriers_size, num_sockets_per_node, &shmemid_gpu, &shmem_gpu);
      }
    }
#endif
    if (ext_mpi_generate_byte_code(
          shmem, shmem_size, shmemid, buffer_in, (char *)sendbuf,
          (char *)recvbuf, my_size_shared_buf, barriers_size, locmem, reduction_op,
          global_ranks, ip, sizeof(MPI_Comm), sizeof(MPI_Request), &shmem_comm_node_row, my_cores_per_node_row,
          &shmem_comm_node_column, my_cores_per_node_column,
          shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, tag) < 0)
      goto error;
  } else {
    comm_code[handle + 1] = NULL;
  }
  if (shmem_zero) {
    free(shmem);
  }
  free(global_ranks);
  return handle;
error:
  ext_mpi_destroy_shared_memory(shmem_size, num_sockets_per_node, shmemid, shmem, comm_code[handle]);
  free(global_ranks);
  return ERROR_MALLOC;
error_shared:
  ext_mpi_destroy_shared_memory(shmem_size, num_sockets_per_node, shmemid, shmem, comm_code[handle]);
  free(global_ranks);
  return ERROR_SHMEM;
}

int EXT_MPI_Reduce_init_native(const void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *groups, int copyin, int *copyin_factors,
                               int alt, int bit, int waitany, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int coarse_count, *counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *buffer_temp, *str;
  int nbuffer1 = 0, msize, *msizes = NULL, i, allreduce_short = (num_ports[0] > 0);
  int reduction_op;
  struct parameters_block *parameters;
  if (allreduce_short) {
    for (i = 0; groups[i]; i++) {
      if ((groups[i] < 0) && groups[i + 1]) {
        allreduce_short = 0;
      }
    }
  }
  if (bit) {
    allreduce_short = 0;
  }
allreduce_short = 0;
  buffer1 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer1)
    goto error;
  buffer2 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer2)
    goto error;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  if ((op == MPI_SUM) && (datatype == MPI_FLOAT)) {
    reduction_op = OPCODE_REDUCE_SUM_FLOAT;
  }
  if ((op == MPI_SUM) && (datatype == MPI_INT)) {
    reduction_op = OPCODE_REDUCE_SUM_INT;
  }
  MPI_Type_size(datatype, &type_size);
  count *= type_size;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  counts = (int *)malloc(my_cores_per_node_column * sizeof(int));
  if (!counts)
    goto error;
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(&count, &coarse_count, 1, MPI_INT, MPI_SUM, comm_column);
    PMPI_Allgather(&count, 1, MPI_INT, counts, 1, MPI_INT, comm_column);
  } else {
    coarse_count = count;
    counts[0] = count;
  }
  msize = 0;
  for (i = 0; i < my_cores_per_node_column; i++) {
    msize += counts[i];
  }
  if (!allreduce_short) {
    msizes =
        (int *)malloc(sizeof(int) * my_mpi_size_row / my_cores_per_node_row);
    if (!msizes)
      goto error;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes[i] =
          (msize / type_size) / (my_mpi_size_row / my_cores_per_node_row);
    }
    for (i = 0;
         i < (msize / type_size) % (my_mpi_size_row / my_cores_per_node_row);
         i++) {
      msizes[i]++;
    }
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes[i] *= type_size;
    }
  } else {
    msizes = (int *)malloc(sizeof(int) * 1);
    if (!msizes)
      goto error;
    msizes[0] = msize;
  }
  if (allreduce_short) {
    nbuffer1 += sprintf(buffer1 + nbuffer1,
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE_SHORT\n");
  } else {
    nbuffer1 += sprintf(buffer1 + nbuffer1,
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_SOCKETS %d\n",
                      my_mpi_size_row / my_cores_per_node_row);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_SOCKETS %d\n",
                      num_sockets_per_node);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_METHOD %d\n", copyin);
  if (copyin_factors) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_FACTORS");
    for (i = 0; copyin_factors[i]; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", copyin_factors[i]);
    }
    nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  }
  if ((root >= 0) || (root <= -10)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ROOT %d\n", root);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COUNTS");
  for (i = 0; i < my_cores_per_node_column; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", counts[i]);
  }
  free(counts);
  counts = NULL;
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  str = ext_mpi_print_ports_groups(num_ports, groups);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS %s\n", str);
  free(str);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER MESSAGE_SIZE");
  if (!allreduce_short) {
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", msizes[i]);
    }
  } else {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", msizes[0]);
    for (i = 1; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", 0);
    }
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  if (datatype == MPI_LONG) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE LONG_INT\n");
  }
  if (datatype == MPI_DOUBLE) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE DOUBLE\n");
  }
  if (datatype == MPI_INT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE INT\n");
  }
  if (datatype == MPI_FLOAT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE FLOAT\n");
  }
  if (bit) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER BIT_IDENTICAL\n");
  }
  if (sendbuf==recvbuf){
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER IN_PLACE\n");
  }
  if (blocking){
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER BLOCKING\n");
  }
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(recvbuf)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ON_GPU\n");
  }
#endif
  //nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
  free(msizes);
  msizes = NULL;
//  if (ext_mpi_generate_rank_permutation_forward(buffer1, buffer2) < 0)
//    goto error;
buffer_temp = buffer1;
buffer1 = buffer2;
buffer2 = buffer_temp;
  if (not_recursive) {
    if (ext_mpi_generate_allreduce(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_allreduce_recursive(buffer2, buffer1) < 0)
      goto error;
  }
  if (padding_factor) {
    ext_mpi_read_parameters(buffer1, &parameters);
    *padding_factor = ext_mpi_greatest_common_divisor(parameters->message_sizes_max, parameters->socket_row_size);
    ext_mpi_delete_parameters(parameters);
  }
//  if (ext_mpi_generate_rank_permutation_backward(buffer1, buffer2) < 0)
//    goto error;
buffer_temp = buffer1;
buffer1 = buffer2;
buffer2 = buffer_temp;
  if (my_mpi_size_row / my_cores_per_node_row > 1 && ((root >= 0) || (root <= -10))) {
    if (root >= 0) {
      if (ext_mpi_generate_backward_interpreter(buffer2, buffer1, comm_row) < 0)
        goto error;
    } else {
      if (ext_mpi_generate_forward_interpreter(buffer2, buffer1, comm_row) < 0)
        goto error;
    }
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(recvbuf)) {
#endif
    if (ext_mpi_generate_raw_code_tasks_node(buffer2, buffer1) < 0)
      goto error;
#ifdef GPU_ENABLED
  } else {
    if (ext_mpi_generate_raw_code_tasks_node_master(buffer2, buffer1) < 0)
      goto error;
  }
#endif
  } else {
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (ext_mpi_generate_reduce_copyin(buffer1, buffer2) < 0)
    goto error;
  if (my_mpi_size_row / my_cores_per_node_row > 1) {
    if (ext_mpi_generate_raw_code(buffer2, buffer1) < 0)
      goto error;
  } else {
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (ext_mpi_generate_reduce_copyout(buffer1, buffer2) < 0)
    goto error;
  /*
     int mpi_rank;
   MPI_Comm_rank(MPI_COMM_WORLD,&mpi_rank);
   if (mpi_rank ==0){
   printf("%s",buffer1);
   }
   exit(9);
   */
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (root > -10) {
    if (ext_mpi_generate_optimise_buffers(buffer2, buffer1) < 0)
      goto error;
    if (ext_mpi_generate_optimise_buffers2(buffer1, buffer2) < 0)
      goto error;
  }
  if (num_sockets_per_node > 1) {
    if (ext_mpi_messages_shared_memory(buffer2, buffer1, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column) < 0)
      goto error;
    if (ext_mpi_generate_optimise_multi_socket(buffer1, buffer2) < 0)
      goto error;
  }
  if (waitany&&!not_recursive) {
    if (ext_mpi_generate_waitany(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(recvbuf)) {
#endif
    if (ext_mpi_generate_parallel_memcpy(buffer2, buffer1) < 0)
      goto error;
    if (ext_mpi_generate_raw_code_merge(buffer1, buffer2) < 0)
      goto error;
#ifdef GPU_ENABLED
  }
#endif
  if (ext_mpi_clean_barriers(buffer2, buffer1, comm_row, comm_column) < 0)
    goto error;
  if (alt) {
    if (ext_mpi_generate_no_first_barrier(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_dummy(buffer1, buffer2) < 0)
      goto error;
  }
  if (my_cores_per_node_row*my_cores_per_node_column == 1){
    if (ext_mpi_generate_no_socket_barriers(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (root > -10 && !not_recursive && my_cores_per_node_row * my_cores_per_node_column == 1) {
    if (ext_mpi_generate_use_sendbuf_recvbuf(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  iret = init_epilogue(buffer2, sendbuf, recvbuf, reduction_op, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem);
  free(buffer2);
  free(buffer1);
  return iret;
error:
  free(msizes);
  free(counts);
  free(buffer2);
  free(buffer1);
  return ERROR_MALLOC;
}

int EXT_MPI_Allreduce_init_native(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *groups, int copyin,
                                  int *copyin_factors,
                                  int alt, int bit, int waitany,
                                  int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor) {
  return (EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, -1, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, copyin, copyin_factors, alt, bit, waitany, not_recursive, blocking, num_sockets_per_node, shmem_zero, locmem, padding_factor));
}

int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int copyin, int *copyin_factors,
                              int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
  return (EXT_MPI_Reduce_init_native(
      buffer, buffer, count, datatype, MPI_OP_NULL, -10 - root, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, copyin, copyin_factors, alt, 0, 0, not_recursive, blocking, num_sockets_per_node, shmem_zero, locmem, NULL));
}

int EXT_MPI_Gatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype, int root,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *buffer_temp, *str;
  int nbuffer1 = 0, i, j;
  buffer1 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer1)
    goto error;
  buffer2 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer2)
    goto error;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  MPI_Type_size(sendtype, &type_size);
  if ((sendcount != recvcounts[my_mpi_rank_row]) || (sendtype != recvtype)) {
    printf("sendtype != recvtype not implemented\n");
    exit(2);
  }
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  coarse_counts =
      (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
  if (!coarse_counts)
    goto error;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    coarse_counts[i] = 0;
    for (j = 0; j < my_cores_per_node_row; j++) {
      coarse_counts[i] += recvcounts[i * my_cores_per_node_row + j] * type_size;
    }
  }
  local_counts = (int *)malloc(my_cores_per_node_row *
                               my_cores_per_node_column * sizeof(int));
  if (!local_counts)
    goto error;
  global_counts = (int *)malloc(my_cores_per_node_column * sizeof(int));
  if (!global_counts)
    goto error;
  j = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    j += recvcounts[i] * type_size;
  }
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                   my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                   comm_column);
    PMPI_Allgather(&recvcounts[my_node * my_cores_per_node_row],
                   my_cores_per_node_row, MPI_INT, local_counts,
                   my_cores_per_node_row, MPI_INT, comm_column);
    PMPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column);
  } else {
    for (i = 0; i < my_cores_per_node_row; i++) {
      local_counts[i] = recvcounts[my_node * my_cores_per_node_row + i];
    }
    global_counts[0] = j;
  }
  for (i = 0; i < my_cores_per_node_row * my_cores_per_node_column; i++) {
    local_counts[i] *= type_size;
  }
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COLLECTIVE_TYPE ALLGATHERV\n");
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_SOCKETS %d\n",
                      my_mpi_size_row / my_cores_per_node_row);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_SOCKETS %d\n",
                      num_sockets_per_node);
  if (root >= 0) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ROOT %d\n", root);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COUNTS");
  for (i = 0; i < my_cores_per_node_column; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", global_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(global_counts);
  global_counts = NULL;
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER IOCOUNTS");
  for (j = 0; j < my_cores_per_node_row; j++) {
    for (i = 0; i < my_cores_per_node_column; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d",
                          local_counts[i * my_cores_per_node_row + j]);
    }
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(local_counts);
  local_counts = NULL;
  str = ext_mpi_print_ports_groups(num_ports, groups);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS %s\n", str);
  free(str);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER MESSAGE_SIZE");
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", coarse_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(coarse_counts);
  coarse_counts = NULL;
  if (sendtype == MPI_LONG) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE LONG_INT\n");
  }
  if (sendtype == MPI_DOUBLE) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE DOUBLE\n");
  }
  if (sendtype == MPI_INT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE INT\n");
  }
  if (sendtype == MPI_FLOAT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE FLOAT\n");
  }
  if (blocking){
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER BLOCKING\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(recvbuf)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ON_GPU\n");
  }
#endif
  j = 0;
  for (i = 0; groups[i]; i++) {
    if (groups[i] < 0) j++;
  }
  if (j <= 1) {
    if (ext_mpi_generate_rank_permutation_forward(buffer1, buffer2) < 0)
      goto error;
  } else {
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (not_recursive) {
    if (ext_mpi_generate_allreduce(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_allreduce_recursive(buffer2, buffer1) < 0)
      goto error;
  }
  if (j <= 1) {
    if (ext_mpi_generate_rank_permutation_backward(buffer1, buffer2) < 0)
      goto error;
  } else {
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (root >= 0) {
    if (ext_mpi_generate_backward_interpreter(buffer2, buffer1, comm_row) < 0)
      goto error;
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(recvbuf)) {
#endif
    if (ext_mpi_generate_raw_code_tasks_node(buffer2, buffer1) < 0)
      goto error;
#ifdef GPU_ENABLED
  } else {
    if (ext_mpi_generate_raw_code_tasks_node_master(buffer2, buffer1) < 0)
      goto error;
  }
#endif
  if (ext_mpi_generate_reduce_copyin(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_raw_code(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_reduce_copyout(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(recvbuf)) {
#endif
    if (ext_mpi_generate_parallel_memcpy(buffer2, buffer1) < 0)
      goto error;
    if (ext_mpi_generate_raw_code_merge(buffer1, buffer2) < 0)
      goto error;
#ifdef GPU_ENABLED
  }
#endif
  if (!not_recursive && my_cores_per_node_row * my_cores_per_node_column == 1){
    if (ext_mpi_generate_use_recvbuf(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (alt) {
    if (ext_mpi_generate_no_first_barrier(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_dummy(buffer2, buffer1) < 0)
      goto error;
  }
  if (ext_mpi_clean_barriers(buffer1, buffer2, comm_row, comm_column) < 0)
    goto error;
  if (my_cores_per_node_row*my_cores_per_node_column == 1){
    if (ext_mpi_generate_no_socket_barriers(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (!not_recursive && my_cores_per_node_row * my_cores_per_node_column == 1){
    if (ext_mpi_generate_swap_copyin(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  iret = init_epilogue(buffer2, sendbuf, recvbuf, OPCODE_REDUCE_SUM_CHAR, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem);
  free(buffer2);
  free(buffer1);
  return iret;
error:
  free(coarse_counts);
  free(global_counts);
  free(local_counts);
  free(buffer2);
  free(buffer1);
  return ERROR_MALLOC;
}

int EXT_MPI_Allgatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
  return (EXT_MPI_Gatherv_init_native(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, -1,
      comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
      num_ports, groups, alt, not_recursive, blocking, num_sockets_per_node, shmem_zero, locmem));
}

int EXT_MPI_Scatterv_init_native(const void *sendbuf, const int *sendcounts,
                                 const int *displs, MPI_Datatype sendtype,
                                 void *recvbuf, int recvcount,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups,
                                 int copyin, int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *str, *buffer_temp;
  int nbuffer1 = 0, i, j;
  buffer1 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer1)
    goto error;
  buffer2 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer2)
    goto error;
  MPI_Type_size(sendtype, &type_size);
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  coarse_counts =
      (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
  if (!coarse_counts)
    goto error;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    coarse_counts[i] = 0;
    for (j = 0; j < my_cores_per_node_row; j++) {
      coarse_counts[i] += sendcounts[i * my_cores_per_node_row + j] * type_size;
    }
  }
  local_counts = (int *)malloc(my_cores_per_node_row *
                               my_cores_per_node_column * sizeof(int));
  if (!local_counts)
    goto error;
  global_counts = (int *)malloc(my_cores_per_node_column * sizeof(int));
  if (!global_counts)
    goto error;
  j = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    j += sendcounts[i] * type_size;
  }
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                   my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                   comm_column);
    PMPI_Allgather(&sendcounts[my_node * my_cores_per_node_row],
                   my_cores_per_node_row, MPI_INT, local_counts,
                   my_cores_per_node_row, MPI_INT, comm_column);
    PMPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column);
  } else {
    for (i = 0; i < my_cores_per_node_row; i++) {
      local_counts[i] = sendcounts[my_node * my_cores_per_node_row + i];
    }
  }
  global_counts[0] = j;
  for (i = 0; i < my_cores_per_node_row * my_cores_per_node_column; i++) {
    local_counts[i] *= type_size;
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1,
                      " PARAMETER COLLECTIVE_TYPE REDUCE_SCATTER\n");
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_SOCKETS %d\n",
                      my_mpi_size_row / my_cores_per_node_row);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_SOCKETS %d\n",
                      num_sockets_per_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ROOT %d\n", root);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_METHOD %d\n", copyin);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COUNTS");
  for (i = 0; i < my_cores_per_node_column; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", global_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(global_counts);
  global_counts = NULL;
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER IOCOUNTS");
  for (j = 0; j < my_cores_per_node_row; j++) {
    for (i = 0; i < my_cores_per_node_column; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d",
                          local_counts[i * my_cores_per_node_row + j]);
    }
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(local_counts);
  local_counts = NULL;
  for (i = 0; num_ports[i]; i++)
    ;
  str = ext_mpi_print_ports_groups(num_ports, groups);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS %s\n", str);
  free(str);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER MESSAGE_SIZE");
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", coarse_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(coarse_counts);
  coarse_counts = NULL;
  if (sendtype == MPI_LONG) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE LONG_INT\n");
  }
  if (sendtype == MPI_DOUBLE) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE DOUBLE\n");
  }
  if (sendtype == MPI_INT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE INT\n");
  }
  if (sendtype == MPI_FLOAT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE FLOAT\n");
  }
  if (blocking){
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER BLOCKING\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(recvbuf)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ON_GPU\n");
  }
#endif
  j = 0;
  for (i = 0; groups[i]; i++) {
    if (groups[i] < 0) j++;
  }
  if (j <= 1) {
    if (ext_mpi_generate_rank_permutation_forward(buffer1, buffer2) < 0)
      goto error;
  } else {
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (ext_mpi_generate_allreduce(buffer2, buffer1) < 0)
    goto error;
  if (j <= 1) {
    if (ext_mpi_generate_rank_permutation_backward(buffer1, buffer2) < 0)
      goto error;
  } else {
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (ext_mpi_generate_forward_interpreter(buffer2, buffer1, comm_row) < 0)
    goto error;
  if (ext_mpi_generate_raw_code_tasks_node(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_reduce_copyin(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_raw_code(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_reduce_copyout(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_buffer_offset(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_no_offset(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers2(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_parallel_memcpy(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_raw_code_merge(buffer2, buffer1) < 0)
    goto error;
  if (alt) {
    if (ext_mpi_generate_no_first_barrier(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_dummy(buffer1, buffer2) < 0)
      goto error;
  }
  if (ext_mpi_clean_barriers(buffer2, buffer1, comm_row, comm_column) < 0)
    goto error;
  if (my_cores_per_node_row*my_cores_per_node_column == 1){
    if (ext_mpi_generate_no_socket_barriers(buffer1, buffer2) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  iret = init_epilogue(buffer1, sendbuf, recvbuf, -1, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem);
  free(buffer2);
  free(buffer1);
  return iret;
error:
  free(local_counts);
  free(global_counts);
  free(coarse_counts);
  free(buffer2);
  free(buffer1);
  return ERROR_MALLOC;
}

int EXT_MPI_Reduce_scatter_init_native(
    const void *sendbuf, void *recvbuf, const int *recvcounts,
    MPI_Datatype datatype, MPI_Op op, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int copyin, int *copyin_factors, int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *str, *buffer_temp;
  int nbuffer1 = 0, i, j;
  int reduction_op;
  struct parameters_block *parameters;
  buffer1 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer1)
    goto error;
  buffer2 = (char *)malloc(MAX_BUFFER_SIZE);
  if (!buffer2)
    goto error;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  if ((op == MPI_SUM) && (datatype == MPI_FLOAT)) {
    reduction_op = OPCODE_REDUCE_SUM_FLOAT;
  }
  if ((op == MPI_SUM) && (datatype == MPI_INT)) {
    reduction_op = OPCODE_REDUCE_SUM_INT;
  }
  MPI_Type_size(datatype, &type_size);
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  coarse_counts =
      (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
  if (!coarse_counts)
    goto error;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    coarse_counts[i] = 0;
    for (j = 0; j < my_cores_per_node_row; j++) {
      coarse_counts[i] += recvcounts[i * my_cores_per_node_row + j] * type_size;
    }
  }
  local_counts = (int *)malloc(my_cores_per_node_row *
                               my_cores_per_node_column * sizeof(int));
  if (!local_counts)
    goto error;
  global_counts = (int *)malloc(my_cores_per_node_column * sizeof(int));
  if (!global_counts)
    goto error;
  j = 0;
  for (i = 0; i < my_mpi_size_row; i++) {
    j += recvcounts[i] * type_size;
  }
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                   my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                   comm_column);
    PMPI_Allgather(&recvcounts[my_node * my_cores_per_node_row],
                   my_cores_per_node_row, MPI_INT, local_counts,
                   my_cores_per_node_row, MPI_INT, comm_column);
    PMPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column);
  } else {
    for (i = 0; i < my_cores_per_node_row; i++) {
      local_counts[i] = recvcounts[my_node * my_cores_per_node_row + i];
    }
  }
  global_counts[0] = j;
  for (i = 0; i < my_cores_per_node_row * my_cores_per_node_column; i++) {
    local_counts[i] *= type_size;
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1,
                      " PARAMETER COLLECTIVE_TYPE REDUCE_SCATTER\n");
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_SOCKETS %d\n",
                      my_mpi_size_row / my_cores_per_node_row);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_SOCKETS %d\n",
                      num_sockets_per_node);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_METHOD %d\n", copyin);
  if (copyin_factors) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COPYIN_FACTORS");
    for (i = 0; copyin_factors[i]; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", copyin_factors[i]);
    }
    nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COUNTS");
  for (i = 0; i < my_cores_per_node_column; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", global_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(global_counts);
  global_counts = NULL;
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER IOCOUNTS");
  for (j = 0; j < my_cores_per_node_row; j++) {
    for (i = 0; i < my_cores_per_node_column; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d",
                          local_counts[i * my_cores_per_node_row + j]);
    }
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(local_counts);
  local_counts = NULL;
  for (i = 0; num_ports[i]; i++)
    ;
  str = ext_mpi_print_ports_groups(num_ports, groups);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS %s\n", str);
  free(str);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER MESSAGE_SIZE");
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", coarse_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(coarse_counts);
  coarse_counts = NULL;
  if (datatype == MPI_LONG) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE LONG_INT\n");
  }
  if (datatype == MPI_DOUBLE) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE DOUBLE\n");
  }
  if (datatype == MPI_INT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE INT\n");
  }
  if (datatype == MPI_FLOAT) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER DATA_TYPE FLOAT\n");
  }
  if (blocking){
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER BLOCKING\n");
  }
  //nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(recvbuf)) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ON_GPU\n");
  }
#endif
  if (ext_mpi_generate_rank_permutation_forward(buffer1, buffer2) < 0)
    goto error;
  if (not_recursive) {
    if (ext_mpi_generate_allreduce(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_allreduce_recursive(buffer2, buffer1) < 0)
      goto error;
  }
  if (padding_factor) {
    ext_mpi_read_parameters(buffer1, &parameters);
    *padding_factor = parameters->socket_row_size;
    ext_mpi_delete_parameters(parameters);
  }
  if (ext_mpi_generate_rank_permutation_backward(buffer1, buffer2) < 0)
    goto error;
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
#ifdef GPU_ENABLED
    if (!gpu_is_device_pointer(recvbuf)) {
#endif
      if (ext_mpi_generate_raw_code_tasks_node(buffer2, buffer1) < 0)
        goto error;
#ifdef GPU_ENABLED
    } else {
      if (ext_mpi_generate_raw_code_tasks_node_master(buffer2, buffer1) < 0)
        goto error;
    }
#endif
  }
  if (my_mpi_size_row / my_cores_per_node_row > 1) {
    if (ext_mpi_generate_reduce_copyin(buffer1, buffer2) < 0)
      goto error;
    if (my_mpi_size_row / my_cores_per_node_row > 1) {
      if (ext_mpi_generate_raw_code(buffer2, buffer1) < 0)
        goto error;
    } else {
      buffer_temp = buffer2;
      buffer2 = buffer1;
      buffer1 = buffer_temp;
    }
    if (ext_mpi_generate_reduce_copyout(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_reduce_scatter_single_node(buffer1, buffer2) < 0)
      goto error;
  }
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(recvbuf)) {
#endif
    if (ext_mpi_generate_parallel_memcpy(buffer2, buffer1) < 0)
      goto error;
    if (ext_mpi_generate_raw_code_merge(buffer1, buffer2) < 0)
      goto error;
#ifdef GPU_ENABLED
  }
#endif
  if (alt) {
    if (ext_mpi_generate_no_first_barrier(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_dummy(buffer2, buffer1) < 0)
      goto error;
  }
  if (ext_mpi_clean_barriers(buffer1, buffer2, comm_row, comm_column) < 0)
    goto error;
  if (my_cores_per_node_row*my_cores_per_node_column == 1){
    if (ext_mpi_generate_no_socket_barriers(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (!not_recursive && my_cores_per_node_row * my_cores_per_node_column == 1) {
    if (ext_mpi_generate_use_sendbuf_recvbuf(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  iret = init_epilogue(buffer2, sendbuf, recvbuf, reduction_op, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem);
  free(buffer2);
  free(buffer1);
  return iret;
error:
  free(local_counts);
  free(global_counts);
  free(coarse_counts);
  free(buffer2);
  free(buffer1);
  return ERROR_MALLOC;
}

int EXT_MPI_Init_native() {
  PMPI_Comm_dup(MPI_COMM_WORLD, &ext_mpi_COMM_WORLD_dup);
  is_initialised = 1;
  return 0;
}

int EXT_MPI_Initialized_native() { return is_initialised; }

int EXT_MPI_Finalize_native() {
  PMPI_Comm_free(&ext_mpi_COMM_WORLD_dup);
  return 0;
}

void ext_mpi_native_export(int *e_handle_code_max, char ***e_comm_code, char ***e_execution_pointer, int **e_active_wait, int *e_is_initialised, MPI_Comm *e_ext_mpi_COMM_WORLD_dup, int *e_tag_max, void (*e_socket_barrier)(char *shmem, int *barrier_count, int socket_rank, int num_cores), void (*e_node_barrier)(char **shmem, int *barrier_count, int socket_rank, int num_sockets_per_node), void (*e_socket_barrier_atomic_set)(char *shmem, int barrier_count, int entry), void (*e_socket_barrier_atomic_wait)(char *shmem, int *barrier_count, int entry)){
  e_handle_code_max = &handle_code_max;
  e_comm_code = &comm_code;
  e_execution_pointer = &execution_pointer;
  e_active_wait = &active_wait;
  e_is_initialised = &is_initialised;
  e_ext_mpi_COMM_WORLD_dup = &ext_mpi_COMM_WORLD_dup;
  e_tag_max = &tag_max;
}
