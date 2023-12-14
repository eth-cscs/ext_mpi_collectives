#include "read_write.h"
#include "constants.h"
#include "byte_code.h"
#include "ext_mpi_native_exec.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <mpi.h>

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

extern MPI_Comm ext_mpi_COMM_WORLD_dup;

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

int ext_mpi_exec_native(char *ip, char **ip_exec, int active_wait) {
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
      MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, header->tag, ext_mpi_COMM_WORLD_dup,
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
      MPI_Isend((void *)p1, i1, MPI_CHAR, i2, header->tag, ext_mpi_COMM_WORLD_dup,
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
               ext_mpi_COMM_WORLD_dup, MPI_STATUS_IGNORE);
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

int ext_mpi_normalize_blocking(char *ip, MPI_Comm comm, int tag, int count, char **send_pointers_allreduce_blocking) {
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

int ext_mpi_exec_blocking(char *ip, MPI_Comm comm, int tag, char *shmem_socket, int *counter_socket, int socket_rank, int num_cores, char **shmem_node, int *counter_node, int num_sockets_per_node, void **shmem_blocking, const void *sendbuf, void *recvbuf, int count, int reduction_op, int count_io, char **send_pointers, void *p_dev_temp) {
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

int ext_mpi_exec_padding(char *ip, void *sendbuf, void *recvbuf, void **shmem, int *numbers_padding) {
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
