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
#include "shmem.h"
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
static MPI_Comm EXT_MPI_COMM_WORLD = MPI_COMM_NULL;
static int tag_max = 0;

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

static int gpu_is_device_pointer(void *buf) {
#ifdef GPU_ENABLED
  if ((void *)(SEND_PTR_CPU) == buf || (void *)(RECV_PTR_CPU) == buf) return 0;
  if ((void *)(SEND_PTR_GPU) == buf || (void *)(RECV_PTR_GPU) == buf) return 1;
  return ext_mpi_gpu_is_device_pointer(buf);
#else
  return 0;
#endif
}

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
    MPI_Comm_rank(EXT_MPI_COMM_WORLD, &grank);
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
    MPI_Comm_rank(EXT_MPI_COMM_WORLD, &grank);
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

static void node_barrier(char **shmem, int *barrier_count, int socket_rank, int num_sockets_per_node) {
  int step;
  for (step = 1; step < num_sockets_per_node; step <<= 1) {
    ((volatile int*)(shmem[0]))[socket_rank * (CACHE_LINE_SIZE / sizeof(int))] = *barrier_count;
    while ((unsigned int)(((volatile int*)(shmem[step]))[socket_rank * (CACHE_LINE_SIZE / sizeof(int))] - *barrier_count) > INT_MAX)
      ;
    (*barrier_count)++;
  }
}

static int node_barrier_test(char **shmem, int barrier_count, int socket_rank, int num_sockets_per_node) {
  int step;
  for (step = 1; step < num_sockets_per_node; step <<= 1) {
    ((volatile int*)(shmem[0]))[socket_rank * (CACHE_LINE_SIZE / sizeof(int))] = barrier_count;
    if ((unsigned int)(((volatile int*)(shmem[step]))[socket_rank * (CACHE_LINE_SIZE / sizeof(int))] - barrier_count) > INT_MAX) {
      return 1;
    }
    barrier_count++;
  }
  return 0;
}

static void socket_barrier(char *shmem, int *barrier_count, int socket_rank, int num_cores) {
  int step;
  for (step = 1; step < num_cores; step <<= 1) {
    ((volatile int*)shmem)[socket_rank * (CACHE_LINE_SIZE / sizeof(int))] = *barrier_count;
    while ((unsigned int)(((volatile int*)shmem)[((socket_rank + step) % num_cores) * (CACHE_LINE_SIZE / sizeof(int))] - *barrier_count) > INT_MAX)
      ;
    (*barrier_count)++;
  }
}

static int socket_barrier_test(char *shmem, int barrier_count, int socket_rank, int num_cores) {
  int step;
  for (step = 1; step < num_cores; step <<= 1) {
    ((volatile int*)shmem)[socket_rank * (CACHE_LINE_SIZE / sizeof(int))] = barrier_count;
    if ((unsigned int)(((volatile int*)shmem)[((socket_rank + step) % num_cores) * (CACHE_LINE_SIZE / sizeof(int))] - barrier_count) > INT_MAX) {
      return 1;
    }
    barrier_count++;
  }
  return 0;
}

static void socket_barrier_atomic_set(char *shmem, int barrier_count, int entry) {
  ((volatile int*)shmem)[entry * (CACHE_LINE_SIZE / sizeof(int))] = barrier_count;
}

static void socket_barrier_atomic_wait(char *shmem, int *barrier_count, int entry) {
  while ((unsigned int)(((volatile int*)shmem)[entry * (CACHE_LINE_SIZE / sizeof(int))] - *barrier_count) > INT_MAX)
    ;
  (*barrier_count)++;
}

static int socket_barrier_atomic_test(char *shmem, int barrier_count, int entry) {
  return (unsigned int)(((volatile int*)shmem)[entry * (CACHE_LINE_SIZE / sizeof(int))] - barrier_count) > INT_MAX;
}

static void reduce_waitany(void **pointers_to, void **pointers_from, int *sizes, int num_red, int op_reduce){
  /*
  Function to perform the reduction operation using MPI_Waitany.
  */
  void *p1, *p2;
  int red_it, i1, i;
            //loop over reduction_iteration 
            for (red_it = 0; red_it<num_red; red_it++) {
              //pointer to which to sum up
              p1 = pointers_to[red_it];
              //pointer from which to sum up
              p2 = pointers_from[red_it];
              //length over which to perform reduction
              i1 = sizes[red_it];
              //switch to datatype that is reduced
              switch (op_reduce) {
              case OPCODE_REDUCE_SUM_DOUBLE:
                for (i = 0; i < i1; i++) {
                  ((double *)p1)[i] += ((double *)p2)[i];
                }
                break;
              case OPCODE_REDUCE_SUM_LONG_INT:
                for (i = 0; i < i1; i++) {
                  ((long int *)p1)[i] += ((long int *)p2)[i];
                }
                break;
              case OPCODE_REDUCE_SUM_FLOAT:
                for (i = 0; i < i1; i++) {
                  ((float *)p1)[i] += ((float *)p2)[i];
                }
                break;
              case OPCODE_REDUCE_SUM_INT:
                for (i = 0; i < i1; i++) {
                  ((int *)p1)[i] += ((int *)p2)[i];
                }
                break;
              }
            }
}

static void exec_waitany(int num_wait, int num_red_max, void *p3, char **ip){
  void *pointers[num_wait][2][abs(num_red_max)], *pointers_temp[abs(num_red_max)];
  int sizes[num_wait][abs(num_red_max)], num_red[num_wait], done = 0, red_it = 0, count, index, num_waitall, target_index, not_moved, i;
        char op, op_reduce=-1;
        for (i=0; i<num_wait; i++){
          num_red[i]=0;
        }
        while (done < num_wait) {
          //get values for reduction operation and put them in a datastructure
          op = code_get_char(ip);
          //check whether an attached or a reduce is encountered
          switch (op) {
          case OPCODE_REDUCE: {
            //get all parameters for all reductions
            op_reduce = code_get_char(ip);
            //get pointer from which to perform reduction operation
            pointers[done][0][num_red[done]] = code_get_pointer(ip);
            //get pointer to which to perform reduction operation
            pointers[done][1][num_red[done]] = code_get_pointer(ip);
            //get for how many elements to perform reduction
            sizes[done][num_red[done]] = code_get_int(ip);
            num_red[done]++;
            break;
          }
          case OPCODE_ATTACHED: {
            //increase done to indicate you are at the next set of reduction operations
            done++;
            break;
          }
	  default:{
	    printf("illegal opcode in exec_waitany\n");
	    exit(1);
          }
          }
        }
        if (num_red_max>=0){
        //if there is any reduction operation to perform, iterate over all waitany and perform the corresponding reduction operations based on the parameters that are just acquired
        for (count=0; count<num_wait; count++) {
            MPI_Waitany(num_wait, (MPI_Request *)p3, &index, MPI_STATUS_IGNORE);
            reduce_waitany(pointers[index][0], pointers[index][1], sizes[index], num_red[index], op_reduce);
        }
        }else{
        num_waitall=0;
        for (count=0; count<num_wait; count++) {
          if (!num_red[count]){
            num_waitall++;
          }
        }
        target_index = -1;
        not_moved = 0;
        for (count=0; count<num_wait; count++) {
            MPI_Waitany(num_wait, (MPI_Request *)p3, &index, MPI_STATUS_IGNORE);
            if (!num_red[index]){
              num_waitall--;
              if ((!num_waitall)&&not_moved){
                reduce_waitany(pointers_temp, pointers[target_index][1], sizes[target_index], num_red[target_index], op_reduce);
                not_moved = 0;
              }
            }else{
              if (num_waitall){
                if (!not_moved){
                  target_index = index;
                  not_moved = 1;
                  for (red_it = 0; red_it<num_red[index]; red_it++) {
                    pointers_temp[red_it] = pointers[index][0][red_it];
                  }
                }else{
                  reduce_waitany(pointers[target_index][1], pointers[index][1], sizes[index], num_red[index], op_reduce);
                }
              }else{
		if (!not_moved){
                  reduce_waitany(pointers[index][0], pointers[index][1], sizes[index], num_red[index], op_reduce);
                }else{
                  reduce_waitany(pointers[target_index][1], pointers[index][1], sizes[index], num_red[index], op_reduce);
		}
              }
            }
        }
        }
}

static int exec_native(char *ip, char **ip_exec, int active_wait) {
  char instruction, instruction2; //, *r_start, *r_temp, *ipl;
  void *p1, *p2, *p3;
  //  char *rlocmem=NULL;
  int i1, i2; //, n_r, s_r, i;
  struct header_byte_code *header;
#ifdef NCCL_ENABLED
  static int initialised = 0;
  static cudaStream_t stream;
  if (!initialised) {
    cudaStreamCreate(&stream);
    initialised = 1;
  }
#endif
  header = (struct header_byte_code *)ip;
  ip = *ip_exec;
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      /*          if (rlocmem != NULL){
                  ipl = r_start;
                  for (i=0; i<n_r; i++){
                    i1 = code_get_int (&ipl);
                    r_temp = r_start+i1;
                    p1 = code_get_pointer (&r_temp);
                    r_temp = r_start+i1;
                    code_put_pointer (&r_temp, (void
         *)((char*)p1-(rlocmem-NULL)), 0);
                  }
                  free(rlocmem);
                }*/
      break;
      /*        case OPCODE_LOCALMEM:
                s_r = code_get_int (&ip);
                n_r = code_get_int (&ip);
                rlocmem = (char *) malloc(s_r);
                r_start = ip;
                for (i=0; i<n_r; i++){
                  i1 = code_get_int (&ip);
                  r_temp = r_start+i1;
                  p1 = code_get_pointer (&r_temp);
                  r_temp = r_start+i1;
                  code_put_pointer (&r_temp, (void *)((char*)p1+(rlocmem-NULL)),
         0);
                }
                break;*/
    case OPCODE_MEMCPY:
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      memcpy((void *)p1, (void *)p2, code_get_int(&ip));
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
      ncclRecv((void *)p1, i1, ncclChar, i2, ext_mpi_nccl_comm, stream);
#else
      MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, header->tag, EXT_MPI_COMM_WORLD,
                (MPI_Request *)code_get_pointer(&ip));
#endif
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
      ncclSend((const void *)p1, i1, ncclChar, i2, ext_mpi_nccl_comm, stream);
#else
      MPI_Isend((void *)p1, i1, MPI_CHAR, i2, header->tag, EXT_MPI_COMM_WORLD,
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
      if (active_wait & 2) {
        i1 = code_get_int(&ip);
        p1 = code_get_pointer(&ip);
        PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
      } else {
        *ip_exec = ip - 1;
        i1 = code_get_int(&ip);
        p1 = code_get_pointer(&ip);
        PMPI_Testall(i1, (MPI_Request *)p1, &i2, MPI_STATUSES_IGNORE);
        if (!i2) {
          return (0);
        } else {
          PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
        }
      }
#endif
      break;
    case OPCODE_MPIWAITANY:
      if (active_wait & 2) {
        // how many to wait for
        i1 = code_get_int(&ip);
        // max reduction size
        i2 = code_get_int(&ip);
        // MPI requests
        p3 = code_get_pointer(&ip);
        exec_waitany(i1, i2, p3, &ip);
      } else {
        printf("not implemented");
        exit(2);
        *ip_exec = ip - 1;
        i1 = code_get_int(&ip);
        p1 = code_get_pointer(&ip);
        PMPI_Testall(i1, (MPI_Request *)p1, &i2, MPI_STATUSES_IGNORE);
        if (!i2) {
          return 0;
        } else {
          PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
        }
      }
      break;
    case OPCODE_SOCKETBARRIER:
      if (active_wait & 2) {
        socket_barrier(header->barrier_shmem_socket + header->barrier_shmem_size, &header->barrier_counter_socket,
                       header->socket_rank, header->num_cores);
      } else {
        *ip_exec = ip - 1;
        if (!socket_barrier_test(header->barrier_shmem_socket + header->barrier_shmem_size, header->barrier_counter_socket,
                                 header->socket_rank, header->num_cores)) {
          return 0;
        } else {
          socket_barrier(header->barrier_shmem_socket + header->barrier_shmem_size, &header->barrier_counter_socket,
                         header->socket_rank, header->num_cores);
        }
      }
      break;
    case OPCODE_NODEBARRIER:
      if (active_wait & 2) {
        node_barrier(header->barrier_shmem_node, &header->barrier_counter_node,
                       header->socket_rank, header->num_sockets_per_node);
      } else {
        *ip_exec = ip - 1;
        if (!node_barrier_test(header->barrier_shmem_node, header->barrier_counter_node,
                                 header->socket_rank, header->num_sockets_per_node)) {
          return 0;
        } else {
          node_barrier(header->barrier_shmem_node, &header->barrier_counter_node,
                         header->socket_rank, header->num_sockets_per_node);
        }
      }
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      socket_barrier_atomic_set(header->barrier_shmem_socket + header->barrier_shmem_size, header->barrier_counter_socket, code_get_int(&ip));
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      i1 = code_get_int(&ip);
      if (active_wait & 2) {
        socket_barrier_atomic_wait(header->barrier_shmem_socket + header->barrier_shmem_size, &header->barrier_counter_socket, i1);
      } else {
        *ip_exec = ip - 1 - sizeof(int);
        if (!socket_barrier_atomic_test(header->barrier_shmem_socket + header->barrier_shmem_size, header->barrier_counter_socket, i1)) {
          return 0;
        } else {
          socket_barrier_atomic_wait(header->barrier_shmem_socket + header->barrier_shmem_size, &header->barrier_counter_socket, i1);
        }
      }
      break;
    case OPCODE_REDUCE:
      instruction2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      switch (instruction2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        for (i2 = 0; i2 < i1; i2++) {
          ((double *)p1)[i2] += ((double *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        for (i2 = 0; i2 < i1; i2++) {
          ((long int *)p1)[i2] += ((long int *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_FLOAT:
        for (i2 = 0; i2 < i1; i2++) {
          ((float *)p1)[i2] += ((float *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_INT:
        for (i2 = 0; i2 < i1; i2++) {
          ((int *)p1)[i2] += ((int *)p2)[i2];
        }
        break;
      }
      break;
    case OPCODE_MPISENDRECV:
      p1 = code_get_pointer(&ip);
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      MPI_Recv((void *)p1, i1, MPI_CHAR, code_get_int(&ip), 0,
               EXT_MPI_COMM_WORLD, MPI_STATUS_IGNORE);
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
      ext_mpi_gpu_copy_reduce(instruction2, (void *)p1, code_get_int(&ip));
      break;
    case OPCODE_GPUGEMV:
      instruction2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      ext_mpi_gemv_exec(&header->gpu_gemv_var, instruction2, p1, i1, code_get_int(&ip));
      break;
#endif
#ifdef NCCL_ENABLED
    case OPCODE_START:
      ncclGroupStart();
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute native\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  *ip_exec = NULL;
#ifdef NCCL_ENABLED
//  cudaStreamDestroy(stream);
#endif
  return 0;
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
    exec_native(comm_code[handle], &execution_pointer[handle],
                active_wait[handle]);
  }
  return (execution_pointer[handle] == NULL);
}

int EXT_MPI_Progress_native() {
  int ret = 0, handle;
  for (handle = 0; handle < handle_code_max; handle += 2) {
    if (execution_pointer[handle]) {
      ret += exec_native(comm_code[handle], &execution_pointer[handle],
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
    exec_native(comm_code[handle], &execution_pointer[handle],
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
    ext_mpi_gpu_destroy_shared_memory(header->num_sockets_per_node, header->shmemid_gpu, header->shmem_gpu, comm_code[handle]);
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
      ext_mpi_gpu_destroy_shared_memory(header->num_sockets_per_node, header->shmemid_gpu, header->shmem_gpu, comm_code[handle]);
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
    ext_mpi_gpu_setup_shared_memory(comm_row, my_cores_per_node_row,
                                    comm_column, my_cores_per_node_column,
                                    shmem_size - barriers_size, num_sockets_per_node, &shmemid_gpu, &shmem_gpu);
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
      ext_mpi_gpu_setup_shared_memory(comm_row, my_cores_per_node_row,
                                      comm_column, my_cores_per_node_column,
                                      shmem_size - barriers_size, num_sockets_per_node, &shmemid_gpu, &shmem_gpu);
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
                               int *groups, int num_active_ports, int copyin, int *copyin_factors,
                               int alt, int bit, int waitany, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor) {
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
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
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
  if (!recursive) {
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
  if ((root >= 0) || (root <= -10)) {
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
  if (ext_mpi_generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (ext_mpi_generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
  if (num_sockets_per_node > 1) {
    if (ext_mpi_messages_shared_memory(buffer2, buffer1, comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column) < 0)
      goto error;
    if (ext_mpi_generate_optimise_multi_socket(buffer1, buffer2) < 0)
      goto error;
  }
  if (waitany&&recursive) {
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
  if (recursive && my_cores_per_node_row * my_cores_per_node_column == 1) {
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
                                  int *groups, int num_active_ports, int copyin,
                                  int *copyin_factors,
                                  int alt, int bit, int waitany,
                                  int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor) {
  return (EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, -1, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, num_active_ports, copyin, copyin_factors, alt, bit, waitany, recursive, blocking, num_sockets_per_node, shmem_zero, locmem, padding_factor));
}

int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int num_active_ports, int copyin, int *copyin_factors,
                              int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
  return (EXT_MPI_Reduce_init_native(
      buffer, buffer, count, datatype, MPI_OP_NULL, -10 - root, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, num_active_ports, copyin, copyin_factors, alt, 0, 0, recursive, blocking, num_sockets_per_node, shmem_zero, locmem, NULL));
}

int EXT_MPI_Gatherv_init_native(
    const void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    const int *recvcounts, const int *displs, MPI_Datatype recvtype, int root,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *groups,
    int num_active_ports, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
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
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
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
  if (!recursive) {
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
  if (recursive && my_cores_per_node_row * my_cores_per_node_column == 1){
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
  if (recursive && my_cores_per_node_row * my_cores_per_node_column == 1){
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
    int num_active_ports, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
  return (EXT_MPI_Gatherv_init_native(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, -1,
      comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
      num_ports, groups, num_active_ports, alt, recursive, blocking, num_sockets_per_node, shmem_zero, locmem));
}

int EXT_MPI_Scatterv_init_native(const void *sendbuf, const int *sendcounts,
                                 const int *displs, MPI_Datatype sendtype,
                                 void *recvbuf, int recvcount,
                                 MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *groups, int num_active_ports,
                                 int copyin, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem) {
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
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
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
    int num_active_ports, int copyin, int *copyin_factors, int alt, int recursive, int blocking, int num_sockets_per_node, int shmem_zero, char *locmem, int *padding_factor) {
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
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
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
  if (!recursive) {
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
  if (recursive && my_cores_per_node_row * my_cores_per_node_column == 1) {
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
  PMPI_Comm_dup(MPI_COMM_WORLD, &EXT_MPI_COMM_WORLD);
  is_initialised = 1;
  return 0;
}

int EXT_MPI_Initialized_native() { return is_initialised; }

int EXT_MPI_Finalize_native() {
  PMPI_Comm_free(&EXT_MPI_COMM_WORLD);
  return 0;
}

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
  int num_streams, num_stream, index, count2;
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
    break;
  }
  num_streams = *((int *)(p_ref + sizeof(int)));
  memcpy(p_temp, p_ref, 2 * sizeof(int) + sizeof(long int));
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
      *((char **)lldata) = p1;
      *((char **)(lldata + sizeof(char *))) = p2;
      size /= count2;
      *((long int *)(lldata + 2 * sizeof(char *))) = size;
      index++;
      ldata = p_ref + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
      lldata = p_temp + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int));
      p1 = *((char **)ldata);
    }
  }
  ext_mpi_gpu_memcpy_hd(p_dev, p_temp, 2 * sizeof(int) + sizeof(long int) + num_streams * index * (sizeof(char *) * 2 + sizeof(long int)) + 2 * sizeof(char *) + sizeof(long int));
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
      socket_barrier(shmem_socket, counter_socket, socket_rank, num_cores);
      break;
    case OPCODE_NODEBARRIER:
      node_barrier(shmem_node, counter_node, socket_rank, num_sockets_per_node);
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_SET:
      socket_barrier_atomic_set(shmem_socket, *counter_socket, code_get_int(&ip));
      break;
    case OPCODE_SOCKETBARRIER_ATOMIC_WAIT:
      socket_barrier_atomic_wait(shmem_socket, counter_socket, code_get_int(&ip));
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

static int add_blocking_member(int count, MPI_Datatype datatype, int handle, char **comm_code_blocking, int *count_blocking, int pfactor, MPI_Comm comm, int tag, char ***send_pointers_allreduce_blocking) {
  int type_size, i = 0, comm_size;
  MPI_Type_size(datatype, &type_size);
  while (comm_code_blocking[i]) i++;
  comm_code_blocking[i] = comm_code[handle];
  comm_code[handle] = 0;
  execution_pointer[handle] = NULL;
  active_wait[handle] = 0;
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
  int handle, size_shared = 1024*1024, *loc_shmemid, *recvcounts, *displs, padding_factor, type_size, i;
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
      if ((*comms_blocking)[i_comm]->mpi_size_blocking > (*comms_blocking)[i_comm]->num_cores_blocking || count * type_size > CACHE_LINE_SIZE) {
        handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), (*comms_blocking)[i_comm]->mpi_size_blocking * CACHE_LINE_SIZE, datatype, op, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 12, copyin, copyin_factors, 0, bit, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
      } else {
        handle = EXT_MPI_Allreduce_init_native((char *)(send_ptr), (char *)(recv_ptr), 1, datatype, op, (*comms_blocking)[i_comm]->comm_blocking, my_cores_per_node, MPI_COMM_NULL, 1, num_ports, groups, 12, copyin, copyin_factors, 0, bit, 0, arecursive, 0, num_sockets_per_node, 1, (*comms_blocking)[i_comm]->locmem_blocking, &padding_factor);
        padding_factor = 1;
      }
      for (i = 0; (*comms_blocking)[i_comm]->comm_code_allreduce_blocking[i]; i++)
        ;
      (*comms_blocking)[i_comm]->padding_factor_allreduce_blocking[i] = padding_factor;
      if ((*comms_blocking)[i_comm]->mpi_size_blocking > (*comms_blocking)[i_comm]->num_cores_blocking || count * type_size > CACHE_LINE_SIZE) {
	add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allreduce_blocking, (*comms_blocking)[i_comm]->count_allreduce_blocking, (*comms_blocking)[i_comm]->mpi_size_blocking * CACHE_LINE_SIZE / padding_factor, comm, 1, &((*comms_blocking)[i_comm]->send_pointers_allreduce_blocking[i]));
      } else {
	add_blocking_member(count, datatype, handle, (*comms_blocking)[i_comm]->comm_code_allreduce_blocking, (*comms_blocking)[i_comm]->count_allreduce_blocking, 1, comm, 1, &((*comms_blocking)[i_comm]->send_pointers_allreduce_blocking[i]));
      }
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
  add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking, SEND_PTR_CPU, RECV_PTR_CPU);
#ifdef GPU_ENABLED
  add_blocking_native(count, datatype, op, comm, my_cores_per_node, num_ports, groups, copyin, copyin_factors, bit, recursive, arecursive, blocking, num_sockets_per_node, collective_type, i_comm, &comms_blocking_gpu, SEND_PTR_GPU, RECV_PTR_GPU);
#endif
  return 0;
}

int EXT_MPI_Release_blocking_native(int i_comm) {
  struct header_byte_code *header;
  int *loc_shmemid, i;
  char **loc_shmem;
  header = (struct header_byte_code *)malloc(sizeof(struct header_byte_code) + 2 * sizeof(MPI_Comm));
  header->size_to_return = sizeof(struct header_byte_code);
  *((MPI_Comm *)(((char *)header)+header->size_to_return)) = comms_blocking[i_comm]->comm_row_blocking;
  *((MPI_Comm *)(((char *)header)+header->size_to_return+sizeof(MPI_Comm))) = comms_blocking[i_comm]->comm_column_blocking;
  free(comms_blocking[i_comm]->padding_factor_allreduce_blocking);
  free(comms_blocking[i_comm]->padding_factor_reduce_scatter_block_blocking);
  free(comms_blocking[i_comm]->count_allreduce_blocking);
  free(comms_blocking[i_comm]->count_reduce_scatter_block_blocking);
  free(comms_blocking[i_comm]->count_allgather_blocking);
#ifndef GPU_ENABLED
  ext_mpi_destroy_shared_memory(0, 1, comms_blocking[i_comm]->shmem_blocking_shmemid1, comms_blocking[i_comm]->shmem_blocking1, (char *)header);
  ext_mpi_destroy_shared_memory(0, 1, comms_blocking[i_comm]->shmem_blocking_shmemid2, comms_blocking[i_comm]->shmem_blocking2, (char *)header);
#else
  ext_mpi_gpu_destroy_shared_memory(1, comms_blocking[i_comm]->shmem_blocking_shmemid1, comms_blocking[i_comm]->shmem_blocking1, (char *)header);
  ext_mpi_gpu_destroy_shared_memory(1, comms_blocking[i_comm]->shmem_blocking_shmemid2, comms_blocking[i_comm]->shmem_blocking2, (char *)header);
#endif
  free(comms_blocking[i_comm]->locmem_blocking);
  loc_shmemid = (int *)malloc(1 * sizeof(int));
  *loc_shmemid = comms_blocking[i_comm]->shmem_socket_blocking_shmemid;
  loc_shmem = (char **)malloc(1 * sizeof(char *));
  *loc_shmem = comms_blocking[i_comm]->shmem_socket_blocking;
  ext_mpi_destroy_shared_memory(0, 1, loc_shmemid, loc_shmem, (char *)header);
  ext_mpi_destroy_shared_memory(0, comms_blocking[i_comm]->shmem_node_blocking_num_segments, comms_blocking[i_comm]->shmem_node_blocking_shmemid, comms_blocking[i_comm]->shmem_node_blocking, (char *)header);
  free(header);
  for (i = 0; i < 101; i++) {
    free(comms_blocking[i_comm]->send_pointers_allreduce_blocking[i]);
    free(comms_blocking[i_comm]->comm_code_allreduce_blocking[i]);
    free(comms_blocking[i_comm]->comm_code_reduce_scatter_block_blocking[i]);
    free(comms_blocking[i_comm]->comm_code_allgather_blocking[i]);
  }
  free(comms_blocking[i_comm]->send_pointers_allreduce_blocking);
  free(comms_blocking[i_comm]->comm_code_allreduce_blocking);
  free(comms_blocking[i_comm]->comm_code_reduce_scatter_block_blocking);
  free(comms_blocking[i_comm]->comm_code_allgather_blocking);
  PMPI_Comm_free(&comms_blocking[i_comm]->comm_blocking);
#ifdef GPU_ENABLED
  ext_mpi_gpu_free(comms_blocking[i_comm]->p_dev_temp);
#endif
  free(comms_blocking[i_comm]);
  comms_blocking[i_comm] = NULL;
  if (i_comm == 0) {
    free(comms_blocking);
    comms_blocking = NULL;
  }
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
  while (comms_blocking_->count_allreduce_blocking[i] < recvcount && comms_blocking_->comm_code_reduce_scatter_block_blocking[i + 1]) i++;
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
  while (comms_blocking_->count_allreduce_blocking[i] < sendcount && comms_blocking_->comm_code_allgather_blocking[i + 1]) i++;
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
