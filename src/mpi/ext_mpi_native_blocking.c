#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "prime_factors.h"
#include "alltoall.h"
#include "buffer_offset.h"
#include "byte_code.h"
#include "clean_barriers.h"
#include "constants.h"
#include "ext_mpi_native.h"
#include "waitany.h"
#include "messages_shared_memory.h"
#include "optimise_multi_socket.h"
#include "padding_factor.h"
#include "shmem.h"
#include "ext_mpi_native_exec.h"
#include "ext_mpi_native_blocking.h"
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

static int *e_handle_code_max = NULL;
static char ***e_comm_code = NULL;
static char ***e_execution_pointer = NULL;
static int **e_active_wait = NULL;

static int *e_is_initialised = NULL;
static MPI_Comm *e_EXT_MPI_COMM_WORLD = NULL;
static int *e_tag_max = NULL;

static void (*e_socket_barrier)(char *shmem, int *barrier_count, int socket_rank, int num_cores);
static void (*e_node_barrier)(char **shmem, int *barrier_count, int socket_rank, int num_sockets_per_node);
static void (*e_socket_barrier_atomic_set)(char *shmem, int barrier_count, int entry);
static void (*e_socket_barrier_atomic_wait)(char *shmem, int *barrier_count, int entry);

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

static struct comm_comm_blocking **comms_blocking = NULL;
#ifdef GPU_ENABLED
static struct comm_comm_blocking **comms_blocking_gpu = NULL;
#endif

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

static int add_blocking_member(int count, MPI_Datatype datatype, int handle, char **comm_code_blocking, int *count_blocking, int pfactor, MPI_Comm comm, int tag, char ***send_pointers_allreduce_blocking) {
  int type_size, i = 0, comm_size;
  MPI_Type_size(datatype, &type_size);
  while (comm_code_blocking[i]) i++;
  comm_code_blocking[i] = (*e_comm_code)[handle];
  (*e_comm_code)[handle] = 0;
  (*e_execution_pointer)[handle] = NULL;
  (*e_active_wait)[handle] = 0;
  if (send_pointers_allreduce_blocking) {
    MPI_Comm_size(comm, &comm_size);
    *send_pointers_allreduce_blocking = (char **)malloc(comm_size * comm_size * sizeof(char *));
    memset(*send_pointers_allreduce_blocking, 0, comm_size * comm_size * sizeof(char *));
    ext_mpi_normalize_blocking(comm_code_blocking[i], comm, tag, type_size * pfactor, *send_pointers_allreduce_blocking);
  } else {
    ext_mpi_normalize_blocking(comm_code_blocking[i], comm, tag, type_size * pfactor, NULL);
  }
  count_blocking[i] = count * type_size;
  return 0;
}

static int add_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int bit, int recursive, int arecursive, int blocking, int num_sockets_per_node, enum ecollective_type collective_type, int i_comm, struct comm_comm_blocking ***comms_blocking, long int send_ptr, long int recv_ptr) {
  int handle, size_shared = 1024*1024, *loc_shmemid, *recvcounts, *displs, padding_factor, type_size, i, j, *numbers, j_;
  char **shmem_local, *comm_code_temp;
  struct header_byte_code *header;
  MPI_Type_size(datatype, &type_size);
  if (!(*comms_blocking)) {
    *comms_blocking = (struct comm_comm_blocking **)malloc(1000 * sizeof(struct comm_comm_blocking *));
    memset(*comms_blocking, 0, 1000 * sizeof(struct comm_comm_blocking *));
  }
  if (!(*comms_blocking)[i_comm]) {
    (*comms_blocking)[i_comm] = (struct comm_comm_blocking *)malloc(sizeof(struct comm_comm_blocking));
    memset((*comms_blocking)[i_comm], 0, sizeof(struct comm_comm_blocking));
  }
  if (!(*comms_blocking)[i_comm]->comm_code_allreduce_blocking && !(*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking && !(*comms_blocking)[i_comm]->comm_code_allgather_blocking) {
    (*comms_blocking)[i_comm]->p_dev_temp = NULL;
#ifdef GPU_ENABLED
    ext_mpi_gpu_malloc(&(*comms_blocking)[i_comm]->p_dev_temp, 10000);
#endif
    PMPI_Comm_dup(comm, &(*comms_blocking)[i_comm]->comm_blocking);
    MPI_Comm_size((*comms_blocking)[i_comm]->comm_blocking, &(*comms_blocking)[i_comm]->mpi_size_blocking);
    MPI_Comm_rank((*comms_blocking)[i_comm]->comm_blocking, &(*comms_blocking)[i_comm]->mpi_rank_blocking);
    (*comms_blocking)[i_comm]->comm_code_allreduce_blocking = (char **)malloc(101 * sizeof(char *));
    memset((*comms_blocking)[i_comm]->comm_code_allreduce_blocking, 0, 101 * sizeof(char *));
    (*comms_blocking)[i_comm]->count_allreduce_blocking = (int *)malloc(100 * sizeof(int));
    (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking = (char **)malloc(101 * sizeof(char *));
    memset((*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking, 0, 101 * sizeof(char *));
    (*comms_blocking)[i_comm]->count_reduce_scatter_block_blocking = (int *)malloc(100 * sizeof(int));
    (*comms_blocking)[i_comm]->comm_code_allgather_blocking = (char **)malloc(101 * sizeof(char *));
    memset((*comms_blocking)[i_comm]->comm_code_allgather_blocking, 0, 101 * sizeof(char *));
    (*comms_blocking)[i_comm]->count_allgather_blocking = (int *)malloc(100 * sizeof(int));
    (*comms_blocking)[i_comm]->padding_factor_allreduce_blocking = (int *)malloc(100 * sizeof(int));
    memset((*comms_blocking)[i_comm]->padding_factor_allreduce_blocking, 0, 100 * sizeof(int));
    (*comms_blocking)[i_comm]->padding_factor_reduce_scatter_block_blocking = (int *)malloc(100 * sizeof(int));
    memset((*comms_blocking)[i_comm]->padding_factor_reduce_scatter_block_blocking, 0, 100 * sizeof(int));
    (*comms_blocking)[i_comm]->send_pointers_allreduce_blocking = (char ***)malloc(101 * sizeof(char **));
    memset((*comms_blocking)[i_comm]->send_pointers_allreduce_blocking, 0, 101 * sizeof(char **));
    comm_code_temp = (char *)malloc(sizeof(struct header_byte_code) + 2 * sizeof(MPI_Comm));
    header = (struct header_byte_code *)comm_code_temp;
    header->size_to_return = sizeof(struct header_byte_code);
    *((MPI_Comm *)(comm_code_temp+header->size_to_return)) = comm;
    *((MPI_Comm *)(comm_code_temp+header->size_to_return+sizeof(MPI_Comm))) = MPI_COMM_NULL;
    ext_mpi_setup_shared_memory(&(*comms_blocking)[i_comm]->comm_row_blocking, &(*comms_blocking)[i_comm]->comm_column_blocking, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, &size_shared, 1, &loc_shmemid, &shmem_local, 0, size_shared, &comm_code_temp);
    (*comms_blocking)[i_comm]->shmem_socket_blocking_shmemid = loc_shmemid[0];
    free(loc_shmemid);
    (*comms_blocking)[i_comm]->shmem_socket_blocking = shmem_local[0];
    free(shmem_local);
    (*comms_blocking)[i_comm]->counter_socket_blocking = 1;
    (*comms_blocking)[i_comm]->num_cores_blocking = my_cores_per_node;
    (*comms_blocking)[i_comm]->socket_rank_blocking = (*comms_blocking)[i_comm]->mpi_rank_blocking % (*comms_blocking)[i_comm]->num_cores_blocking;
    (*comms_blocking)[i_comm]->shmem_node_blocking_num_segments = num_sockets_per_node;
    ext_mpi_setup_shared_memory(&(*comms_blocking)[i_comm]->comm_row_blocking, &(*comms_blocking)[i_comm]->comm_column_blocking, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, &size_shared, (*comms_blocking)[i_comm]->shmem_node_blocking_num_segments, &(*comms_blocking)[i_comm]->shmem_node_blocking_shmemid, &(*comms_blocking)[i_comm]->shmem_node_blocking, 0, size_shared, &comm_code_temp);
    (*comms_blocking)[i_comm]->counter_node_blocking = 1;
    (*comms_blocking)[i_comm]->num_sockets_per_node_blocking = 1;
    size_shared = 1024 * 1024 * 1024 / 8;
#ifndef GPU_ENABLED
    ext_mpi_setup_shared_memory(&(*comms_blocking)[i_comm]->comm_row_blocking, &(*comms_blocking)[i_comm]->comm_column_blocking, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, &size_shared, 1, &(*comms_blocking)[i_comm]->shmem_blocking_shmemid1, &(*comms_blocking)[i_comm]->shmem_blocking1, 0, size_shared, &comm_code_temp);
    ext_mpi_setup_shared_memory(&(*comms_blocking)[i_comm]->comm_row_blocking, &(*comms_blocking)[i_comm]->comm_column_blocking, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, &size_shared, 1, &(*comms_blocking)[i_comm]->shmem_blocking_shmemid2, &(*comms_blocking)[i_comm]->shmem_blocking2, 0, size_shared, &comm_code_temp);
#else
    ext_mpi_gpu_setup_shared_memory((*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, size_shared, 1, &(*comms_blocking)[i_comm]->shmem_blocking_shmemid1, &(*comms_blocking)[i_comm]->shmem_blocking1);
    ext_mpi_gpu_setup_shared_memory((*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, size_shared, 1, &(*comms_blocking)[i_comm]->shmem_blocking_shmemid2, &(*comms_blocking)[i_comm]->shmem_blocking2);
#endif
    free(comm_code_temp);
    (*comms_blocking)[i_comm]->locmem_blocking = (char *)malloc(1024 * sizeof(MPI_Request));
  }
  switch (collective_type) {
    case collective_type_allreduce:
      j = (*comms_blocking)[i_comm]->mpi_size_blocking / (*comms_blocking)[i_comm]->num_cores_blocking * count;
      if (count > (*comms_blocking)[i_comm]->mpi_size_blocking / (*comms_blocking)[i_comm]->num_cores_blocking && count % (*comms_blocking)[i_comm]->mpi_size_blocking / (*comms_blocking)[i_comm]->num_cores_blocking == 0) {
	j = count;
      }
      if (count < (*comms_blocking)[i_comm]->mpi_size_blocking / (*comms_blocking)[i_comm]->num_cores_blocking && (*comms_blocking)[i_comm]->mpi_size_blocking / (*comms_blocking)[i_comm]->num_cores_blocking % count == 0) {
	j = (*comms_blocking)[i_comm]->mpi_size_blocking / (*comms_blocking)[i_comm]->num_cores_blocking;
      }
      handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), j, datatype, op, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 0, bit, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
      numbers = (int *)malloc(1024 * 1024 * sizeof(int));
      j_ = ext_mpi_exec_padding((*e_comm_code)[handle], (char *)(send_ptr), (char *)(recv_ptr), NULL, numbers);
      j_ = ext_mpi_prime_factor_padding(j_, numbers);
      padding_factor = ext_mpi_padding_factor(j_, comm);
      free(numbers);
      for (i = 0; (*comms_blocking)[i_comm]->comm_code_allreduce_blocking[i]; i++)
        ;
      (*comms_blocking)[i_comm]->padding_factor_allreduce_blocking[i] = j / padding_factor;
      add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allreduce_blocking, (*comms_blocking)[i_comm]->count_allreduce_blocking, padding_factor, comm, 1, &((*comms_blocking)[i_comm]->send_pointers_allreduce_blocking[i]));
    break;
    case collective_type_reduce_scatter_block:
      recvcounts = (int *)malloc((*comms_blocking)[i_comm]->mpi_size_blocking * sizeof(int));
      for (i = 0; i < (*comms_blocking)[i_comm]->mpi_size_blocking; i++) {
// FIXME
        recvcounts[i] = (*comms_blocking)[i_comm]->num_cores_blocking * CACHE_LINE_SIZE;
        recvcounts[i] = (*comms_blocking)[i_comm]->num_cores_blocking;
      }
      handle = EXT_MPI_Reduce_scatter_init_native((char *)(send_ptr), (char *)(recv_ptr), recvcounts, datatype, op, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, copyin, copyin_factors, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
      for (i = 0; (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking[i]; i++)
	;
      (*comms_blocking)[i_comm]->padding_factor_reduce_scatter_block_blocking[i] = padding_factor;
// FIXME
//      add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->count_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->num_cores_blocking * CACHE_LINE_SIZE / padding_factor, MPI_COMM_NULL, 0, NULL);
      add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->count_reduce_scatter_block_blocking, (*comms_blocking)[i_comm]->num_cores_blocking, MPI_COMM_NULL, 0, NULL);
      free(recvcounts);
    break;
    case collective_type_allgather:
      recvcounts = (int *)malloc((*comms_blocking)[i_comm]->mpi_size_blocking * sizeof(int));
      displs = (int *)malloc((*comms_blocking)[i_comm]->mpi_size_blocking * sizeof(int));
      for (i = 0; i < (*comms_blocking)[i_comm]->mpi_size_blocking; i++) {
        recvcounts[i] = 1;
        displs[i] = i;
      }
      handle = EXT_MPI_Allgatherv_init_native((char *)(send_ptr), 1, datatype, (char *)(recv_ptr), recvcounts, displs, datatype, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking);
// FIXME
//      add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allgather_blocking, (*comms_blocking)[i_comm]->count_allgather_blocking, 1, MPI_COMM_NULL, 0, NULL);
      add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allgather_blocking, (*comms_blocking)[i_comm]->count_allgather_blocking, (*comms_blocking)[i_comm]->num_cores_blocking, MPI_COMM_NULL, 0, NULL);
      free(displs);
      free(recvcounts);
    break;
    default:
    printf("collective_type not implemented\n");
    exit(1);
  }
  return 0;
}

int EXT_MPI_Add_blocking_native(int count, MPI_Datatype datatype, MPI_Op op, MPI_Comm comm, int my_cores_per_node, int *num_ports, int *groups, int copyin, int *copyin_factors, int bit, int recursive, int arecursive, int blocking, int num_sockets_per_node, enum ecollective_type collective_type, int i_comm) {
  ext_mpi_native_export(e_handle_code_max, e_comm_code, e_execution_pointer, e_active_wait, e_is_initialised, e_EXT_MPI_COMM_WORLD, e_tag_max, e_socket_barrier, e_node_barrier, e_socket_barrier_atomic_set, e_socket_barrier_atomic_wait);
  add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking, SEND_PTR_CPU, RECV_PTR_CPU);
#ifdef GPU_ENABLED
  add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking_gpu, SEND_PTR_GPU, RECV_PTR_GPU);
#endif
  return 0;
}

static int release_blocking_native(int i_comm, struct comm_comm_blocking ***comms_blocking) {
  struct header_byte_code *header;
  int *loc_shmemid, i;
  char **loc_shmem;
  header = (struct header_byte_code *)malloc(sizeof(struct header_byte_code) + 2 * sizeof(MPI_Comm));
  header->size_to_return = sizeof(struct header_byte_code);
  *((MPI_Comm *)(((char *)header)+header->size_to_return)) = (*comms_blocking)[i_comm]->comm_row_blocking;
  *((MPI_Comm *)(((char *)header)+header->size_to_return+sizeof(MPI_Comm))) = (*comms_blocking)[i_comm]->comm_column_blocking;
  free((*comms_blocking)[i_comm]->padding_factor_allreduce_blocking);
  free((*comms_blocking)[i_comm]->padding_factor_reduce_scatter_block_blocking);
  free((*comms_blocking)[i_comm]->count_allreduce_blocking);
  free((*comms_blocking)[i_comm]->count_reduce_scatter_block_blocking);
  free((*comms_blocking)[i_comm]->count_allgather_blocking);
#ifndef GPU_ENABLED
  ext_mpi_destroy_shared_memory(0, 1, (*comms_blocking)[i_comm]->shmem_blocking_shmemid1, (*comms_blocking)[i_comm]->shmem_blocking1, (char *)header);
  ext_mpi_destroy_shared_memory(0, 1, (*comms_blocking)[i_comm]->shmem_blocking_shmemid2, (*comms_blocking)[i_comm]->shmem_blocking2, (char *)header);
#else
  if ((*comms_blocking)[i_comm]->shmem_blocking_shmemid1) {
    ext_mpi_gpu_destroy_shared_memory(1, (*comms_blocking)[i_comm]->shmem_blocking_shmemid1, (*comms_blocking)[i_comm]->shmem_blocking1, (char *)header);
  }
  if ((*comms_blocking)[i_comm]->shmem_blocking_shmemid2) {
    ext_mpi_gpu_destroy_shared_memory(1, (*comms_blocking)[i_comm]->shmem_blocking_shmemid2, (*comms_blocking)[i_comm]->shmem_blocking2, (char *)header);
  }
#endif
  free((*comms_blocking)[i_comm]->locmem_blocking);
  loc_shmemid = (int *)malloc(1 * sizeof(int));
  *loc_shmemid = (*comms_blocking)[i_comm]->shmem_socket_blocking_shmemid;
  loc_shmem = (char **)malloc(1 * sizeof(char *));
  *loc_shmem = (*comms_blocking)[i_comm]->shmem_socket_blocking;
  ext_mpi_destroy_shared_memory(0, 1, loc_shmemid, loc_shmem, (char *)header);
  ext_mpi_destroy_shared_memory(0, (*comms_blocking)[i_comm]->shmem_node_blocking_num_segments, (*comms_blocking)[i_comm]->shmem_node_blocking_shmemid, (*comms_blocking)[i_comm]->shmem_node_blocking, (char *)header);
  free(header);
  for (i = 0; i < 101; i++) {
    free((*comms_blocking)[i_comm]->send_pointers_allreduce_blocking[i]);
    free((*comms_blocking)[i_comm]->comm_code_allreduce_blocking[i]);
    free((*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking[i]);
    free((*comms_blocking)[i_comm]->comm_code_allgather_blocking[i]);
  }
  free((*comms_blocking)[i_comm]->send_pointers_allreduce_blocking);
  free((*comms_blocking)[i_comm]->comm_code_allreduce_blocking);
  free((*comms_blocking)[i_comm]->comm_code_reduce_scatter_block_blocking);
  free((*comms_blocking)[i_comm]->comm_code_allgather_blocking);
  PMPI_Comm_free(&(*comms_blocking)[i_comm]->comm_blocking);
#ifdef GPU_ENABLED
  ext_mpi_gpu_free((*comms_blocking)[i_comm]->p_dev_temp);
#endif
  free((*comms_blocking)[i_comm]);
  (*comms_blocking)[i_comm] = NULL;
  if (i_comm == 0) {
    free(*comms_blocking);
    *comms_blocking = NULL;
  }
  return 0;
}

int EXT_MPI_Release_blocking_native(int i_comm) {
  release_blocking_native(i_comm, &comms_blocking);
#ifdef GPU_ENABLED
  release_blocking_native(i_comm, &comms_blocking_gpu);
#endif
  return 0;
}

int EXT_MPI_Allreduce_native(const void *sendbuf, void *recvbuf, int count, int reduction_op, int i_comm) {
  struct comm_comm_blocking *comms_blocking_;
  int type_size, ccount, *shmemid_temp, i = 0;
  char **shmem_temp;
  switch (reduction_op) {
    case OPCODE_REDUCE_SUM_DOUBLE: type_size = sizeof(double); break;
    case OPCODE_REDUCE_SUM_LONG_INT: type_size = sizeof(long int); break;
    case OPCODE_REDUCE_SUM_FLOAT: type_size = sizeof(float); break;
    case OPCODE_REDUCE_SUM_INT: type_size = sizeof(int); break;
    default: type_size = 1;
  }
  ccount = count * type_size;
  comms_blocking_ = comms_blocking[i_comm];
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    comms_blocking_ = comms_blocking_gpu[i_comm];
  }
#endif
  while (comms_blocking_->count_allreduce_blocking[i] < ccount && comms_blocking_->comm_code_allreduce_blocking[i + 1]) i++;
  ext_mpi_exec_blocking(comms_blocking_->comm_code_allreduce_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->send_pointers_allreduce_blocking[i], comms_blocking_->p_dev_temp);
  shmem_temp = comms_blocking_->shmem_blocking1;
  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
  comms_blocking_->shmem_blocking2 = shmem_temp;
  shmemid_temp = comms_blocking_->shmem_blocking_shmemid1;
  comms_blocking_->shmem_blocking_shmemid1 = comms_blocking_->shmem_blocking_shmemid2;
  comms_blocking_->shmem_blocking_shmemid2 = shmemid_temp;
  return 0;
}

int EXT_MPI_Reduce_scatter_block_native(const void *sendbuf, void *recvbuf, int recvcount, int reduction_op, int i_comm) {
  struct comm_comm_blocking *comms_blocking_;
  int *shmemid_temp, i = 0;
  char **shmem_temp;
  comms_blocking_ = comms_blocking[i_comm];
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    comms_blocking_ = comms_blocking_gpu[i_comm];
  }
#endif
  while (comms_blocking_->count_reduce_scatter_block_blocking[i] < recvcount && comms_blocking_->comm_code_reduce_scatter_block_blocking[i + 1]) i++;
// FIXME
//  ext_mpi_exec_blocking(comms_blocking_->comm_code_reduce_scatter_block_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, recvcount / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i], reduction_op, recvcount, NULL);
  ext_mpi_exec_blocking(comms_blocking_->comm_code_reduce_scatter_block_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, recvcount / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i], reduction_op, recvcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL, comms_blocking_->p_dev_temp);
  shmem_temp = comms_blocking_->shmem_blocking1;
  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
  comms_blocking_->shmem_blocking2 = shmem_temp;
  shmemid_temp = comms_blocking_->shmem_blocking_shmemid1;
  comms_blocking_->shmem_blocking_shmemid1 = comms_blocking_->shmem_blocking_shmemid2;
  comms_blocking_->shmem_blocking_shmemid2 = shmemid_temp;
  return 0;
}

int EXT_MPI_Allgather_native(const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, MPI_Comm comm, int i_comm) {
  struct comm_comm_blocking *comms_blocking_;
  int *shmemid_temp, i = 0;
  char **shmem_temp;
  comms_blocking_ = comms_blocking[i_comm];
#ifdef GPU_ENABLED
  if (ext_mpi_gpu_is_device_pointer(recvbuf)) {
    comms_blocking_ = comms_blocking_gpu[i_comm];
  }
#endif
  while (comms_blocking_->count_allgather_blocking[i] < sendcount && comms_blocking_->comm_code_allgather_blocking[i + 1]) i++;
// FIXME
//  ext_mpi_exec_blocking(comms_blocking_->comm_code_allgather_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, sendcount, -1, sendcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL);
  ext_mpi_exec_blocking(comms_blocking_->comm_code_allgather_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, sendcount, -1, sendcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL, comms_blocking_->p_dev_temp);
  shmem_temp = comms_blocking_->shmem_blocking1;
  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
  comms_blocking_->shmem_blocking2 = shmem_temp;
  shmemid_temp = comms_blocking_->shmem_blocking_shmemid1;
  comms_blocking_->shmem_blocking_shmemid1 = comms_blocking_->shmem_blocking_shmemid2;
  comms_blocking_->shmem_blocking_shmemid2 = shmemid_temp;
  return 0;
}
