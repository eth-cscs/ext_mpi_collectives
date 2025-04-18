#include "read_write.h"
#include "constants.h"
#include "byte_code.h"
#include "ext_mpi_native.h"
#include "ext_mpi_native_exec.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#include "gpu_shmem.h"
#include "cuda_gemv.h"
#endif
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <mpi.h>

extern MPI_Comm ext_mpi_COMM_WORLD_dup;

static void node_barrier(int **shmem, int *barrier_count, int socket_rank, int num_sockets_per_node) {
  int step, *p, bc;
  memory_fence_store();
  for (step = 1; step < num_sockets_per_node; step <<= 1) {
    bc = *(shmem[0]) = ++(*barrier_count);
    p = shmem[step];
    while ((unsigned int)(*((volatile int*)(p)) - bc) > INT_MAX)
      ;
  }
  memory_fence_load();
}

static int node_barrier_test(int **shmem, int barrier_count, int socket_rank, int num_sockets_per_node) {
  int step, *p;
  memory_fence_store();
  for (step = 1; step < num_sockets_per_node; step <<= 1) {
    *(shmem[0]) = ++barrier_count;
    p = shmem[step];
    if ((unsigned int)(*((volatile int*)(p)) - barrier_count) > INT_MAX) {
      return 1;
    }
  }
  memory_fence_load();
  return 0;
}

static void socket_barrier(int **shmem, int *barrier_count, int num_cores) {
  int step, *p, bc;
  memory_fence_store();
  for (step = 1; step < num_cores; step <<= 1) {
    bc = *(shmem[0]) = ++(*barrier_count);
    p = shmem[step];
    while ((unsigned int)(*((volatile int*)(p)) - bc) > INT_MAX)
      ;
  }
  memory_fence_load();
}

static int socket_barrier_test(int **shmem, int barrier_count, int num_cores) {
  int step, *p;
  memory_fence_store();
  for (step = 1; step < num_cores; step <<= 1) {
    *(shmem[0]) = ++barrier_count;
    p = shmem[step];
    if ((unsigned int)(*((volatile int*)(p)) - barrier_count) > INT_MAX) {
      return 1;
    }
  }
  memory_fence_load();
  return 0;
}

static void node_barrier_atomic_set(int *shmem, int *barrier_count) {
  *shmem = ++(*barrier_count);
}

static void node_barrier_atomic_wait(int *shmem, int barrier_count) {
  while ((unsigned int)(*((volatile int*)(shmem)) - barrier_count) > INT_MAX)
    ;
}

static int node_barrier_atomic_test(int *shmem, int barrier_count) {
  return (unsigned int)(*((volatile int*)(shmem)) - barrier_count) > INT_MAX;
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
            ext_mpi_call_mpi(MPI_Waitany(num_wait, (MPI_Request *)p3, &index, MPI_STATUS_IGNORE));
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
            ext_mpi_call_mpi(MPI_Waitany(num_wait, (MPI_Request *)p3, &index, MPI_STATUS_IGNORE));
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

static void reduction(int instruction2, void *p1, void *p2, int i1, void *mpi_user_function, int blocking) {
  int i2;
  MPI_Datatype datatype;
  switch (instruction2) {
  case OPCODE_REDUCE_SUM_DOUBLE:
    if (blocking) i1 /= sizeof(double);
    for (i2 = 0; i2 < i1; i2++) {
      ((double *)p1)[i2] += ((double *)p2)[i2];
    }
    break;
  case OPCODE_REDUCE_SUM_LONG_INT:
    if (blocking) i1 /= sizeof(long int);
    for (i2 = 0; i2 < i1; i2++) {
      ((long int *)p1)[i2] += ((long int *)p2)[i2];
    }
    break;
  case OPCODE_REDUCE_SUM_FLOAT:
    if (blocking) i1 /= sizeof(float);
    for (i2 = 0; i2 < i1; i2++) {
      ((float *)p1)[i2] += ((float *)p2)[i2];
    }
    break;
  case OPCODE_REDUCE_SUM_INT:
    if (blocking) i1 /= sizeof(int);
    for (i2 = 0; i2 < i1; i2++) {
      ((int *)p1)[i2] += ((int *)p2)[i2];
    }
    break;
  case OPCODE_REDUCE_SUM_CHAR:
    for (i2 = 0; i2 < i1; i2++) {
      ((char *)p1)[i2] += ((char *)p2)[i2];
    }
    break;
  case OPCODE_REDUCE_MIN_DOUBLE:
    if (blocking) i1 /= sizeof(double);
    for (i2 = 0; i2 < i1; i2++) {
      if (((double *)p2)[i2] < ((double *)p1)[i2]) {
        ((double *)p1)[i2] = ((double *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MIN_LONG_INT:
    if (blocking) i1 /= sizeof(long int);
    for (i2 = 0; i2 < i1; i2++) {
	  if (((long int *)p2)[i2] < ((long int *)p1)[i2]) {
        ((long int *)p1)[i2] = ((long int *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MIN_FLOAT:
    if (blocking) i1 /= sizeof(float);
    for (i2 = 0; i2 < i1; i2++) {
	  if (((float *)p2)[i2] < ((float *)p1)[i2]) {
        ((float *)p1)[i2] = ((float *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MIN_INT:
    if (blocking) i1 /= sizeof(int);
    for (i2 = 0; i2 < i1; i2++) {
	  if (((int *)p2)[i2] < ((int *)p1)[i2]) {
        ((int *)p1)[i2] = ((int *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MIN_CHAR:
    for (i2 = 0; i2 < i1; i2++) {
	  if (((char *)p2)[i2] < ((char *)p1)[i2]) {
        ((char *)p1)[i2] = ((char *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MAX_DOUBLE:
    if (blocking) i1 /= sizeof(double);
    for (i2 = 0; i2 < i1; i2++) {
      if (((double *)p2)[i2] > ((double *)p1)[i2]) {
        ((double *)p1)[i2] = ((double *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MAX_LONG_INT:
    if (blocking) i1 /= sizeof(long int);
    for (i2 = 0; i2 < i1; i2++) {
	  if (((long int *)p2)[i2] > ((long int *)p1)[i2]) {
        ((long int *)p1)[i2] = ((long int *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MAX_FLOAT:
    if (blocking) i1 /= sizeof(float);
    for (i2 = 0; i2 < i1; i2++) {
	  if (((float *)p2)[i2] > ((float *)p1)[i2]) {
        ((float *)p1)[i2] = ((float *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MAX_INT:
    if (blocking) i1 /= sizeof(int);
    for (i2 = 0; i2 < i1; i2++) {
	  if (((int *)p2)[i2] > ((int *)p1)[i2]) {
        ((int *)p1)[i2] = ((int *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_MAX_CHAR:
    for (i2 = 0; i2 < i1; i2++) {
	  if (((char *)p2)[i2] > ((char *)p1)[i2]) {
        ((char *)p1)[i2] = ((char *)p2)[i2];
	  }
    }
    break;
  case OPCODE_REDUCE_USER_DOUBLE:
    if (blocking) i1 /= sizeof(double);
	datatype = MPI_DOUBLE;
	((MPI_User_function *)(mpi_user_function))(p2, p1, &i1, &datatype);
	break;
  case OPCODE_REDUCE_USER_LONG_INT:
    if (blocking) i1 /= sizeof(long int);
	datatype = MPI_LONG_INT;
	((MPI_User_function *)(mpi_user_function))(p2, p1, &i1, &datatype);
	break;
  case OPCODE_REDUCE_USER_FLOAT:
    if (blocking) i1 /= sizeof(float);
	datatype = MPI_FLOAT;
	((MPI_User_function *)(mpi_user_function))(p2, p1, &i1, &datatype);
	break;
  case OPCODE_REDUCE_USER_INT:
    if (blocking) i1 /= sizeof(int);
	datatype = MPI_INT;
	((MPI_User_function *)(mpi_user_function))(p2, p1, &i1, &datatype);
	break;
  case OPCODE_REDUCE_USER_CHAR:
	datatype = MPI_CHAR;
	((MPI_User_function *)(mpi_user_function))(p2, p1, &i1, &datatype);
	break;
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
      ext_mpi_call_mpi(MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, header->tag, ext_mpi_COMM_WORLD_dup,
                      (MPI_Request *)code_get_pointer(&ip)));
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
      ext_mpi_call_mpi(MPI_Isend((void *)p1, i1, MPI_CHAR, i2, header->tag, ext_mpi_COMM_WORLD_dup,
                                 (MPI_Request *)code_get_pointer(&ip)));
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
        ext_mpi_call_mpi(PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE));
      } else {
        *ip_exec = ip - 1;
        i1 = code_get_int(&ip);
        p1 = code_get_pointer(&ip);
        ext_mpi_call_mpi(PMPI_Testall(i1, (MPI_Request *)p1, &i2, MPI_STATUSES_IGNORE));
        if (!i2) {
          return 0;
        } else {
          ext_mpi_call_mpi(PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE));
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
        ext_mpi_call_mpi(PMPI_Testall(i1, (MPI_Request *)p1, &i2, MPI_STATUSES_IGNORE));
        if (!i2) {
          return 0;
        } else {
          ext_mpi_call_mpi(PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE));
        }
      }
      break;
    case OPCODE_SOCKETBARRIER:
      if (active_wait & 2) {
        socket_barrier((int **)header->barrier_shmem_socket, &header->barrier_counter_socket,
                       header->num_cores_socket_barrier);
      } else {
        *ip_exec = ip - 1;
        if (!socket_barrier_test((int **)header->barrier_shmem_socket, header->barrier_counter_socket,
                                 header->num_cores_socket_barrier)) {
          return 0;
        } else {
          socket_barrier((int **)header->barrier_shmem_socket, &header->barrier_counter_socket,
                         header->num_cores_socket_barrier);
        }
      }
      break;
    case OPCODE_SOCKETBSMALL:
      if (active_wait & 2) {
        socket_barrier((int **)header->barrier_shmem_socket_small, &header->barrier_counter_socket,
                       header->num_cores_socket_barrier_small);
      } else {
        *ip_exec = ip - 1;
        if (!socket_barrier_test((int **)header->barrier_shmem_socket_small, header->barrier_counter_socket,
                                 header->num_cores_socket_barrier_small)) {
          return 0;
        } else {
          socket_barrier((int **)header->barrier_shmem_socket_small, &header->barrier_counter_socket,
                         header->num_cores_socket_barrier_small);
        }
      }
      break;
    case OPCODE_NODEBARRIER:
      if (active_wait & 2) {
        node_barrier((int **)header->barrier_shmem_node, &header->barrier_counter_node,
                       header->socket_rank, header->num_sockets_per_node);
      } else {
        *ip_exec = ip - 1;
        if (!node_barrier_test((int **)header->barrier_shmem_node, header->barrier_counter_node,
                                 header->socket_rank, header->num_sockets_per_node)) {
          return 0;
        } else {
          node_barrier((int **)header->barrier_shmem_node, &header->barrier_counter_node,
                         header->socket_rank, header->num_sockets_per_node);
        }
      }
      break;
    case OPCODE_NODEBARRIER_ATOMIC_SET:
      node_barrier_atomic_set((int *)code_get_pointer(&ip), &header->barrier_counter_node);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_WAIT:
      p1 = code_get_pointer(&ip);
      if (active_wait & 2) {
        node_barrier_atomic_wait((int *)p1, header->barrier_counter_node);
      } else {
        *ip_exec = ip - 1 - sizeof(char *);
        if (!node_barrier_atomic_test((int *)p1, header->barrier_counter_node)) {
          return 0;
        } else {
          node_barrier_atomic_wait((int *)p1, header->barrier_counter_node);
        }
      }
      break;
    case OPCODE_REDUCE:
    case OPCODE_INVREDUCE:
      instruction2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      reduction(instruction2, p1, p2, i1, header->mpi_user_function, 0);
      break;
    case OPCODE_MPISENDRECV:
      p1 = code_get_pointer(&ip);
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      ext_mpi_call_mpi(MPI_Recv((void *)p1, i1, MPI_CHAR, code_get_int(&ip), 0,
                                ext_mpi_COMM_WORLD_dup, MPI_STATUS_IGNORE));
      break;
    case OPCODE_ATTACHED:
      break;
    case OPCODE_MEMORY_FENCE:
      memory_fence();
      break;
    case OPCODE_MEMORY_FENCE_STORE:
      memory_fence_store();
      break;
    case OPCODE_MEMORY_FENCE_LOAD:
      memory_fence_load();
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

static void normalize_address(int count, void **address) {
  *address = (void *)(((unsigned long int)(*address) & 0xFFFFFFFFFFFF) / count | ((unsigned long int)(*address) & 0xFFFF000000000000));
}

static void recalculate_address_io(void **sendbufs, void **recvbufs, int count, void **shmem, int size_io, void **address, int *size) {
  long int i, j;
  i = (unsigned long int)(*address) >> 60;
  j = ((unsigned long int)(*address) >> 48) & 0xFFF;
  switch (i) {
#ifdef GPU_ENABLED
    case (SEND_PTR_GPU >> 60):
#endif
    case (SEND_PTR_CPU >> 60):
      *address = (void *)(((unsigned long int)(*address) & 0xFFFFFFFFFFFF) * count + (unsigned long int)(sendbufs[j]));
      if ((char *)(*address) + *size > (char *)sendbufs[j] + size_io) {
        *size = (char *)sendbufs[j] + size_io - (char *)(*address);
        if (*size < 0) *size = 0;
      }
      break;
#ifdef GPU_ENABLED
    case (RECV_PTR_GPU >> 60):
#endif
    case (RECV_PTR_CPU >> 60):
      *address = (void *)(((unsigned long int)(*address) & 0xFFFFFFFFFFFF) * count + (unsigned long int)(recvbufs[j]));
      if ((char *)(*address) + *size > (char *)recvbufs[j] + size_io) {
        *size = (char *)recvbufs[j] + size_io - (char *)(*address);
        if (*size < 0) *size = 0;
      }
      break;
#ifdef GPU_ENABLED
    case (SHMEM_PTR_GPU >> 60):
#endif
    case (SHMEM_PTR_CPU >> 60):
      *address = (void *)(((unsigned long int)(*address) & 0xFFFFFFFFFFFF) * count + (unsigned long int)(shmem[j]));
      break;
    default:
      printf("error in recalculate_address_io file ext_mpi_native_exec.c\n");
      exit(1);
  }
}

#ifdef GPU_ENABLED
static void gpu_normalize_addresses(int count, void *p) {
  char *ldata, *p1, *p2;
  long int size, start;
  int num_streams, num_stream, index;
  num_streams = *((int *)(p + sizeof(int)));
  for (num_stream = 0; num_stream < num_streams; num_stream++) {
    index = 0;
    ldata = p + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int) * 2);
    p1 = *((char **)ldata);
    while (p1) {
      p2 = *((char **)(ldata + sizeof(char *)));
      size = *((long int *)(ldata + 2 * sizeof(char *)));
      start = *((long int *)(ldata + 2 * sizeof(char *) + sizeof(long int)));
      normalize_address(count, (void **)&p1);
      normalize_address(count, (void **)&p2);
//      size /= count;
//      start /= count;
      *((char **)ldata) = p1;
      *((char **)(ldata + sizeof(char *))) = p2;
      *((long int *)(ldata + 2 * sizeof(char *))) = size;
      *((long int *)(ldata + 2 * sizeof(char *) + sizeof(long int))) = start;
      index++;
      ldata = p + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int) * 2);
      p1 = *((char **)ldata);
    }
  }
}

static void gpu_recalculate_addresses(void *p_ref, void **sendbufs, void **recvbufs, int count, void **shmem_blocking, int count_io, int instruction, void *p_dev) {
  char *ldata, *lldata, p_temp[10000], *p1, *p2;
  long int size, start, size_max;
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
  size_max = *((long int *)(p_temp + 2 * sizeof(int))) * count;
  *((long int *)(p_temp + 2 * sizeof(int))) = size_max / count2;
  index_max = 0;
  for (num_stream = 0; num_stream < num_streams; num_stream++) {
    index = 0;
    ldata = p_ref + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int) * 2);
    lldata = p_temp + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int) * 2);
    p1 = *((char **)ldata);
    while (p1) {
      p2 = *((char **)(ldata + sizeof(char *)));
      size = *((long int *)(ldata + 2 * sizeof(char *))) * count;
      start = *((long int *)(ldata + 2 * sizeof(char *) + sizeof(long int))) * count;
      if (size >= 0) {
        recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, (void **)&p1, (int *)&size);
        recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, (void **)&p2, (int *)&size);
      } else {
        size = -size;
        recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, (void **)&p1, (int *)&size);
        recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, (void **)&p2, (int *)&size);
        size = -size;
      }
      *((char **)lldata) = p1;
      *((char **)(lldata + sizeof(char *))) = p2;
      size /= count2;
      start /= count2;
      *((long int *)(lldata + 2 * sizeof(char *))) = size;
      *((long int *)(lldata + 2 * sizeof(char *) + sizeof(long int))) = start;
      index++;
      ldata = p_ref + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int) * 2);
      lldata = p_temp + 2 * sizeof(int) + sizeof(long int) + (num_streams * index + num_stream) * (sizeof(char *) * 2 + sizeof(long int) * 2);
      p1 = *((char **)ldata);
    }
    *((char **)lldata) = NULL;
    if (index > index_max) {
      index_max = index;
    }
  }
  ext_mpi_gpu_memcpy_hd(p_dev, p_temp, 2 * sizeof(int) + sizeof(long int) + (num_streams + 1) * index_max * (sizeof(char *) * 2 + sizeof(long int) * 2) + 2 * sizeof(char *) + sizeof(long int));
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
        ext_mpi_call_mpi(MPI_Isend(&p1, sizeof(p1), MPI_BYTE, i2, tag, comm, (MPI_Request *)code_get_pointer(&ip)));
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
        ext_mpi_call_mpi(MPI_Irecv(&send_pointers_allreduce_blocking[send_pointer], sizeof(send_pointers_allreduce_blocking[send_pointer]), MPI_BYTE, i2, tag, comm, (MPI_Request *)code_get_pointer(&ip)));
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
        ext_mpi_call_mpi(PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE));
      }
#endif
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      break;
    case OPCODE_SOCKETBSMALL:
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_NODEBARRIER_ATOMIC_SET:
      code_get_pointer(&ip);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_WAIT:
      code_get_pointer(&ip);
      break;
    case OPCODE_REDUCE:
    case OPCODE_INVREDUCE:
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
      i1 /= count / get_type_size(i2);
      ip -= sizeof(int);
      code_put_int(&ip, i1, 0);
      break;
    case OPCODE_ATTACHED:
      break;
    case OPCODE_MEMORY_FENCE:
      break;
    case OPCODE_MEMORY_FENCE_STORE:
      break;
    case OPCODE_MEMORY_FENCE_LOAD:
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

int ext_mpi_exec_blocking(char *ip, int tag, char **shmem_socket, int *counter_socket, int socket_rank, int num_cores, char **shmem_node, int *counter_node, int num_sockets_per_node, void **shmem_blocking, void **sendbufs, void **recvbufs, int count, int reduction_op, int count_io, void *p_dev_temp) {
  char instruction;
  void *p1, *p2;
  int i1, i2;
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
      recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, &p1, &i1);
      recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, &p2, &i1);
      memcpy((void *)p1, (void *)p2, i1);
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      i2 = code_get_int(&ip);
      recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, &p1, &i1);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
      ncclRecv((void *)p1, i1, ncclChar, i2, ext_mpi_nccl_comm, stream);
#else
      ext_mpi_call_mpi(MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, tag, ext_mpi_COMM_WORLD_dup,
                                 (MPI_Request *)code_get_pointer(&ip)));
#endif
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      i2 = code_get_int(&ip);
      recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, &p1, &i1);
#ifdef NCCL_ENABLED
      code_get_pointer(&ip);
      ncclSend((const void *)p1, i1, ncclChar, i2, ext_mpi_nccl_comm, stream);
#else
      ext_mpi_call_mpi(MPI_Isend((void *)p1, i1, MPI_CHAR, i2, tag, ext_mpi_COMM_WORLD_dup,
                                 (MPI_Request *)code_get_pointer(&ip)));
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
      ext_mpi_call_mpi(PMPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE));
#endif
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      socket_barrier((int **)shmem_socket, counter_socket, num_cores);
      break;
    case OPCODE_SOCKETBSMALL:
      socket_barrier((int **)shmem_socket, counter_socket, num_cores);
      break;
    case OPCODE_NODEBARRIER:
      node_barrier((int **)shmem_node, counter_node, socket_rank, num_sockets_per_node);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_SET:
      code_get_pointer(&ip);
      node_barrier_atomic_set((int *)(shmem_node[0]), counter_node);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_WAIT:
      i1 = (unsigned long int)code_get_pointer(&ip);
      node_barrier_atomic_wait((int *)(shmem_node[i1]), *counter_node);
      break;
    case OPCODE_REDUCE:
    case OPCODE_INVREDUCE:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip) * count;
      recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, &p1, &i1);
      recalculate_address_io(sendbufs, recvbufs, count, shmem_blocking, count_io, &p2, &i1);
      reduction(reduction_op, p1, p2, i1, NULL, 1);
      break;
    case OPCODE_ATTACHED:
      break;
    case OPCODE_MEMORY_FENCE:
      memory_fence();
      break;
    case OPCODE_MEMORY_FENCE_STORE:
      memory_fence_store();
      break;
    case OPCODE_MEMORY_FENCE_LOAD:
      memory_fence_load();
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      ext_mpi_gpu_synchronize();
      break;
    case OPCODE_GPUKERNEL:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      gpu_recalculate_addresses(p1, sendbufs, recvbufs, count, shmem_blocking, count_io, reduction_op, p_dev_temp);
      code_get_int(&ip);
      ext_mpi_gpu_copy_reduce(reduction_op, p_dev_temp, count);
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
  long int i, j;
  i = (unsigned long int)(p) >> 60;
  j = ((unsigned long int)(p) >> 48) & 0xFFF;
  switch (i) {
#ifdef GPU_ENABLED
    case (SEND_PTR_GPU >> 60):
#endif
    case (SEND_PTR_CPU >> 60):
      return p - sendbuf;
      break;
#ifdef GPU_ENABLED
    case (RECV_PTR_GPU >> 60):
#endif
    case (RECV_PTR_CPU >> 60):
      return p - recvbuf;
      break;
#ifdef GPU_ENABLED
    case (SHMEM_PTR_GPU >> 60):
#endif
    case (SHMEM_PTR_CPU >> 60):
      if (!shmem) {
        return p - NULL;
      } else {
        return p - shmem[j];
      }
      break;
    default:
      exit(1);
  }
}

int ext_mpi_exec_padding(char *ip, void *sendbuf, void *recvbuf, void **shmem, int *numbers_padding) {
  char instruction;
  void *p1, *p2;
  int i1, num_padding = 0;
  printf("not implemented\n"); exit(1);
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
    case OPCODE_SOCKETBSMALL:
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_NODEBARRIER_ATOMIC_SET:
      code_get_pointer(&ip);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_WAIT:
      code_get_pointer(&ip);
      break;
    case OPCODE_REDUCE:
    case OPCODE_INVREDUCE:
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
    case OPCODE_MEMORY_FENCE:
      break;
    case OPCODE_MEMORY_FENCE_STORE:
      break;
    case OPCODE_MEMORY_FENCE_LOAD:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      break;
    case OPCODE_GPUKERNEL:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      code_get_int(&ip);
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute padding\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return num_padding;
}

int EXT_MPI_Collective_to_disc(char *ip, char *locmem_blocking, int *rank_list, char *raw_code, char *gpu_byte_code) {
  char instruction;
  char *p1;
  int i1, i2;
  memcpy(raw_code, ip, ((struct header_byte_code*)(ip))->size_to_return);
  ip = raw_code + sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_MPIIRECV:
      code_get_pointer(&ip);
      code_get_int(&ip);
      i1 = code_get_int(&ip);
      ip -= sizeof(int);
      for (i2 = 0; rank_list[i2] != i1; i2++);
      code_put_int(&ip, i2, 0);
      p1 = code_get_pointer(&ip);
      p1 = (char*)NULL + (p1 - locmem_blocking);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      break;
    case OPCODE_MPIISEND:
      code_get_pointer(&ip);
      code_get_int(&ip);
      i1 = code_get_int(&ip);
      ip -= sizeof(int);
      for (i2 = 0; rank_list[i2] != i1; i2++);
      code_put_int(&ip, i2, 0);
      p1 = code_get_pointer(&ip);
      p1 = (char*)NULL + (p1 - locmem_blocking);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      break;
    case OPCODE_MPIWAITALL:
      code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      p1 = (char*)NULL + (p1 - locmem_blocking);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      break;
    case OPCODE_SOCKETBSMALL:
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_NODEBARRIER_ATOMIC_SET:
      code_get_pointer(&ip);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_WAIT:
      code_get_pointer(&ip);
      break;
    case OPCODE_REDUCE:
    case OPCODE_INVREDUCE:
      code_get_char(&ip);
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_ATTACHED:
      break;
    case OPCODE_MEMORY_FENCE:
      break;
    case OPCODE_MEMORY_FENCE_STORE:
      break;
    case OPCODE_MEMORY_FENCE_LOAD:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      break;
    case OPCODE_GPUKERNEL:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p1 = (char*)NULL + (p1 - gpu_byte_code);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      code_get_int(&ip);
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute to disc\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return 0;
}

char * EXT_MPI_Collective_from_disc(char *raw_code, char *locmem_blocking, int *rank_list, char *gpu_byte_code) {
  char instruction;
  char *ip, *p1, *ret;
  int i1;
  ret = ip = (char*)malloc(((struct header_byte_code*)(raw_code))->size_to_return);
  memcpy(ip, raw_code, ((struct header_byte_code*)(raw_code))->size_to_return);
  ip += sizeof(struct header_byte_code);
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_MPIIRECV:
      code_get_pointer(&ip);
      code_get_int(&ip);
      i1 = code_get_int(&ip);
      ip -= sizeof(int);
      code_put_int(&ip, rank_list[i1], 0);
      p1 = code_get_pointer(&ip);
      p1 = (p1 - (char*)NULL) + locmem_blocking;
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      break;
    case OPCODE_MPIISEND:
      code_get_pointer(&ip);
      code_get_int(&ip);
      i1 = code_get_int(&ip);
      ip -= sizeof(int);
      code_put_int(&ip, rank_list[i1], 0);
      p1 = code_get_pointer(&ip);
      p1 = (p1 - (char*)NULL) + locmem_blocking;
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      break;
    case OPCODE_MPIWAITALL:
      code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      p1 = (p1 - (char*)NULL) + locmem_blocking;
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      break;
    case OPCODE_MPIWAITANY:
      printf("OPCODE_MPIWAITANY not implemented\n");
      exit(9);
      break;
    case OPCODE_SOCKETBARRIER:
      break;
    case OPCODE_SOCKETBSMALL:
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_NODEBARRIER_ATOMIC_SET:
      code_get_pointer(&ip);
      break;
    case OPCODE_NODEBARRIER_ATOMIC_WAIT:
      code_get_pointer(&ip);
      break;
    case OPCODE_REDUCE:
    case OPCODE_INVREDUCE:
      code_get_char(&ip);
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_ATTACHED:
      break;
    case OPCODE_MEMORY_FENCE:
      break;
    case OPCODE_MEMORY_FENCE_STORE:
      break;
    case OPCODE_MEMORY_FENCE_LOAD:
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      break;
    case OPCODE_GPUKERNEL:
      code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      p1 = p1 + (gpu_byte_code - (char*)NULL);
      ip -= sizeof(void *);
      code_put_pointer(&ip, p1, 0);
      code_get_int(&ip);
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute from disc %d\n", instruction);
      assert(0);
    }
  } while (instruction != OPCODE_RETURN);
  return ret;
}
