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

static void normalize_address(int count, void **address) {
  long int i;
  i = (long int)(*address) >> 28;
  *address = (void *)(((long int)(*address) & 0xFFFFFFF) / count + (i << 28));
}

static void recalculate_address_io(const void *sendbuf, void *recvbuf, int count, void **shmem, int size_io, void **address, int *size) {
  long int i;
  i = (long int)(*address) >> 28;
  switch (i) {
#ifdef GPU_ENABLED
    case (SEND_PTR_GPU >> 28):
#endif
    case (SEND_PTR_CPU >> 28):
      *address = (void *)(((long int)(*address) & 0xFFFFFFF) * count + (long int)(sendbuf));
      if ((char *)(*address) + *size > (char *)sendbuf + size_io) {
        *size = (char *)sendbuf + size_io - (char *)(*address);
        if (*size < 0) *size = 0;
      }
      break;
#ifdef GPU_ENABLED
    case (RECV_PTR_GPU >> 28):
#endif
    case (RECV_PTR_CPU >> 28):
      *address = (void *)(((long int)(*address) & 0xFFFFFFF) * count + (long int)(recvbuf));
      if ((char *)(*address) + *size > (char *)recvbuf + size_io) {
        *size = (char *)recvbuf + size_io - (char *)(*address);
        if (*size < 0) *size = 0;
      }
      break;
    default:
      *address = (void *)(((long int)(*address) & 0xFFFFFFF) * count + (long int)(shmem[i]));
  }
}

#ifdef GPU_ENABLED
static void gpu_normalize_addresses(int count, void *p) {
  char *ldata, *p1, *p2;
  long int size;
  int num_streams, num_stream, index;
  num_streams = *((int *)(p + sizeof(int)));
  for (num_stream = 0; num_stream < num_streams; num_stream++) {
    index = 0;
    ldata = p + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
    p1 = *((char **)ldata);
    while (p1) {
      p2 = *((char **)(ldata + sizeof(char *)));
      size = *((long int *)(ldata + 2 * sizeof(char *)));
      normalize_address(count, (void **)&p1);
      normalize_address(count, (void **)&p2);
      size /= count;
      *((char **)ldata) = p1;
      *((char **)(ldata + sizeof(char *))) = p2;
      *((long int *)(ldata + 2 * sizeof(char *))) = size;
      index++;
      ldata = p + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
      p1 = *((char **)ldata);
    }
  }
}

static void gpu_recalculate_addresses(void *p_ref, const void *sendbuf, void *recvbuf, int count, void **shmem_blocking, int count_io, int instruction, void *p_dev) {
  char *ldata, *lldata, p_temp[10000], *p1, *p2;
  long int size;
  int num_streams, num_stream, index, count2, index_max;
  switch (instruction) {
  case OPCODE_REDUCE_SUM_DOUBLE:
    count2 = sizeof(double);
    break;
  case OPCODE_REDUCE_SUM_LONG_INT:
    count2 = sizeof(long int);
    break;
  case OPCODE_REDUCE_SUM_FLOAT:
    count2 = sizeof(float);
    break;
  case OPCODE_REDUCE_SUM_INT:
    count2 = sizeof(int);
    break;
  default:
    count2 = 1;
    break;
  }
  num_streams = *((int *)(p_ref + sizeof(int)));
  memcpy(p_temp, p_ref, 2 * sizeof(int) + sizeof(long int));
  index_max = 0;
  for (num_stream = 0; num_stream < num_streams; num_stream++) {
    index = 0;
    ldata = p_ref + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
    lldata = p_temp + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
    p1 = *((char **)ldata);
    while (p1) {
      p2 = *((char **)(ldata + sizeof(char *)));
      size = *((long int *)(ldata + 2 * sizeof(char *)));
      if (size >= 0) {
        recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, (void **)&p1, (int *)&size);
        recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, (void **)&p2, (int *)&size);
      } else {
        size = -size;
        recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, (void **)&p1, (int *)&size);
        recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, (void **)&p2, (int *)&size);
        size = -size;
      }
      *((char **)lldata) = p1 - (char *)p_ref + (char *)p_dev;
      *((char **)(lldata + sizeof(char *))) = p2 - (char *)p_ref + (char *)p_dev;
      size /= count2;
      *((long int *)(lldata + 2 * sizeof(char *))) = size;
      index++;
      ldata = p_ref + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
      lldata = p_temp + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
      p1 = *((char **)ldata);
    }
    *((char **)lldata) = NULL;
    if (index > index_max) {
      index_max = index;
    }
  }
  ext_mpi_gpu_memcpy_hd(p_dev, p_temp, 2 * sizeof(int) + sizeof(long int) + (num_streams + 1) * index_max * (sizeof(char *) * 2 + sizeof(long int)) + 2 * sizeof(char *) + sizeof(long int));
}
#endif

static int normalize_blocking(char *ip, MPI_Comm comm, int tag, int count, char **send_pointers_allreduce_blocking) {
  char instruction;
  void *p1, *p2;
  int i1, i2, send_pointer = 0;
#ifdef NCCL_ENABLED
  static int initialised = 0;
  static cudaStream_t stream;
  if (!initialised) {
    cudaStreamCreate(&stream);
    initialised = 1;
  }
#endif
  ip += sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      p1 = code_get_pointer(&ip);
      normalize_address(count, &p1);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      p2 = code_get_pointer(&ip);
      normalize_address(count, &p2);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p2, 0);
      i1 = code_get_int(&ip) / count;
      ip -= sizeof(int);
      code_put_int(&ip, i1, 0);
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      normalize_address(count, &p1);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      i1 = code_get_int(&ip) / count;
      ip -= sizeof(int);
      code_put_int(&ip, i1, 0);
      i2 = code_get_int(&ip);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
#else
      if (send_pointers_allreduce_blocking) {
        MPI_Isend(&p1, sizeof(p1), MPI_BYTE, i2, tag, comm, (MPI_Request *)code_get_pointer(&ip));
      } else {
        code_get_pointer(&ip);
      }
#endif
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      normalize_address(count, &p1);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      i1 = code_get_int(&ip) / count;
      ip -= sizeof(int);
      code_put_int(&ip, i1, 0);
      i2 = code_get_int(&ip);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
#else
      if (send_pointers_allreduce_blocking) {
        MPI_Irecv(&send_pointers_allreduce_blocking[send_pointer], sizeof(send_pointers_allreduce_blocking[send_pointer]), MPI_BYTE, i2, tag, comm, (MPI_Request *)code_get_pointer(&ip));
	send_pointer++;
      } else {
        code_get_pointer(&ip);
      }
#endif
      break;
    case OPCODE_MPIWAITALL:
#ifdef NCCL_ENABLED
      code_get_int(&ip);
      code_get_pointer(&ip);
#else
      i1 = code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      if (send_pointers_allreduce_blocking) {
        PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
      }
#endif
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      code_get_int(&ip);
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      code_get_int(&ip);
      break;
    case OPCODE_REDUCE:
      i2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      normalize_address(count, &p1);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      p2 = code_get_pointer(&ip);
      normalize_address(count, &p2);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p2, 0);
      i1 = code_get_int(&ip);
      switch (i2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        i1 /= count / sizeof(double);
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        i1 /= count / sizeof(long int);
        break;
      case OPCODE_REDUCE_SUM_FLOAT:
        i1 /= count / sizeof(float);
        break;
      case OPCODE_REDUCE_SUM_INT:
        i1 /= count / sizeof(int);
        break;
      default:
	i1 /= count;
      }
      ip -= sizeof(int);
      code_put_int(&ip, i1, 0);
      break;
    case OPCODE_ATTACHED:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      break;
    case OPCODE_GPUKERNEL:
      code_get_char(&ip);
      gpu_normalize_addresses(count, code_get_pointer(&ip));
      code_get_int(&ip);
      break;
#endif
#ifdef NCCL_ENABLED
    case OPCODE_START:
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute normalize\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
#ifdef NCCL_ENABLED
//  cudaStreamDestroy(stream);
#endif
  return 0;
}

static int exec_blocking(char *ip, MPI_Comm comm, int tag, char *shmem_socket, int *counter_socket, int socket_rank, int num_cores, char **shmem_node, int *counter_node, int num_sockets_per_node, void **shmem_blocking, const void *sendbuf, void *recvbuf, int count, int reduction_op, int count_io, char **send_pointers, void *p_dev_temp) {
  char instruction;
  void *p1, *p2;
  int i1, i2, send_pointer = 0;
#ifdef GPU_ENABLED
  char instruction2;
#endif
#ifdef NCCL_ENABLED
  static int initialised = 0;
  static cudaStream_t stream;
  if (!initialised) {
    cudaStreamCreate(&stream);
    initialised = 1;
  }
#endif
  ip += sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p1, &i1);
      recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p2, &i1);
      memcpy((void *)p1, (void *)p2, i1);
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      i2 = code_get_int(&ip);
      recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p1, &i1);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
      ncclRecv((void *)p1, i1, ncclChar, i2, ext_mpi_nccl_comm, stream);
#else
      MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, tag, comm,
                (MPI_Request *)code_get_pointer(&ip));
#endif
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      i2 = code_get_int(&ip);
      recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p1, &i1);
      if (send_pointers && send_pointers[send_pointer]) {
	p2 = send_pointers[send_pointer];
        recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p2, &i1);
	send_pointer++;
      }
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
      ncclSend((const void *)p1, i1, ncclChar, i2, ext_mpi_nccl_comm, stream);
#else
      MPI_Isend((void *)p1, i1, MPI_CHAR, i2, tag, comm,
                (MPI_Request *)code_get_pointer(&ip));
#endif
      break;
    case OPCODE_MPIWAITALL:
#ifdef NCCL_ENABLED
      i1 = code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      ncclGroupEnd();
      cudaStreamSynchronize(stream);
#else
      i1 = code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
#endif
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      (*e_socket_barrier)(shmem_socket, counter_socket, socket_rank, num_cores);
      break;
    case OPCODE_NODEBARRIER:
      (*e_node_barrier)(shmem_node, counter_node, socket_rank, num_sockets_per_node);
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      (*e_socket_barrier_atomic_set)(shmem_socket, *counter_socket, code_get_int(&ip));
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      (*e_socket_barrier_atomic_wait)(shmem_socket, counter_socket, code_get_int(&ip));
      break;
    case OPCODE_REDUCE:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p1, &i1);
      recalculate_address_io(sendbuf, recvbuf, count, shmem_blocking, count_io, &p2, &i1);
      switch (reduction_op) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        i1 /= sizeof(double);
        for (i2 = 0; i2 < i1; i2++) {
          ((double *)p1)[i2] += ((double *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        i1 /= sizeof(long int);
        for (i2 = 0; i2 < i1; i2++) {
          ((long int *)p1)[i2] += ((long int *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_FLOAT:
        i1 /= sizeof(float);
        for (i2 = 0; i2 < i1; i2++) {
          ((float *)p1)[i2] += ((float *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_INT:
        i1 /= sizeof(int);
        for (i2 = 0; i2 < i1; i2++) {
          ((int *)p1)[i2] += ((int *)p2)[i2];
        }
        break;
      }
      break;
    case OPCODE_ATTACHED:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      ext_mpi_gpu_synchronize();
      break;
    case OPCODE_GPUKERNEL:
      instruction2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      gpu_recalculate_addresses(p1, sendbuf, recvbuf, count, shmem_blocking, count_io, instruction2, p_dev_temp);
      ext_mpi_gpu_copy_reduce(instruction2, p_dev_temp, code_get_int(&ip));
      break;
#endif
#ifdef NCCL_ENABLED
    case OPCODE_START:
      ncclGroupStart();
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute blocking\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
#ifdef NCCL_ENABLED
//  cudaStreamDestroy(stream);
#endif
  return 0;
}

static long int exec_padding_address(void *p, void *sendbuf, void *recvbuf, void **shmem) {
  long int i;
  i = (long int)(p) >> 28;
  switch (i) {
#ifdef GPU_ENABLED
    case (SEND_PTR_GPU >> 28):
#endif
    case (SEND_PTR_CPU >> 28):
      return p - sendbuf;
      break;
#ifdef GPU_ENABLED
    case (RECV_PTR_GPU >> 28):
#endif
    case (RECV_PTR_CPU >> 28):
      return p - recvbuf;
      break;
    default:
      if (!shmem) {
        return p - NULL;
      } else {
        return p - shmem[i];
      }
  }
}

static int exec_padding(char *ip, void *sendbuf, void *recvbuf, void **shmem, int *numbers_padding) {
  char instruction;
  void *p1, *p2;
  int i1, num_padding = 0;
#ifdef GPU_ENABLED
  char instruction2;
#endif
  ip += sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      i1 = exec_padding_address(p1, sendbuf, recvbuf, shmem);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      i1 = exec_padding_address(p2, sendbuf, recvbuf, shmem);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      i1 = exec_padding_address(p1, sendbuf, recvbuf, shmem);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      code_get_pointer(&ip);
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      code_get_int(&ip);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      i1 = exec_padding_address(p1, sendbuf, recvbuf, shmem);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      code_get_pointer(&ip);
      break;
    case OPCODE_MPIWAITALL:
      i1 = code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      code_get_int(&ip);
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      code_get_int(&ip);
      break;
    case OPCODE_REDUCE:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      numbers_padding[num_padding++] = i1;
      i1 = exec_padding_address(p1, sendbuf, recvbuf, shmem);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      i1 = exec_padding_address(p2, sendbuf, recvbuf, shmem);
      if (i1 > 0) numbers_padding[num_padding++] = i1;
      break;
    case OPCODE_ATTACHED:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      break;
    case OPCODE_GPUKERNEL:
      instruction2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute padding\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return num_padding;
}

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
    normalize_blocking(comm_code_blocking[i], comm, tag, type_size * pfactor, *send_pointers_allreduce_blocking);
  } else {
    normalize_blocking(comm_code_blocking[i], comm, tag, type_size * pfactor, NULL);
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
      handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), j, datatype, op, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 12, copyin, copyin_factors, 0, bit, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
      numbers = (int *)malloc(1024 * 1024 * sizeof(int));
      j_ = exec_padding((*e_comm_code)[handle], (char *)(send_ptr), (char *)(recv_ptr), NULL, numbers);
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
      handle = EXT_MPI_Reduce_scatter_init_native((char *)(send_ptr), (char *)(recv_ptr), recvcounts, datatype, op, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 12, copyin, copyin_factors, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
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
      handle = EXT_MPI_Allgatherv_init_native((char *)(send_ptr), 1, datatype, (char *)(recv_ptr), recvcounts, displs, datatype, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 12, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking);
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
  exec_blocking(comms_blocking_->comm_code_allreduce_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, ((count - 1) / comms_blocking_->padding_factor_allreduce_blocking[i] + 1) * type_size, reduction_op, ccount, comms_blocking_->send_pointers_allreduce_blocking[i], comms_blocking_->p_dev_temp);
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
//  exec_blocking(comms_blocking_->comm_code_reduce_scatter_block_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, recvcount / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i], reduction_op, recvcount, NULL);
  exec_blocking(comms_blocking_->comm_code_reduce_scatter_block_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, recvcount / comms_blocking_->padding_factor_reduce_scatter_block_blocking[i], reduction_op, recvcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL, comms_blocking_->p_dev_temp);
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
//  exec_blocking(comms_blocking_->comm_code_allgather_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, sendcount, -1, sendcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL);
  exec_blocking(comms_blocking_->comm_code_allgather_blocking[i], comms_blocking_->comm_blocking, 1, comms_blocking_->shmem_socket_blocking, &comms_blocking_->counter_socket_blocking, comms_blocking_->socket_rank_blocking, comms_blocking_->num_cores_blocking, comms_blocking_->shmem_node_blocking, &comms_blocking_->counter_node_blocking, comms_blocking_->num_sockets_per_node_blocking, (void **)comms_blocking_->shmem_blocking1, sendbuf, recvbuf, sendcount, -1, sendcount * comms_blocking_->mpi_size_blocking / comms_blocking_->num_cores_blocking, NULL, comms_blocking_->p_dev_temp);
  shmem_temp = comms_blocking_->shmem_blocking1;
  comms_blocking_->shmem_blocking1 = comms_blocking_->shmem_blocking2;
  comms_blocking_->shmem_blocking2 = shmem_temp;
  shmemid_temp = comms_blocking_->shmem_blocking_shmemid1;
  comms_blocking_->shmem_blocking_shmemid1 = comms_blocking_->shmem_blocking_shmemid2;
  comms_blocking_->shmem_blocking_shmemid2 = shmemid_temp;
  return 0;
}
