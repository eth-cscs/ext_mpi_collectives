#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dlfcn.h>
#include <assert.h>
#include "prime_factors.h"
#include "allreduce.h"
#include "allreduce_recursive.h"
#include "allreduce_short.h"
#include "alltoall.h"
#include "backward_interpreter.h"
#include "buffer_offset.h"
#include "byte_code.h"
#include "source_code.h"
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
#include "no_waitall_zero.h"
#include "waitany.h"
#include "reduce_scatter_single_node.h"
#include "padding_factor.h"
#include "shmem.h"
#include "ext_mpi_xpmem.h"
#include "ext_mpi_native_exec.h"
#include "reduce_copyin.h"
#include "hash_table_operator.h"
#include "count_mem_partners.h"
#include "memory_manager.h"
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

static int handle_code_max = 100;
static char **comm_code = NULL;
static int *execution_pointer_func = NULL;
static char **execution_pointer = NULL;
static int *active_wait = NULL;
static int is_initialised = 0;
static int tag_max = 0;
static void *dlhandle = NULL;
static void **shmem_root_cpu;
#ifdef GPU_ENABLED
static void **shmem_root_gpu;
#endif

extern int ext_mpi_fast;
extern int ext_mpi_verbose;

MPI_Comm ext_mpi_COMM_WORLD_dup = MPI_COMM_NULL;

/*static int global_min(int i, MPI_Comm comm_row, MPI_Comm comm_column) {
  ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_column));
  }
  return (i);
}*/

#ifdef GPU_ENABLED
static int gpu_is_device_pointer(void *buf) {
  if (SEND_PTR_CPU == ((unsigned long int)buf & 0xF000000000000000) || RECV_PTR_CPU == ((unsigned long int)buf & 0xF000000000000000)) return 0;
  if (SEND_PTR_GPU == ((unsigned long int)buf & 0xF000000000000000) || RECV_PTR_GPU == ((unsigned long int)buf & 0xF000000000000000)) return 1;
  return ext_mpi_gpu_is_device_pointer(buf);
}
#endif

void ext_mpi_call_mpi(int i) {
  char string[1000];
  int resultlen;
  if (i != MPI_SUCCESS) {
    MPI_Error_string(i, string, &resultlen);
    printf("%s\n", string);
  }
  assert(i == MPI_SUCCESS);
}

static int setup_rank_translation(MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column,
                                  int *global_ranks) {
  MPI_Comm my_comm_node;
  int my_mpi_size_row, grank, my_mpi_size_column, my_mpi_rank_column,
      *lglobal_ranks = NULL;
  ext_mpi_call_mpi(PMPI_Comm_size(comm_row, &my_mpi_size_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(PMPI_Comm_size(comm_column, &my_mpi_size_column));
    ext_mpi_call_mpi(PMPI_Comm_rank(comm_column, &my_mpi_rank_column));
    ext_mpi_call_mpi(PMPI_Comm_split(comm_column,
                        my_mpi_rank_column / my_cores_per_node_column,
                        my_mpi_rank_column % my_cores_per_node_column,
                        &my_comm_node));
    ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &grank));
    lglobal_ranks = (int *)malloc(sizeof(int) * my_cores_per_node_column);
    if (!lglobal_ranks)
      goto error;
    ext_mpi_call_mpi(PMPI_Gather(&grank, 1, MPI_INT, lglobal_ranks, 1, MPI_INT, 0, my_comm_node));
    ext_mpi_call_mpi(PMPI_Bcast(lglobal_ranks, my_cores_per_node_column, MPI_INT, 0,
               my_comm_node));
    ext_mpi_call_mpi(PMPI_Barrier(my_comm_node));
    ext_mpi_call_mpi(PMPI_Comm_free(&my_comm_node));
    ext_mpi_call_mpi(PMPI_Gather(lglobal_ranks, my_cores_per_node_column, MPI_INT, global_ranks,
                     my_cores_per_node_column, MPI_INT, 0, comm_row));
    free(lglobal_ranks);
  } else {
    ext_mpi_call_mpi(PMPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &grank));
    ext_mpi_call_mpi(PMPI_Gather(&grank, 1, MPI_INT, global_ranks, 1, MPI_INT, 0, comm_row));
  }
  ext_mpi_call_mpi(PMPI_Bcast(global_ranks, my_mpi_size_row * my_cores_per_node_column, MPI_INT,
                   0, comm_row));
  return 0;
error:
  free(lglobal_ranks);
  return ERROR_MALLOC;
}

int EXT_MPI_Get_handle() {
  char **comm_code_old = NULL, **execution_pointer_old = NULL;
  int *active_wait_old = NULL, handle, *execution_pointer_func_old, i;
  if (comm_code == NULL) {
    comm_code = (char **)malloc(sizeof(char *) * handle_code_max);
    if (!comm_code)
      goto error;
    execution_pointer_func = (int *)malloc(sizeof(int) * handle_code_max);
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
      execution_pointer_func_old = execution_pointer_func;
      execution_pointer_old = execution_pointer;
      active_wait_old = active_wait;
      handle_code_max *= 2;
      comm_code = (char **)malloc(sizeof(char *) * handle_code_max);
      if (!comm_code)
        goto error;
      execution_pointer_func = (int *)malloc(sizeof(int) * handle_code_max);
      if (!execution_pointer_func)
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
        execution_pointer_func[i] = execution_pointer_func_old[i];
        execution_pointer[i] = execution_pointer_old[i];
        active_wait[i] = active_wait_old[i];
      }
      free(active_wait_old);
      free(execution_pointer_func_old);
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
  active_wait[handle] = 0;
  execution_pointer_func[handle] = 0;
  execution_pointer[handle] =
      comm_code[handle] + sizeof(struct header_byte_code);
  return (0);
}

int EXT_MPI_Test_native(int handle) {
  if (((struct header_byte_code *)(comm_code[handle]))->function) {
    if (execution_pointer_func[handle] != -1) {
      execution_pointer_func[handle] = ((int (*)(int, int))(((struct header_byte_code *)(comm_code[handle]))->function))(execution_pointer_func[handle], 0);
      if (!execution_pointer_func[handle]) {
	execution_pointer_func[handle] = -1;
      }
    }
    return (execution_pointer_func[handle] != -1);
  } else {
    active_wait[handle] = 1;
    if (execution_pointer[handle]) {
      ext_mpi_exec_native(comm_code[handle], &execution_pointer[handle],
                          active_wait[handle]);
    }
    return (execution_pointer[handle] == NULL);
  }
}

int EXT_MPI_Wait_native(int handle) {
  if (!active_wait[handle]) {
    active_wait[handle] = 3;
  } else {
    active_wait[handle] = 2;
  }
  if (((struct header_byte_code *)(comm_code[handle]))->function) {
    execution_pointer_func[handle] = ((int (*)(int, int))(((struct header_byte_code *)(comm_code[handle]))->function))(execution_pointer_func[handle], 1);
  } else {
    if (execution_pointer[handle]) {
      ext_mpi_exec_native(comm_code[handle], &execution_pointer[handle],
                          active_wait[handle]);
    }
  }
  active_wait[handle] = 0;
  return 0;
}

int EXT_MPI_Done_native(int handle) {
  char **shmem;
  char *ip, *locmem;
  int *shmem_sizes, *shmemid, i;
  struct header_byte_code *header;
#ifdef GPU_ENABLED
  int gpu_is_device_p;
#endif
  EXT_MPI_Wait_native(handle);
  ip = comm_code[handle + 1];
  if (ip) {
    header = (struct header_byte_code *)ip;
    shmem = header->shmem;
    shmemid = header->shmemid;
    shmem_sizes = header->shmem_sizes;
    locmem = header->locmem;
#ifdef GPU_ENABLED
    if (header->shmem_gpu) {
      if (header->shmemid_gpu && header->shmem_sizes) {
        ext_mpi_gpu_destroy_shared_memory(header->num_sockets_per_node * header->node_num_cores_row, header->ranks_node, header->shmemid_gpu, header->shmem_gpu);
      }
      header->shmem_gpu = NULL;
      header->shmemid_gpu = NULL;
    }
//    if (header->gpu_gemv_var.handle) {
//      ext_mpi_gemv_done(&header->gpu_gemv_var);
//    }
#endif
    ext_mpi_destroy_shared_memory(header->num_sockets_per_node * header->node_num_cores_row, header->ranks_node, shmem_sizes, shmemid, shmem);
#ifdef GPU_ENABLED
    if (ext_mpi_gpu_is_device_pointer(header->gpu_byte_code)) {
      ext_mpi_gpu_free(header->gpu_byte_code);
      free(header->ranks_node);
    } else {
      free(header->gpu_byte_code);
    }
    gpu_is_device_p = gpu_is_device_pointer(header->recvbufs[0]);
    if (!gpu_is_device_p) {
#endif
#ifndef XPMEM
    free(header->sendbufs);
    free(header->recvbufs);
#else
    ext_mpi_sendrecvbuf_done_xpmem(header->node_num_cores_row * header->num_sockets_per_node, header->sendbufs);
    ext_mpi_sendrecvbuf_done_xpmem(header->node_num_cores_row * header->num_sockets_per_node, header->recvbufs);
#endif
#ifdef GPU_ENABLED
    }
#endif
    free(locmem);
    free(((struct header_byte_code *)comm_code[handle + 1])->barrier_shmem_node);
    free(((struct header_byte_code *)comm_code[handle + 1])->barrier_shmem_socket);
    free(((struct header_byte_code *)comm_code[handle + 1])->barrier_shmem_socket_small);
    free(comm_code[handle + 1]);
    comm_code[handle + 1] = NULL;
  }
  ip = comm_code[handle];
  header = (struct header_byte_code *)ip;
  shmem = header->shmem;
  shmemid = header->shmemid;
  shmem_sizes = header->shmem_sizes;
  locmem = header->locmem;
#ifdef GPU_ENABLED
  if (header->shmem_gpu) {
    if (header->shmemid_gpu && header->shmem_sizes) {
      ext_mpi_gpu_destroy_shared_memory(header->num_sockets_per_node * header->node_num_cores_row, header->ranks_node, header->shmemid_gpu, header->shmem_gpu);
    }
    header->shmem_gpu = NULL;
    header->shmemid_gpu = NULL;
  }
//  if (header->gpu_gemv_var.handle) {
//    ext_mpi_gemv_done(&header->gpu_gemv_var);
//  }
#endif
  ext_mpi_destroy_shared_memory(header->num_sockets_per_node * header->node_num_cores_row, header->ranks_node, shmem_sizes, shmemid, shmem);
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(header->gpu_byte_code)) {
    ext_mpi_gpu_free(header->gpu_byte_code);
    free(header->ranks_node);
  } else {
    free(header->gpu_byte_code);
  }
  gpu_is_device_p = gpu_is_device_pointer(header->recvbufs[0]);
  if (gpu_is_device_p) {
    ext_mpi_sendrecvbuf_done_gpu(header->node_num_cores_row * header->num_sockets_per_node, header->sendbufs);
    ext_mpi_sendrecvbuf_done_gpu(header->node_num_cores_row * header->num_sockets_per_node, header->recvbufs);
    header->sendbufs = header->recvbufs = NULL;
  } else {
#endif
#ifndef XPMEM
  free(header->sendbufs);
  free(header->recvbufs);
#else
  ext_mpi_sendrecvbuf_done_xpmem(header->node_num_cores_row * header->num_sockets_per_node, header->sendbufs);
  ext_mpi_sendrecvbuf_done_xpmem(header->node_num_cores_row * header->num_sockets_per_node, header->recvbufs);
#endif
#ifdef GPU_ENABLED
  }
#endif
  free(locmem);
  free(((struct header_byte_code *)comm_code[handle])->barrier_shmem_node);
  free(((struct header_byte_code *)comm_code[handle])->barrier_shmem_socket);
  free(((struct header_byte_code *)comm_code[handle])->barrier_shmem_socket_small);
  free(comm_code[handle]);
  comm_code[handle] = NULL;
  for (i = 0; i < handle_code_max; i++) {
    if (comm_code[i] != NULL) {
      return 0;
    }
  }
  free(active_wait);
  free(execution_pointer_func);
  free(execution_pointer);
  free(comm_code);
  comm_code = NULL;
  execution_pointer = NULL;
  active_wait = NULL;
  return 0;
}

static int init_epilogue(char *buffer_in, const void *sendbuf, void *recvbuf,
                         int reduction_op, MPI_User_function *func, MPI_Comm comm_row,
                         int my_cores_per_node_row, MPI_Comm comm_column,
                         int my_cores_per_node_column, int alt, int shmem_zero, char *locmem, int *mem_partners_send, int *mem_partners_recv) {
  int i, num_comm_max = -1, barriers_size = 1, nbuffer_in = 0, tag = -1, not_locmem;
  char *ip, **sendbufs = NULL, **recvbufs = NULL, str[1000];
  int handle, *global_ranks = NULL, code_size, my_mpi_size_row, my_mpi_rank, socket_rank, flag;
  int locmem_size, shmem_size = 0, *shmem_sizes = NULL, *shmemid = NULL, num_sockets_per_node = 1, *ranks_node = NULL;
  char **shmem = NULL;
  int gpu_byte_code_counter = 0;
  char **shmem_gpu = NULL;
  int *shmemid_gpu = NULL;
  struct parameters_block *parameters;
  MPI_Comm comm_node;
#if defined GPU_ENABLED || defined XPMEM
  int counts_send, counts_recv;
#endif
  not_locmem = (locmem == NULL);
  handle = EXT_MPI_Get_handle();
  nbuffer_in += i = ext_mpi_read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  num_sockets_per_node = parameters->num_sockets_per_node;
  ext_mpi_call_mpi(MPI_Comm_size(comm_row, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_row, &my_mpi_rank));
  ext_mpi_call_mpi(PMPI_Comm_split(comm_row, my_mpi_rank / (my_cores_per_node_row * num_sockets_per_node), my_mpi_rank % (my_cores_per_node_row * num_sockets_per_node), &comm_node));
  ranks_node = (int*)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  ext_mpi_call_mpi(MPI_Comm_rank(ext_mpi_COMM_WORLD_dup, &i));
  ext_mpi_call_mpi(PMPI_Allgather(&i, 1, MPI_INT, ranks_node, 1, MPI_INT, comm_node));
  ext_mpi_call_mpi(PMPI_Comm_free(&comm_node));
  num_comm_max = parameters->locmem_max;
  shmem_size = parameters->shmem_max;
  socket_rank = parameters->socket_rank;
  code_size = ext_mpi_generate_byte_code(
      shmem, shmemid, shmem_sizes, buffer_in, sendbufs, recvbufs,
      barriers_size, locmem, reduction_op, (void *)func, global_ranks, NULL,
      sizeof(MPI_Request), ranks_node, my_cores_per_node_row,
      my_cores_per_node_column, shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, 0, tag);
  flag = code_size == -7;
  ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MAX, comm_row));
  if (flag) {
    if (ext_mpi_verbose && my_mpi_rank == 0) {
      printf("# fallback gpu_memcpy\n");
    }
    code_size = ext_mpi_generate_byte_code(
        shmem, shmemid, shmem_sizes, buffer_in, sendbufs, recvbufs,
        barriers_size, locmem, reduction_op, (void *)func, global_ranks, NULL,
        sizeof(MPI_Request), ranks_node, my_cores_per_node_row,
        my_cores_per_node_column, shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, 1, tag);
  }
  if (code_size < 0)
    goto error;
#if defined GPU_ENABLED || defined XPMEM
  counts_send = 0;
  for (i = 0; i < parameters->counts_max; i++) {
    counts_send += parameters->counts[i];
  }
  counts_recv = counts_send;
  if (parameters->collective_type == collective_type_reduce_scatter) {
    counts_recv = 0;
  }
#endif
  locmem_size = num_comm_max * sizeof(MPI_Request);
  if (not_locmem) {
    locmem = (char *)malloc(locmem_size);
  }
  if (!locmem)
    goto error;
  barriers_size = sizeof(int);
  if (CACHE_LINE_SIZE > barriers_size) barriers_size = CACHE_LINE_SIZE;
  shmem_size += barriers_size * 2;
  if (shmem_zero) {
    shmem_sizes = (int*)malloc(sizeof(int) * 0x1000);
    shmem = (char **)malloc(sizeof(char *) * 0x1000);
    sendbufs = (char **)malloc(sizeof(char *) * 0x1000);
    recvbufs = (char **)malloc(sizeof(char *) * 0x1000);
    for (i = 0; i < 0x1000; i++) {
      shmem_sizes[i] = shmem_size;
      shmem[i] = (char *)(((unsigned long int)i) << 48) + SHMEM_PTR_CPU;
      sendbufs[i] = (char *)(((unsigned long int)i) << 48) + SEND_PTR_CPU;
      recvbufs[i] = (char *)(((unsigned long int)i) << 48) + RECV_PTR_CPU;
    }
#ifdef GPU_ENABLED
    if (gpu_is_device_pointer(recvbuf)) {
      for (i = 0; i < 0x1000; i++) {
        shmem[i] = (char *)(((unsigned long int)i) << 48) + SHMEM_PTR_GPU;
        sendbufs[i] = (char *)(((unsigned long int)i) << 48) + SEND_PTR_GPU;
        recvbufs[i] = (char *)(((unsigned long int)i) << 48) + RECV_PTR_GPU;
       }
     }
#endif
    shmemid = NULL;
  } else {
    if (ext_mpi_setup_shared_memory(comm_row, my_cores_per_node_row, num_sockets_per_node,
                                    shmem_size, 1, &shmem_sizes, &shmemid, &shmem) < 0)
      goto error_shared;
  }
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf) || (recvbufs && ((unsigned long int)recvbufs[0] & 0xF000000000000000) == RECV_PTR_GPU)) {
    if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
      ext_mpi_sendrecvbuf_init_gpu(comm_row, num_sockets_per_node * my_cores_per_node_row, num_sockets_per_node, (char *)sendbuf, counts_send, &sendbufs, mem_partners_send);
      ext_mpi_sendrecvbuf_init_gpu(comm_row, num_sockets_per_node * my_cores_per_node_row, num_sockets_per_node, recvbuf, counts_recv, &recvbufs, mem_partners_recv);
    }
    if (shmem_zero) {
      shmem_gpu = shmem;
      shmemid_gpu = NULL;
    } else {
      ext_mpi_gpu_setup_shared_memory(comm_row, my_cores_per_node_row,
                                      shmem_size - barriers_size * 2, num_sockets_per_node, &shmemid_gpu, &shmem_gpu);
    }
  } else {
#endif
  if (!shmem_zero) {
#ifndef XPMEM
    sendbufs = (char **)malloc(num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
    recvbufs = (char **)malloc(num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
    memset(sendbufs, 0, num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
    memset(recvbufs, 0, num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
    sendbufs[0] = (char *)sendbuf;
    recvbufs[0] = recvbuf;
#else
    ext_mpi_sendrecvbuf_init_xpmem(comm_row, num_sockets_per_node * my_cores_per_node_row, num_sockets_per_node, (char *)sendbuf, counts_send, &sendbufs);
    ext_mpi_sendrecvbuf_init_xpmem(comm_row, num_sockets_per_node * my_cores_per_node_row, num_sockets_per_node, recvbuf, counts_recv, &recvbufs);
#endif
  }
#ifdef GPU_ENABLED
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
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &tag, 1, MPI_INT, MPI_MAX, comm_row));
    if (comm_column != MPI_COMM_NULL) {
      ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, &tag, 1, MPI_INT, MPI_MAX, comm_column));
    }
    tag_max = tag + 1;
  } else {
    tag = 0;
  }
  ip = comm_code[handle] = (char *)malloc(code_size);
  if (!ip)
    goto error;
  if (ext_mpi_fast) {
  if (ext_mpi_generate_source_code(
          shmem, shmemid, shmem_sizes, buffer_in, sendbufs,
          recvbufs, barriers_size, locmem, reduction_op, (void *)func,
          global_ranks, ip, sizeof(MPI_Comm), sizeof(MPI_Request), NULL, my_cores_per_node_row,
          NULL, my_cores_per_node_column,
          shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, tag, handle) < 0)
    goto error;
  } else {
    code_size = ext_mpi_generate_byte_code(
        shmem, shmemid, shmem_sizes, buffer_in, sendbufs,
        recvbufs, barriers_size, locmem, reduction_op, (void *)func,
        global_ranks, ip, sizeof(MPI_Request), ranks_node, my_cores_per_node_row,
        my_cores_per_node_column,
        shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, flag, tag);
    if (code_size < 0)
      goto error;
  }
  if (alt) {
    ip = comm_code[handle + 1] = (char *)malloc(code_size);
    if (!ip)
      goto error;
    if (shmem_zero) {
      shmemid = NULL;
    } else {
      if (ext_mpi_setup_shared_memory(comm_row, my_cores_per_node_row, num_sockets_per_node, 
                                      shmem_size, 1, &shmem_sizes, &shmemid, &shmem) < 0)
        goto error_shared;
    }
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
                                        shmem_size - barriers_size * 2, num_sockets_per_node, &shmemid_gpu, &shmem_gpu);
      }
    } else {
#endif
    if (!shmem_zero) {
#ifndef XPMEM
      sendbufs = (char **)malloc(num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
      recvbufs = (char **)malloc(num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
      memset(sendbufs, 0, num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
      memset(recvbufs, 0, num_sockets_per_node * my_cores_per_node_row * sizeof(char *));
      sendbufs[0] = (char *)sendbuf;
      recvbufs[0] = recvbuf;
#else
      ext_mpi_sendrecvbuf_init_xpmem(comm_row, num_sockets_per_node * my_cores_per_node_row, num_sockets_per_node, (char *)sendbuf, counts_send, &sendbufs);
      ext_mpi_sendrecvbuf_init_xpmem(comm_row, num_sockets_per_node * my_cores_per_node_row, num_sockets_per_node, recvbuf, counts_recv, &recvbufs);
#endif
    }
#ifdef GPU_ENABLED
    }
#endif
    if (ext_mpi_fast) {
      if (ext_mpi_generate_source_code(
            shmem, shmemid, shmem_sizes, buffer_in, sendbufs,
            recvbufs, barriers_size, locmem, reduction_op, (void *)func,
            global_ranks, ip, sizeof(MPI_Comm), sizeof(MPI_Request), NULL, my_cores_per_node_row,
            NULL, my_cores_per_node_column,
            shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, tag, handle + 1) < 0)
        goto error;
    } else {
      code_size = ext_mpi_generate_byte_code(
          shmem, shmemid, shmem_sizes, buffer_in, sendbufs,
          recvbufs, barriers_size, locmem, reduction_op, (void *)func,
          global_ranks, ip, sizeof(MPI_Request), ranks_node, my_cores_per_node_row,
          my_cores_per_node_column,
          shmemid_gpu, shmem_gpu, &gpu_byte_code_counter, flag, tag);
      if (code_size < 0)
        goto error;
    }
  } else {
    comm_code[handle + 1] = NULL;
  }
  ext_mpi_delete_parameters(parameters);
  if (shmem_zero) {
    free(recvbufs);
    free(sendbufs);
    free(shmem);
    free(shmem_sizes);
  }
  free(global_ranks);
  if (ext_mpi_fast) {
    if (dlhandle) {
      dlclose(dlhandle);
    }
    sprintf(str, "./compiled_code_%d.so", socket_rank);
    dlhandle = dlopen(str, RTLD_NOW);
    if (!dlhandle) {
      printf("error dlopen %s\n", dlerror());
      exit(1);
    }
    for (i = 0; i < handle_code_max; i++) {
      if (comm_code[i]) {
	sprintf(str, "func_source_%d", i);
	*(void **) (&((struct header_byte_code *)(comm_code[i]))->function) = dlsym(dlhandle, str);
      }
    }
  }
  free(ranks_node);
  return handle;
error:
  ext_mpi_destroy_shared_memory(num_sockets_per_node * my_cores_per_node_row, ranks_node, shmem_sizes, shmemid, shmem);
  free(ranks_node);
  free(global_ranks);
  return ERROR_MALLOC;
error_shared:
  ext_mpi_destroy_shared_memory(num_sockets_per_node * my_cores_per_node_row, ranks_node, shmem_sizes, shmemid, shmem);
  free(ranks_node);
  free(global_ranks);
  return ERROR_SHMEM;
}

int EXT_MPI_Reduce_init_native(const void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *groups, int copyin, int *copyin_factors,
                               int alt, int bit, int waitany, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor, int **mem_partners_send_back, int **mem_partners_recv_back) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node, socket_number,
      *counts = NULL, iret, num_ranks, lrank_row, *ranks, *ranks_inv, *countsa, *displsa, *mem_partners_send = NULL, *mem_partners_recv = NULL,
      nbuffer1 = 0, msize, *msizes = NULL, allreduce_short = (num_ports[0] > 0), reduction_op, factors_max, *factors, socket_size_barrier = 0, socket_size_barrier_small = 0, i, j;
  char *buffer1 = NULL, *buffer2 = NULL, *buffer_temp, *str;
  struct parameters_block *parameters, *parameters2;
  MPI_Comm comm_subrow;
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
  reduction_op = get_reduction_op(datatype, op);
  ext_mpi_call_mpi(MPI_Type_size(datatype, &type_size));
  count *= type_size;
  ext_mpi_call_mpi(MPI_Comm_size(comm_row, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_row, &my_mpi_rank_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(MPI_Comm_size(comm_column, &my_mpi_size_column));
    ext_mpi_call_mpi(MPI_Comm_rank(comm_column, &my_mpi_rank_column));
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  my_node = my_mpi_rank_row / (my_cores_per_node_row * num_sockets_per_node);
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  socket_number = (my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node)) / my_cores_per_node_row;
  counts = (int *)malloc(num_sockets_per_node * sizeof(int));
  if (!counts)
    goto error;
  num_ranks = my_cores_per_node_row * num_sockets_per_node;
  lrank_row = my_lrank_node + socket_number * my_cores_per_node_row;
  ranks = (int*)malloc(num_ranks * sizeof(int));
  ranks_inv = (int*)malloc(num_ranks * sizeof(int));
  countsa = (int*)malloc(num_ranks * sizeof(int));
  displsa = (int*)malloc((num_ranks + 1) * sizeof(int));
  for (i = 0; i < num_ranks; i++) {
    ranks[i] = i;
  }
  if (copyin == 6 || copyin == 7) {
    for (i = 1; copyin_factors[i] < 0; i++);
    ext_mpi_rank_order(num_ranks, i - 1, copyin_factors + 1, ranks);
    for (i = 0; i < num_ranks; i++) {
      if (ranks[i] == lrank_row) {
        lrank_row = i;
        break;
      }
    }
  }
  for (i = 0; i < num_ranks; i++) {
    ranks_inv[ranks[i]] = i;
  }
  if (copyin_factors[0] < 0) {
    ext_mpi_sizes_displs(num_ranks, count, type_size, -copyin_factors[0], countsa, displsa);
  } else {
    ext_mpi_sizes_displs(num_ranks, count, type_size, 0, countsa, displsa);
  }
  for (i = 0; i < num_sockets_per_node; i++) {
    counts[i] = 0;
  }
  for (j = 0; j < num_sockets_per_node; j++) {
    for (i = 0; i < num_ranks; i++) {
      if (i / (num_ranks / num_sockets_per_node) == j) {
	counts[(num_sockets_per_node - j + socket_number) % num_sockets_per_node] += countsa[ranks_inv[i]];
	if (!((num_sockets_per_node - j + socket_number) % num_sockets_per_node) && countsa[ranks_inv[i]]) {
	  socket_size_barrier++;
	}
      }
    }
  }
  if (copyin_factors[0] < 0 && count <= type_size * abs(copyin_factors[0])) {
    socket_size_barrier = 0;
  } else {
    socket_size_barrier = num_ranks / num_sockets_per_node;
  }
  for (i = 0; num_ports[i]; i++) {
    if (abs(num_ports[i]) > socket_size_barrier) socket_size_barrier = abs(num_ports[i]);
    if (abs(num_ports[i]) > socket_size_barrier_small) socket_size_barrier_small = abs(num_ports[i]);
  }
  if (socket_size_barrier > num_ranks) socket_size_barrier = num_ranks;
  if (socket_size_barrier_small > num_ranks) socket_size_barrier_small = num_ranks;
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(recvbuf) && my_cores_per_node_row == 1) {
    socket_size_barrier = socket_size_barrier_small = 1;
  }
#endif
  free(displsa);
  free(countsa);
  free(ranks_inv);
  free(ranks);
  msize = counts[0];
  if (!allreduce_short) {
    msizes =
        (int *)malloc(sizeof(int) * my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node));
    if (!msizes)
      goto error;
    for (i = 0; i < my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node); i++) {
      msizes[i] =
          (msize / type_size) / (my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node));
    }
    for (i = 0;
         i < (msize / type_size) % (my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node));
         i++) {
      msizes[i]++;
    }
    for (i = 0; i < my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node); i++) {
      msizes[i] *= type_size;
    }
  } else {
    msizes = (int *)malloc(sizeof(int) * 1);
    if (!msizes)
      goto error;
    msizes[0] = msize;
  }
  ext_mpi_call_mpi(PMPI_Comm_split(comm_row, socket_number, my_mpi_rank_row, &comm_subrow));
  if (allreduce_short) {
    nbuffer1 += sprintf(buffer1 + nbuffer1,
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE_SHORT\n");
  } else {
    nbuffer1 += sprintf(buffer1 + nbuffer1,
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_NODES %d\n",
                      my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node));
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_NUMBER %d\n", socket_number);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_SIZE_BARRIER %d\n",
                      socket_size_barrier);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER SOCKET_SIZE_BSMALL %d\n",
                      socket_size_barrier_small);
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
  for (i = 0; i < num_sockets_per_node; i++) {
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
    for (i = 0; i < my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node); i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", msizes[i]);
    }
  } else {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", msizes[0]);
    for (i = 1; i < my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node); i++) {
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
  if (!not_recursive) {
    for (factors_max = 0; num_ports[factors_max]; factors_max++);
    for (i = 0; num_ports[i] < 0; i++);
    factors = (int*)malloc(factors_max * sizeof(int));
    for (factors_max = 0; num_ports[factors_max + i]; factors_max++){
      factors[factors_max] = num_ports[factors_max + i] + 1;
    }
    ranks = (int*)malloc(my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node) * sizeof(int));
    for (i = 0; i < my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node); i++) {
      ranks[i] = i;
    }
    if (my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node) > 1) {
      ext_mpi_rank_order(my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node), factors_max, factors, ranks);
      nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER RANK_PERM");
      for (i = 0; i < my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node); i++) {
        nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", ranks[i]);
      }
      nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
      if (ext_mpi_generate_rank_permutation_forward(buffer1, buffer2) < 0)
        goto error;
    } else {
      buffer_temp = buffer1;
      buffer1 = buffer2;
      buffer2 = buffer_temp;
    }
    free(ranks);
    free(factors);
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
  if (padding_factor) {
    ext_mpi_read_parameters(buffer1, &parameters);
    *padding_factor = ext_mpi_greatest_common_divisor(parameters->message_sizes_max, parameters->socket_row_size);
    ext_mpi_delete_parameters(parameters);
  }
  if (!not_recursive && my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node) > 1) {
    if (ext_mpi_generate_rank_permutation_backward(buffer1, buffer2) < 0)
      goto error;
  } else {
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
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
//  if (my_cores_per_node_row * my_cores_per_node_column > 1 || num_sockets_per_node > 1) {
  if (my_cores_per_node_row * num_sockets_per_node > 1 && my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node) > 1) {
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
  if (root != -10) {
    if (ext_mpi_generate_allreduce_copyin(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_reduce_copyin(buffer1, buffer2) < 0)
      goto error;
  }
  if (my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node) > 1) {
    if (root > -10 && copyin < 8 && !not_recursive && my_cores_per_node_row * my_cores_per_node_column == 1 && num_sockets_per_node == 1) {
      if (ext_mpi_generate_raw_code(buffer2, buffer1) < 0)
	goto error;
    } else {
      if (copyin < 8) {
	if (ext_mpi_generate_allreduce_copyin_shmem(buffer2, buffer1) < 0)
	  goto error;
      } else {
	buffer_temp = buffer1;
	buffer1 = buffer2;
	buffer2 = buffer_temp;
      }
      if (ext_mpi_generate_raw_code(buffer1, buffer2) < 0)
	goto error;
      if (copyin < 8) {
        if (ext_mpi_generate_allreduce_copyout_shmem(buffer2, buffer1) < 0)
          goto error;
      } else {
	buffer_temp = buffer1;
	buffer1 = buffer2;
	buffer2 = buffer_temp;
      }
    }
  } else {
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (root != 0) {
    if (ext_mpi_generate_allreduce_copyout(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (ext_mpi_generate_reduce_copyout(buffer1, buffer2) < 0)
      goto error;
  }
 
/*   int mpi_rank;
   MPI_Comm_rank(MPI_COMM_WORLD,&mpi_rank);
   if (mpi_rank == 0){
   printf("%s",buffer2);
   }
   exit(9);*/
   
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1, &comm_subrow) < 0)
    goto error;
  if (ext_mpi_generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (root > -10) {
    if (ext_mpi_generate_optimise_buffers(buffer2, buffer1) < 0)
      goto error;
    if (ext_mpi_generate_optimise_buffers2(buffer1, buffer2) < 0)
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
  if (ext_mpi_generate_no_waitall_zero(buffer2, buffer1) < 0)
    goto error;
  buffer_temp = buffer2;
  buffer2 = buffer1;
  buffer1 = buffer_temp;
  if (ext_mpi_clean_barriers(buffer2, buffer1, comm_subrow, comm_column) < 0)
    goto error;
  if (alt) {
    if (ext_mpi_generate_no_first_barrier(buffer1, buffer2) < 0)
      goto error;
  } else {
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (ext_mpi_generate_no_socket_barriers(buffer2, buffer1) < 0)
    goto error;
  buffer_temp = buffer2;
  buffer2 = buffer1;
  buffer1 = buffer_temp;
  if (root > -10 && copyin < 8 && !not_recursive && ((my_cores_per_node_row * my_cores_per_node_column == 1 && num_sockets_per_node == 1) || (copyin == 7 && my_mpi_size_row / (my_cores_per_node_row * num_sockets_per_node) > 1))) {
    if (ext_mpi_generate_use_sendbuf_recvbuf(buffer2, buffer1) < 0)
      goto error;
    buffer_temp = buffer2;
    buffer2 = buffer1;
    buffer1 = buffer_temp;
  }
  if (ext_mpi_generate_count_mem_partners(buffer2, buffer1) < 0)
      goto error;
  ext_mpi_read_parameters(buffer1, &parameters2);
  buffer_temp = buffer1;
  buffer1 = buffer2;
  buffer2 = buffer_temp;
  mem_partners_send = (int*)malloc(sizeof(int) * (parameters2->mem_partners_send_max + 1));
  for (i = 0; i < parameters2->mem_partners_send_max; i++) {
    mem_partners_send[i] = parameters2->mem_partners_send[i];
  }
  mem_partners_send[i] = -1;
  mem_partners_recv = (int*)malloc(sizeof(int) * (parameters2->mem_partners_recv_max + 1));
  for (i = 0; i < parameters2->mem_partners_recv_max; i++) {
    mem_partners_recv[i] = parameters2->mem_partners_recv[i];
  }
  mem_partners_recv[i] = -1;
  ext_mpi_delete_parameters(parameters2);
//int rank;
//ext_mpi_call_mpi(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
//if (rank == 0) printf("%s\n", buffer2);
//exit(1);
  iret = init_epilogue(buffer2, sendbuf, recvbuf, reduction_op, ext_mpi_hash_search_operator(&op), comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem, mem_partners_send, mem_partners_recv);
  free(buffer2);
  free(buffer1);
  ext_mpi_call_mpi(PMPI_Comm_free(&comm_subrow));
  if (mem_partners_send_back) {
    *mem_partners_send_back = mem_partners_send;
  } else {
    free(mem_partners_send);
  }
  if (mem_partners_recv_back) {
    *mem_partners_recv_back = mem_partners_recv;
  } else {
    free(mem_partners_recv);
  }
  return iret;
error:
  free(msizes);
  free(counts);
  free(buffer2);
  free(buffer1);
  free(mem_partners_recv);
  free(mem_partners_send);
  ext_mpi_call_mpi(PMPI_Comm_free(&comm_subrow));
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
                                  int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor, int **mem_partners_send, int **mem_partners_recv) {
  return (EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, -1, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, copyin, copyin_factors, alt, bit, waitany, not_recursive, blocking, num_sockets_per_node, shmem_zero, locmem, padding_factor, mem_partners_send, mem_partners_recv));
}

int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int copyin, int *copyin_factors,
                              int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int **mem_partners_send, int **mem_partners_recv) {
  return (EXT_MPI_Reduce_init_native(
      buffer, buffer, count, datatype, MPI_OP_NULL, -10 - root, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, copyin, copyin_factors, alt, 0, 0, not_recursive, blocking, num_sockets_per_node, shmem_zero, locmem, NULL, mem_partners_send, mem_partners_recv));
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
  ext_mpi_call_mpi(MPI_Comm_size(comm_row, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_row, &my_mpi_rank_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(MPI_Comm_size(comm_column, &my_mpi_size_column));
    ext_mpi_call_mpi(MPI_Comm_rank(comm_column, &my_mpi_rank_column));
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  ext_mpi_call_mpi(MPI_Type_size(sendtype, &type_size));
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
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                   my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                   comm_column));
    ext_mpi_call_mpi(PMPI_Allgather(&recvcounts[my_node * my_cores_per_node_row],
                   my_cores_per_node_row, MPI_INT, local_counts,
                   my_cores_per_node_row, MPI_INT, comm_column));
    ext_mpi_call_mpi(PMPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column));
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_NODES %d\n",
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
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1, &comm_row) < 0)
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
  iret = init_epilogue(buffer2, sendbuf, recvbuf, OPCODE_REDUCE_SUM_CHAR, NULL, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem, NULL, NULL);
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
  ext_mpi_call_mpi(MPI_Type_size(sendtype, &type_size));
  ext_mpi_call_mpi(MPI_Comm_size(comm_row, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_row, &my_mpi_rank_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(MPI_Comm_size(comm_column, &my_mpi_size_column));
    ext_mpi_call_mpi(MPI_Comm_rank(comm_column, &my_mpi_rank_column));
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
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                   my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                   comm_column));
    ext_mpi_call_mpi(PMPI_Allgather(&sendcounts[my_node * my_cores_per_node_row],
                   my_cores_per_node_row, MPI_INT, local_counts,
                   my_cores_per_node_row, MPI_INT, comm_column));
    ext_mpi_call_mpi(PMPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column));
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_NODES %d\n",
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
  if (ext_mpi_generate_buffer_offset(buffer1, buffer2, &comm_row) < 0)
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
  iret = init_epilogue(buffer1, sendbuf, recvbuf, -1, NULL, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem, NULL, NULL);
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
    int copyin, int *copyin_factors, int alt, int not_recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor, int **mem_partners_send_back) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node, *mem_partners_send = NULL,
      *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *str, *buffer_temp;
  int nbuffer1 = 0, i, j;
  int reduction_op;
  struct parameters_block *parameters, *parameters2;
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
  ext_mpi_call_mpi(MPI_Type_size(datatype, &type_size));
  ext_mpi_call_mpi(MPI_Comm_size(comm_row, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_row, &my_mpi_rank_row));
  if (comm_column != MPI_COMM_NULL) {
    ext_mpi_call_mpi(MPI_Comm_size(comm_column, &my_mpi_size_column));
    ext_mpi_call_mpi(MPI_Comm_rank(comm_column, &my_mpi_rank_column));
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
    ext_mpi_call_mpi(PMPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                   my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                   comm_column));
    ext_mpi_call_mpi(PMPI_Allgather(&recvcounts[my_node * my_cores_per_node_row],
                   my_cores_per_node_row, MPI_INT, local_counts,
                   my_cores_per_node_row, MPI_INT, comm_column));
    ext_mpi_call_mpi(PMPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column));
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_NODES %d\n",
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
  if (ext_mpi_generate_buffer_offset(buffer2, buffer1, &comm_row) < 0)
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
  if (ext_mpi_generate_count_mem_partners(buffer2, buffer1) < 0)
      goto error;
//int rank;
//ext_mpi_call_mpi(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
//if (rank == 0) printf("%s\n", buffer1);
  ext_mpi_read_parameters(buffer1, &parameters2);
  buffer_temp = buffer1;
  buffer1 = buffer2;
  buffer2 = buffer_temp;
  mem_partners_send = (int*)malloc(sizeof(int) * (parameters2->mem_partners_send_max + 1));
  for (i = 0; i < parameters2->mem_partners_send_max; i++) {
    mem_partners_send[i] = parameters2->mem_partners_send[i];
  }
  mem_partners_send[i] = -1;
  ext_mpi_delete_parameters(parameters2);
  iret = init_epilogue(buffer2, sendbuf, recvbuf, reduction_op, NULL, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt, shmem_zero, locmem, mem_partners_send, NULL);
  free(buffer2);
  free(buffer1);
  if (mem_partners_send_back) {
    *mem_partners_send_back = mem_partners_send;
  } else {
    free(mem_partners_send);
  }
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
  ext_mpi_call_mpi(PMPI_Comm_dup(MPI_COMM_WORLD, &ext_mpi_COMM_WORLD_dup));
  shmem_root_cpu = ext_mpi_get_shmem_root_cpu();
  ext_mpi_dmalloc_init(shmem_root_cpu, ext_mpi_init_shared_memory(ext_mpi_COMM_WORLD_dup, 1024u * 1024u * 2048u), 1024u * 1024u * 2048u);
#ifdef GPU_ENABLED
  shmem_root_gpu = ext_mpi_get_shmem_root_gpu();
  ext_mpi_dmalloc_init(shmem_root_gpu, ext_mpi_init_shared_memory_gpu(ext_mpi_COMM_WORLD_dup, 1024u * 1024u * 2048u), 1024u * 1024u * 2048u);
#endif
#ifdef XPMEM
  ext_mpi_init_xpmem(ext_mpi_COMM_WORLD_dup);
#endif
#ifdef GPU_ENABLED
  ext_mpi_init_gpu_blocking(ext_mpi_COMM_WORLD_dup);
#endif
  is_initialised = 1;
  return 0;
}

int EXT_MPI_Initialized_native() { return is_initialised; }

int EXT_MPI_Finalize_native() {
#ifdef GPU_ENABLED
  ext_mpi_done_gpu_blocking();
#endif
#ifdef XPMEM
  ext_mpi_done_xpmem();
#endif
  ext_mpi_done_shared_memory(ext_mpi_COMM_WORLD_dup);
  ext_mpi_call_mpi(PMPI_Comm_free(&ext_mpi_COMM_WORLD_dup));
  return 0;
}

void ext_mpi_native_export(int **e_handle_code_max, char ****e_comm_code, char ****e_execution_pointer, int ***e_active_wait, int **e_is_initialised, MPI_Comm **e_ext_mpi_COMM_WORLD_dup, int **e_tag_max) {
  *e_handle_code_max = &handle_code_max;
  *e_comm_code = &comm_code;
  *e_execution_pointer = &execution_pointer;
  *e_active_wait = &active_wait;
  *e_is_initialised = &is_initialised;
  *e_ext_mpi_COMM_WORLD_dup = &ext_mpi_COMM_WORLD_dup;
  *e_tag_max = &tag_max;
}
