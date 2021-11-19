#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef MMAP
#include <sys/shm.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "allreduce.h"
#include "allreduce_groups.h"
#include "alltoall.h"
#include "backward_interpreter.h"
#include "buffer_offset.h"
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
#include "rank_permutation.h"
#include "raw_code.h"
#include "raw_code_merge.h"
#include "raw_code_tasks_node.h"
#include "raw_code_tasks_node_master.h"
#include "byte_code.h"
#include "read.h"
#include "reduce_copyin.h"
#include "reduce_copyout.h"
#include <mpi.h>
#ifdef GPU_ENABLED
#include "gpu_core.h"
#include "gpu_shmem.h"
#endif

#define NUM_BARRIERS 4

static int handle_code_max = 100;
static char **comm_code = NULL;
static char **execution_pointer = NULL;
static int *active_wait = NULL;

static int is_initialised = 0;
static MPI_Comm EXT_MPI_COMM_WORLD = MPI_COMM_NULL;
static int tag_max = 0;

/*static int global_min(int i, MPI_Comm comm_row, MPI_Comm comm_column) {
  MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &i, 1, MPI_INT, MPI_MIN, comm_column);
  }
  return (i);
}*/

#ifdef MMAP
static void gen_shared_name(MPI_Comm comm_node_row, MPI_Comm comm_node_column,
                            char *name) {
  int rank_global;
  char name_org[9000];
  MPI_Comm_rank(EXT_MPI_COMM_WORLD, &rank_global);
  MPI_Allreduce(MPI_IN_PLACE, &rank_global, 1, MPI_INT, MPI_MIN, comm_node_row);
  if (comm_node_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &rank_global, 1, MPI_INT, MPI_MIN,
                  comm_node_column);
  }
  strcpy(name_org, name);
  sprintf(name, "%s_%d", name_org, rank_global);
}
#endif

static void node_barrier_mpi(int handle, MPI_Comm shmem_comm_node_row,
                             MPI_Comm shmem_comm_node_column) {
  struct header_byte_code *header;
  if (handle >= 0) {
    header = (struct header_byte_code *)comm_code[handle];
    shmem_comm_node_row = header->comm_row;
    shmem_comm_node_column = header->comm_column;
  }
  if (shmem_comm_node_row != MPI_COMM_NULL) {
    MPI_Barrier(shmem_comm_node_row);
  }
  if (shmem_comm_node_column != MPI_COMM_NULL) {
    MPI_Barrier(shmem_comm_node_column);
    if (shmem_comm_node_row != MPI_COMM_NULL) {
      MPI_Barrier(shmem_comm_node_row);
    }
  }
}

static int destroy_shared_memory(int handle, int *size_shared, int *shmemid,
                                 char volatile **shmem) {
  if (*shmem != NULL) {
#ifndef MMAP
    node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL);
    shmdt((void *)*shmem);
    node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL);
    if (*shmemid != -1) {
      shmctl(*shmemid, IPC_RMID, NULL);
    }
    node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL);
#else
    munmap((void *)*shmem, *size_shared);
#endif
    *size_shared = 0;
    *shmem = NULL;
    *shmemid = -1;
  }
  return (0);
}

static int setup_shared_memory(MPI_Comm *shmem_comm_node_row,
                               MPI_Comm *shmem_comm_node_column,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int size_shared,
                               int *size_shared_old, int *shmemid,
                               char volatile **shmem, char fill, int numfill) {
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column, my_mpi_size_column;
#ifdef MMAP
  static int shmem_fd = -1;
  static char shmem_name[10000];
#endif
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  MPI_Comm_split(comm_row, my_mpi_rank_row / my_cores_per_node_row,
                 my_mpi_rank_row % my_cores_per_node_row, shmem_comm_node_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                   my_mpi_rank_column % my_cores_per_node_column,
                   shmem_comm_node_column);
  } else {
    *shmem_comm_node_column = MPI_COMM_NULL;
  }
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
#ifndef MMAP
  if ((my_mpi_rank_row % my_cores_per_node_row == 0) &&
      (my_mpi_rank_column % my_cores_per_node_column == 0)) {
    (*shmemid) = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0600);
  }
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  MPI_Bcast(shmemid, 1, MPI_INT, 0, *shmem_comm_node_row);
  if (*shmem_comm_node_column != MPI_COMM_NULL) {
    MPI_Bcast(shmemid, 1, MPI_INT, 0, *shmem_comm_node_column);
  }
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  (*shmem) = (char *)shmat(*shmemid, NULL, 0);
  if ((*shmem) == NULL)
    goto error;
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0))) {
    (*shmemid) = -1;
  } else {
    shmctl(*shmemid, IPC_RMID, NULL);
    memset((void *)*shmem, fill, numfill);
  }
  (*shmemid) = -1;
#else
  if (numfill > 0) {
    sprintf(shmem_name, "/ext_mpi");
  } else {
    sprintf(shmem_name, "/ext_mpi_");
  }
  gen_shared_name(shmem_comm_node_row, shmem_comm_node_column, shmem_name);
  if ((my_mpi_rank_row % my_cores_per_node_row == 0) &&
      (my_mpi_rank_column % my_cores_per_node_column == 0)) {
    shmem_fd = shm_open(shmem_name, O_RDWR | O_CREAT, 0600);
    if (shmem_fd == -1) {
      printf("not enough shared memory\n");
      exit(2);
    }
    if (ftruncate(shmem_fd, size_shared) != 0) {
      printf("not enough shared memory\n");
      exit(2);
    }
  }
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0))) {
    shmem_fd = shm_open(shmem_name, O_RDWR, 0600);
    if (shmem_fd == -1) {
      printf("not enough shared memory\n");
      exit(2);
    }
  }
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  *shmem = (char *)mmap(NULL, size_shared, PROT_READ | PROT_WRITE, MAP_SHARED,
                        shmem_fd, 0);
  if (shmem == MAP_FAILED) {
    printf("not enough shared memory\n");
    exit(2);
  }
  close(shmem_fd);
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  memset((void *)*shmem, fill, numfill);
  (*shmemid) = -1;
#endif
  node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column);
  *size_shared_old = size_shared;
  return 0;
error:
  return ERROR_SHMEM;
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
    if (MPI_Comm_split(comm_column,
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
    MPI_Comm_free(&my_comm_node);
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

static void node_barrier(char volatile *shmem, int *barrier_count,
                         int node_rank, int num_cores) {
  int barriers_size, step, barriers_size_max, i;
  for (barriers_size = 0, step = 1; step <= num_cores;
       barriers_size++, step <<= 1) {
    shmem[(*barrier_count + barriers_size * num_cores +
           (node_rank + step) % num_cores) *
          CACHE_LINE_SIZE] = 1;
    while (!shmem[(*barrier_count + barriers_size * num_cores + node_rank) *
                  CACHE_LINE_SIZE])
      ;
  }
  barriers_size_max = barriers_size;
  i = (barriers_size_max * num_cores * (NUM_BARRIERS - 1) + *barrier_count) %
      (barriers_size_max * num_cores * NUM_BARRIERS);
  for (barriers_size = 0; barriers_size < barriers_size_max; barriers_size++) {
    shmem[(i + barriers_size * num_cores + node_rank) * CACHE_LINE_SIZE] = 0;
  }
  *barrier_count += barriers_size * num_cores;
  if (*barrier_count >= barriers_size * num_cores * NUM_BARRIERS) {
    *barrier_count = 0;
  }
}

static int node_btest(char volatile *shmem, int *barrier_count, int node_rank,
                      int num_cores) {
  int barriers_size, step;
  for (barriers_size = 0, step = 1; step <= num_cores;
       barriers_size++, step <<= 1) {
    shmem[(*barrier_count + barriers_size * num_cores +
           (node_rank + step) % num_cores) *
          CACHE_LINE_SIZE] = 1;
    if (!shmem[(*barrier_count + barriers_size * num_cores + node_rank) *
               CACHE_LINE_SIZE]) {
      return (1);
    }
  }
  return (0);
}

static int exec_native(char *ip, char **ip_exec, int active_wait) {
  char instruction, instruction2; //, *r_start, *r_temp, *ipl;
  void volatile *p1, *p2;
  //  char *rlocmem=NULL;
  int i1, i2, i3; //, n_r, s_r, i;
  struct header_byte_code *header;
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
      MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, header->tag, EXT_MPI_COMM_WORLD,
                (MPI_Request *)code_get_pointer(&ip));
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      MPI_Isend((void *)p1, i1, MPI_CHAR, i2, header->tag, EXT_MPI_COMM_WORLD,
                (MPI_Request *)code_get_pointer(&ip));
      break;
    case OPCODE_MPIWAITALL:
      if (active_wait) {
        i1 = code_get_int(&ip);
        p1 = code_get_pointer(&ip);
        MPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
      } else {
        *ip_exec = ip - 1;
        i1 = code_get_int(&ip);
        p1 = code_get_pointer(&ip);
        MPI_Testall(i1, (MPI_Request *)p1, &i2, MPI_STATUSES_IGNORE);
        if (!i2) {
          return (0);
        } else {
          MPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
        }
      }
      break;
    case OPCODE_NODEBARRIER:
      if (active_wait) {
        node_barrier(header->barrier_shmem, &header->barrier_counter,
                     header->node_rank, header->num_cores);
      } else {
        *ip_exec = ip - 1;
        if (!node_btest(header->barrier_shmem, &header->barrier_counter,
                        header->node_rank, header->num_cores)) {
          return (0);
        } else {
          node_barrier(header->barrier_shmem, &header->barrier_counter,
                       header->node_rank, header->num_cores);
        }
      }
      break;
    case OPCODE_SETNUMCORES:
      header->num_cores = code_get_int(&ip);
      break;
    case OPCODE_SETNODERANK:
      header->node_rank = code_get_int(&ip);
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
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      p2 = code_get_pointer(&ip);
      i3 = code_get_int(&ip);
      MPI_Sendrecv((void *)p1, i1, MPI_CHAR, i2, 0, (void *)p2, i3, MPI_CHAR,
                   code_get_int(&ip), 0, EXT_MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      break;
    case OPCODE_MPISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      MPI_Send((void *)p1, i1, MPI_CHAR, code_get_int(&ip), 0,
               EXT_MPI_COMM_WORLD);
      break;
    case OPCODE_MPIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      MPI_Recv((void *)p1, i1, MPI_CHAR, code_get_int(&ip), 0,
               EXT_MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      break;
#ifdef GPU_ENABLED
    case OPCODE_GPUSYNCHRONIZE:
      gpu_synchronize();
      break;
    case OPCODE_GPUKERNEL:
      instruction2 = code_get_char(&ip);
      p1 = code_get_pointer(&ip);
      gpu_copy_reduce(instruction2, (void *)p1, code_get_int(&ip));
      break;
#endif
    default:
      printf("illegal MPI_OPCODE execute\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  *ip_exec = NULL;
  return (0);
}

int EXT_MPI_Exec_native(int handle) {
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
  return (0);
}

int EXT_MPI_Test_native(int handle) {
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
  active_wait[handle] = 1;
  if (execution_pointer[handle]) {
    exec_native(comm_code[handle], &execution_pointer[handle],
                active_wait[handle]);
  }
  active_wait[handle] = 0;
  return (0);
}

int EXT_MPI_Done_native(int handle) {
  char volatile *shmem;
  char *ip, *locmem;
  int shmem_size, shmemid, i;
  MPI_Comm shmem_comm_node_row, shmem_comm_node_column, shmem_comm_node_row2,
      shmem_comm_node_column2;
  struct header_byte_code *header;
  EXT_MPI_Wait_native(handle);
  ip = comm_code[handle];
  header = (struct header_byte_code *)ip;
  shmem = header->barrier_shmem;
  shmem_size = header->barrier_shmem_size;
  shmemid = header->barrier_shmemid;
  locmem = header->locmem;
  shmem_comm_node_row = header->comm_row;
  shmem_comm_node_column = header->comm_column;
  node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL);
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer((void *)header->shmem)) {
    gpu_destroy_shared_memory(header->comm_row, header->node_num_cores_row,
                              header->comm_column,
                              header->node_num_cores_column, &header->shmem);
  }
  gpu_free(header->gpu_byte_code);
#endif
  destroy_shared_memory(handle, &shmem_size, &shmemid, &shmem);
  node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL);
  free(locmem);
  free(comm_code[handle]);
  comm_code[handle] = NULL;
  ip = comm_code[handle + 1];
  if (ip) {
    header = (struct header_byte_code *)ip;
    shmem = header->barrier_shmem;
    shmem_size = header->barrier_shmem_size;
    shmemid = header->barrier_shmemid;
    locmem = header->locmem;
    shmem_comm_node_row2 = header->comm_row;
    shmem_comm_node_column2 = header->comm_column;
    node_barrier_mpi(handle + 1, MPI_COMM_NULL, MPI_COMM_NULL);
#ifdef GPU_ENABLED
    if (gpu_is_device_pointer((void *)header->shmem)) {
      gpu_destroy_shared_memory(header->comm_row, header->node_num_cores_row,
                                header->comm_column,
                                header->node_num_cores_column, &header->shmem);
    }
    gpu_free(header->gpu_byte_code);
#endif
    destroy_shared_memory(handle + 1, &shmem_size, &shmemid, &shmem);
    node_barrier_mpi(handle + 1, MPI_COMM_NULL, MPI_COMM_NULL);
    free(locmem);
    free(comm_code[handle + 1]);
    comm_code[handle + 1] = NULL;
  }
  for (i = 0; i < handle_code_max; i++) {
    if (comm_code[i] != NULL) {
      return (0);
    }
  }
  if (shmem_comm_node_row != MPI_COMM_NULL) {
    MPI_Comm_free(&shmem_comm_node_row);
  }
  if (shmem_comm_node_column != MPI_COMM_NULL) {
    MPI_Comm_free(&shmem_comm_node_column);
  }
  if (ip) {
    if (shmem_comm_node_row2 != MPI_COMM_NULL) {
      MPI_Comm_free(&shmem_comm_node_row2);
    }
    if (shmem_comm_node_column2 != MPI_COMM_NULL) {
      MPI_Comm_free(&shmem_comm_node_column2);
    }
  }
  free(active_wait);
  free(execution_pointer);
  free(comm_code);
  comm_code = NULL;
  execution_pointer = NULL;
  active_wait = 0;
  return 0;
}

static int init_epilogue(char *buffer_in, void *sendbuf, void *recvbuf,
                         int reduction_op, MPI_Comm comm_row,
                         int my_cores_per_node_row, MPI_Comm comm_column,
                         int my_cores_per_node_column, int alt) {
  int i, num_comm_max = -1, my_size_shared_buf = -1, barriers_size, step,
         nbuffer_in = 0, tag;
  char *ip;
  int handle, *global_ranks = NULL, code_size, my_mpi_size_row;
  int locmem_size, *locmem = NULL, shmem_size, shmemid;
  char volatile *shmem = NULL;
  char volatile *my_shared_buf;
  MPI_Comm shmem_comm_node_row, shmem_comm_node_column;
  int gpu_byte_code_counter = 0;
#ifdef GPU_ENABLED
  char *shmemid_gpu = NULL;
#endif
  struct parameters_block *parameters;
  handle = get_handle();
  nbuffer_in += i = read_parameters(buffer_in + nbuffer_in, &parameters);
  if (i < 0)
    goto error;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  num_comm_max = parameters->locmem_max;
  my_size_shared_buf = parameters->shmem_max;
  delete_parameters(parameters);
  locmem_size = num_comm_max * sizeof(MPI_Request);
  locmem = (int *)malloc(locmem_size);
  if (!locmem)
    goto error;
  shmem_size = -111;
  shmemid = -1;
  for (barriers_size = 0, step = 1;
       step <= my_cores_per_node_row * my_cores_per_node_column;
       barriers_size++, step *= 2)
    ;
  barriers_size *= my_cores_per_node_row * my_cores_per_node_column *
                   (NUM_BARRIERS + 1) * CACHE_LINE_SIZE;
  barriers_size += NUM_BARRIERS * CACHE_LINE_SIZE;
  if (setup_shared_memory(&shmem_comm_node_row, &shmem_comm_node_column,
                          comm_row, my_cores_per_node_row, comm_column,
                          my_cores_per_node_column,
                          my_size_shared_buf + barriers_size, &shmem_size,
                          &shmemid, &shmem, 0, barriers_size) < 0)
    goto error_shared;
  my_shared_buf = shmem + barriers_size;
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(sendbuf)) {
    my_shared_buf = NULL;
    shmemid_gpu = (char *)malloc(gpu_sizeof_memhandle());
    if (!shmemid_gpu)
      goto error;
    gpu_setup_shared_memory(shmem_comm_node_row, my_cores_per_node_row,
                            shmem_comm_node_column, my_cores_per_node_column,
                            shmem_size - barriers_size, shmemid_gpu,
                            &my_shared_buf);
  }
#endif
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  if (!global_ranks)
    goto error;
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(sendbuf)) {
    buffer_temp = (char *)malloc(strlen(buffer) + 100);
    if (!buffer_temp)
      goto error;
    buffer_temp_org = buffer_temp;
    sprintf(buffer_temp, " GPU_ENABLED 1\n%s", buffer);
  } else {
    buffer_temp = strdup(buffer);
    buffer_temp_org = buffer_temp;
  }
#endif
  if (comm_row != MPI_COMM_NULL) {
    tag = tag_max;
    MPI_Allreduce(MPI_IN_PLACE, &tag, 1, MPI_INT, MPI_MAX, comm_row);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Allreduce(MPI_IN_PLACE, &tag, 1, MPI_INT, MPI_MAX, comm_column);
    }
    tag_max = tag + 1;
  } else {
    tag = 0;
  }
  code_size = ext_mpi_generate_byte_code(
      shmem, shmem_size, shmemid, buffer_in, (char *)sendbuf, (char *)recvbuf,
      my_shared_buf, (char *)locmem, reduction_op, global_ranks, NULL,
      shmem_comm_node_row, my_cores_per_node_row, shmem_comm_node_column,
      my_cores_per_node_column, &gpu_byte_code_counter, tag);
  if (code_size < 0)
    goto error;
  ip = comm_code[handle] = (char *)malloc(code_size);
  if (!ip)
    goto error;
#ifdef GPU_ENABLED
  if (gpu_is_device_pointer(sendbuf)) {
    buffer_temp = (char *)malloc(strlen(buffer) + 100);
    if (!buffer_temp)
      goto error;
    buffer_temp_org = buffer_temp;
    sprintf(buffer_temp, " GPU_ENABLED 1\n%s", buffer);
  } else {
    buffer_temp = strdup(buffer);
    if (!buffer_temp)
      goto error;
    buffer_temp_org = buffer_temp;
  }
#endif
  if (ext_mpi_generate_byte_code(shmem, shmem_size, shmemid, buffer_in, (char *)sendbuf,
                                 (char *)recvbuf, my_shared_buf, (char *)locmem,
                                 reduction_op, global_ranks, ip, shmem_comm_node_row,
                                 my_cores_per_node_row, shmem_comm_node_column,
                                 my_cores_per_node_column, &gpu_byte_code_counter,
                                 tag) < 0)
    goto error;
  if (alt) {
    ip = comm_code[handle + 1] = (char *)malloc(code_size);
    if (!ip)
      goto error;
    if (setup_shared_memory(&shmem_comm_node_row, &shmem_comm_node_column,
                            comm_row, my_cores_per_node_row, comm_column,
                            my_cores_per_node_column,
                            my_size_shared_buf + barriers_size, &shmem_size,
                            &shmemid, &shmem, 0, barriers_size) < 0)
      goto error_shared;
    my_shared_buf = shmem + barriers_size;
    locmem = (int *)malloc(locmem_size);
    if (!locmem)
      goto error;
#ifdef GPU_ENABLED
    if (gpu_is_device_pointer(sendbuf)) {
      my_shared_buf = NULL;
      gpu_setup_shared_memory(shmem_comm_node_row, my_cores_per_node_row,
                              shmem_comm_node_column, my_cores_per_node_column,
                              shmem_size - barriers_size, shmemid_gpu,
                              &my_shared_buf);
      buffer_temp = (char *)malloc(strlen(buffer) + 100);
      if (!buffer_temp)
        goto error;
      buffer_temp_org = buffer_temp;
      sprintf(buffer_temp, " GPU_ENABLED 1\n%s", buffer);
    } else {
      buffer_temp = strdup(buffer);
      if (!buffer_temp)
        goto error;
      buffer_temp_org = buffer_temp;
    }
#endif
    if (ext_mpi_generate_byte_code(shmem, shmem_size, shmemid, buffer_in,
                                   (char *)sendbuf, (char *)recvbuf, my_shared_buf,
                                   (char *)locmem, reduction_op, global_ranks, ip,
                                   shmem_comm_node_row, my_cores_per_node_row,
                                   shmem_comm_node_column, my_cores_per_node_column,
                                   &gpu_byte_code_counter, tag) < 0)
      goto error;
  } else {
    comm_code[handle + 1] = NULL;
  }
  free(global_ranks);
#ifdef GPU_ENABLED
  free(shmemid_gpu);
#endif
  return handle;
error:
  destroy_shared_memory(handle, &shmem_size, &shmemid, &shmem);
  free(global_ranks);
#ifdef GPU_ENABLED
  free(shmemid_gpu);
#endif
  return ERROR_MALLOC;
error_shared:
  destroy_shared_memory(handle, &shmem_size, &shmemid, &shmem);
  free(global_ranks);
#ifdef GPU_ENABLED
  free(shmemid_gpu);
#endif
  return ERROR_SHMEM;
}

int EXT_MPI_Reduce_init_native(void *sendbuf, void *recvbuf, int count,
                               MPI_Datatype datatype, MPI_Op op, int root,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int *num_ports,
                               int *groups, int num_active_ports, int copyin,
                               int alt, int bit) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int coarse_count, *counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *buffer_temp;
  int nbuffer1 = 0, msize, *msizes = NULL, *msizes2 = NULL, *rank_perm = NULL,
      i, allreduce_short = (num_ports[0] < 0);
  int reduction_op;
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
    MPI_Allreduce(&count, &coarse_count, 1, MPI_INT, MPI_SUM, comm_column);
    MPI_Allgather(&count, 1, MPI_INT, counts, 1, MPI_INT, comm_column);
  } else {
    coarse_count = count;
    counts[0] = count;
  }
  msize = 0;
  for (i = 0; i < my_cores_per_node_column; i++) {
    msize += counts[i];
  }
  if (!allreduce_short) {
    msizes2 =
        (int *)malloc(sizeof(int) * my_mpi_size_row / my_cores_per_node_row);
    if (!msizes2)
      goto error;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes2[i] =
          (msize / type_size) / (my_mpi_size_row / my_cores_per_node_row);
    }
    for (i = 0;
         i < (msize / type_size) % (my_mpi_size_row / my_cores_per_node_row);
         i++) {
      msizes2[i]++;
    }
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes2[i] *= type_size;
    }
    rank_perm =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    if (!rank_perm)
      goto error;
    rank_perm_heuristic(my_mpi_size_row / my_cores_per_node_row, msizes2,
                        rank_perm);
    msizes =
        (int *)malloc(sizeof(int) * my_mpi_size_row / my_cores_per_node_row);
    if (!msizes)
      goto error;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      msizes[i] = msizes2[rank_perm[i]];
    }
    free(rank_perm);
    rank_perm = NULL;
    free(msizes2);
    msizes2 = NULL;
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
                        " PARAMETER COLLECTIVE_TYPE ALLREDUCE_GROUP\n");
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE %d\n", my_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_NODES %d\n",
                      my_mpi_size_row / my_cores_per_node_row);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER NODE_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPY_METHOD %d\n", copyin);
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS");
  i = 0;
  while (num_ports[i]) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", num_ports[i++]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER GROUPS");
  i = 0;
  while (groups[i]) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", groups[i++]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
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
  //  nbuffer1+=sprintf(buffer1+nbuffer1, " PARAMETER ASCII\n");
  free(msizes);
  msizes = NULL;
  if (!allreduce_short) {
    if (generate_allreduce_groups(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (generate_allreduce(buffer1, buffer2) < 0)
      goto error;
  }
  if ((root >= 0) || (root <= -10)) {
    if (root >= 0) {
      if (generate_backward_interpreter(buffer2, buffer1, comm_row) < 0)
        goto error;
    } else {
      if (generate_forward_interpreter(buffer2, buffer1, comm_row) < 0)
        goto error;
    }
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(sendbuf)) {
#endif
    if (generate_raw_code_tasks_node(buffer2, buffer1) < 0)
      goto error;
#ifdef GPU_ENABLED
  } else {
    if (generate_raw_code_tasks_node_master(buffer2, buffer1) < 0)
      goto error;
  }
#endif
  if (generate_reduce_copyin(buffer1, buffer2) < 0)
    goto error;
  if (generate_raw_code(buffer2, buffer1) < 0)
    goto error;
  if (generate_reduce_copyout(buffer1, buffer2) < 0)
    goto error;
  if (generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
#ifdef GPU_ENABLED
  if (!gpu_is_device_pointer(sendbuf)) {
#endif
    if (generate_parallel_memcpy(buffer2, buffer1) < 0)
      goto error;
    if (generate_raw_code_merge(buffer1, buffer2) < 0)
      goto error;
#ifdef GPU_ENABLED
  }
#endif
  if (alt) {
    if (generate_no_first_barrier(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (generate_dummy(buffer2, buffer1) < 0)
      goto error;
  }
  if (clean_barriers(buffer1, buffer2, comm_row, comm_column) < 0)
    goto error;
  iret = init_epilogue(buffer2, sendbuf, recvbuf, reduction_op, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt);
  free(buffer2);
  free(buffer1);
  return iret;
error:
  free(rank_perm);
  free(msizes);
  free(msizes2);
  free(counts);
  free(buffer2);
  free(buffer1);
  return ERROR_MALLOC;
}

int EXT_MPI_Allreduce_init_native(void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *groups, int num_active_ports, int copyin,
                                  int alt, int bit) {
  return (EXT_MPI_Reduce_init_native(
      sendbuf, recvbuf, count, datatype, op, -1, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, num_active_ports, copyin, alt, bit));
}

int EXT_MPI_Bcast_init_native(void *buffer, int count, MPI_Datatype datatype,
                              int root, MPI_Comm comm_row,
                              int my_cores_per_node_row, MPI_Comm comm_column,
                              int my_cores_per_node_column, int *num_ports,
                              int *groups, int num_active_ports, int copyin,
                              int alt) {
  return (EXT_MPI_Reduce_init_native(
      buffer, buffer, count, datatype, MPI_OP_NULL, -10 - root, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      groups, num_active_ports, copyin, alt, 0));
}

int EXT_MPI_Gatherv_init_native(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, int root,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int alt) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL, *buffer_temp;
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
    MPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                  my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                  comm_column);
    MPI_Allgather(&recvcounts[my_node * my_cores_per_node_row],
                  my_cores_per_node_row, MPI_INT, local_counts,
                  my_cores_per_node_row, MPI_INT, comm_column);
    MPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column);
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
      sprintf(buffer1 + nbuffer1, " PARAMETER NODE_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  if (root >= 0) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ROOT %d\n", root);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER COUNTS");
  for (i = 0; i < my_cores_per_node_column; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", global_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(global_counts);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER IOCOUNTS");
  for (j = 0; j < my_cores_per_node_row; j++) {
    for (i = 0; i < my_cores_per_node_column; i++) {
      nbuffer1 += sprintf(buffer1 + nbuffer1, " %d",
                          local_counts[i * my_cores_per_node_row + j]);
    }
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(local_counts);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS");
  i = 0;
  while (num_ports[i]) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", num_ports[i++]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER MESSAGE_SIZE");
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", coarse_counts[i]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
  free(coarse_counts);
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
  if (generate_rank_permutation_forward(buffer1, buffer2) < 0)
    goto error;
  if (generate_allreduce(buffer2, buffer1) < 0)
    goto error;
  if (generate_rank_permutation_backward(buffer1, buffer2) < 0)
    goto error;
  if (root >= 0) {
    if (generate_backward_interpreter(buffer2, buffer1, comm_row) < 0)
      goto error;
    buffer_temp = buffer1;
    buffer1 = buffer2;
    buffer2 = buffer_temp;
  }
  if (generate_raw_code_tasks_node(buffer2, buffer1) < 0)
    goto error;
  if (generate_reduce_copyin(buffer1, buffer2) < 0)
    goto error;
  if (generate_raw_code(buffer2, buffer1) < 0)
    goto error;
  if (generate_reduce_copyout(buffer1, buffer2) < 0)
    goto error;
  if (generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
  if (generate_parallel_memcpy(buffer2, buffer1) < 0)
    goto error;
  if (generate_raw_code_merge(buffer1, buffer2) < 0)
    goto error;
  if (alt) {
    if (generate_no_first_barrier(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (generate_dummy(buffer2, buffer1) < 0)
      goto error;
  }
  if (clean_barriers(buffer1, buffer2, comm_row, comm_column) < 0)
    goto error;
  iret = init_epilogue(buffer2, sendbuf, recvbuf, -1, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt);
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
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int alt) {
  return (EXT_MPI_Gatherv_init_native(
      sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, -1,
      comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
      num_ports, num_parallel, num_active_ports, alt));
}

int EXT_MPI_Scatterv_init_native(void *sendbuf, int *sendcounts, int *displs,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype, int root,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int *num_ports,
                                 int *num_parallel, int num_active_ports,
                                 int copyin, int alt) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL;
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
    MPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                  my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                  comm_column);
    MPI_Allgather(&sendcounts[my_node * my_cores_per_node_row],
                  my_cores_per_node_row, MPI_INT, local_counts,
                  my_cores_per_node_row, MPI_INT, comm_column);
    MPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column);
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
      sprintf(buffer1 + nbuffer1, " PARAMETER NODE_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ROOT %d\n", root);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPY_METHOD %d\n", copyin);
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS");
  i = 0;
  while (num_ports[i]) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", num_ports[i++]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
  if (generate_rank_permutation_forward(buffer1, buffer2) < 0)
    goto error;
  if (generate_allreduce(buffer2, buffer1) < 0)
    goto error;
  if (generate_rank_permutation_backward(buffer1, buffer2) < 0)
    goto error;
  if (generate_forward_interpreter(buffer2, buffer1, comm_row) < 0)
    goto error;
  if (generate_raw_code_tasks_node(buffer1, buffer2) < 0)
    goto error;
  if (generate_reduce_copyin(buffer2, buffer1) < 0)
    goto error;
  if (generate_raw_code(buffer1, buffer2) < 0)
    goto error;
  if (generate_reduce_copyout(buffer2, buffer1) < 0)
    goto error;
  if (generate_buffer_offset(buffer1, buffer2) < 0)
    goto error;
  if (generate_no_offset(buffer2, buffer1) < 0)
    goto error;
  if (generate_optimise_buffers(buffer1, buffer2) < 0)
    goto error;
  if (generate_optimise_buffers2(buffer2, buffer1) < 0)
    goto error;
  if (generate_parallel_memcpy(buffer1, buffer2) < 0)
    goto error;
  if (generate_raw_code_merge(buffer2, buffer1) < 0)
    goto error;
  if (alt) {
    if (generate_no_first_barrier(buffer1, buffer2) < 0)
      goto error;
  } else {
    if (generate_dummy(buffer1, buffer2) < 0)
      goto error;
  }
  if (clean_barriers(buffer2, buffer1, comm_row, comm_column) < 0)
    goto error;
  iret = init_epilogue(buffer1, sendbuf, recvbuf, -1, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt);
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
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *num_ports,
    int *num_parallel, int num_active_ports, int copyin, int alt) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int *coarse_counts = NULL, *local_counts = NULL, *global_counts = NULL, iret;
  char *buffer1 = NULL, *buffer2 = NULL;
  int nbuffer1 = 0, i, j;
  int reduction_op;
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
    MPI_Allreduce(MPI_IN_PLACE, coarse_counts,
                  my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                  comm_column);
    MPI_Allgather(&recvcounts[my_node * my_cores_per_node_row],
                  my_cores_per_node_row, MPI_INT, local_counts,
                  my_cores_per_node_row, MPI_INT, comm_column);
    MPI_Allgather(&j, 1, MPI_INT, global_counts, 1, MPI_INT, comm_column);
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
      sprintf(buffer1 + nbuffer1, " PARAMETER NODE_RANK %d\n", my_lrank_node);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_ROW_SIZE %d\n",
                      my_cores_per_node_row);
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NODE_COLUMN_SIZE %d\n",
                      my_cores_per_node_column);
  nbuffer1 +=
      sprintf(buffer1 + nbuffer1, " PARAMETER COPY_METHOD %d\n", copyin);
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER NUM_PORTS");
  i = 0;
  while (num_ports[i]) {
    nbuffer1 += sprintf(buffer1 + nbuffer1, " %d", num_ports[i++]);
  }
  nbuffer1 += sprintf(buffer1 + nbuffer1, "\n");
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
  nbuffer1 += sprintf(buffer1 + nbuffer1, " PARAMETER ASCII\n");
  if (generate_rank_permutation_forward(buffer1, buffer2) < 0)
    goto error;
  if (generate_allreduce(buffer2, buffer1) < 0)
    goto error;
  if (generate_rank_permutation_backward(buffer1, buffer2) < 0)
    goto error;
  if (generate_raw_code_tasks_node(buffer2, buffer1) < 0)
    goto error;
  if (generate_reduce_copyin(buffer1, buffer2) < 0)
    goto error;
  if (generate_raw_code(buffer2, buffer1) < 0)
    goto error;
  if (generate_reduce_copyout(buffer1, buffer2) < 0)
    goto error;
  if (generate_buffer_offset(buffer2, buffer1) < 0)
    goto error;
  if (generate_no_offset(buffer1, buffer2) < 0)
    goto error;
  if (generate_optimise_buffers(buffer2, buffer1) < 0)
    goto error;
  if (generate_optimise_buffers2(buffer1, buffer2) < 0)
    goto error;
  if (generate_parallel_memcpy(buffer2, buffer1) < 0)
    goto error;
  if (generate_raw_code_merge(buffer1, buffer2) < 0)
    goto error;
  if (alt) {
    if (generate_no_first_barrier(buffer2, buffer1) < 0)
      goto error;
  } else {
    if (generate_dummy(buffer2, buffer1) < 0)
      goto error;
  }
  if (clean_barriers(buffer1, buffer2, comm_row, comm_column) < 0)
    goto error;
  iret = init_epilogue(buffer2, sendbuf, recvbuf, reduction_op, comm_row,
                       my_cores_per_node_row, comm_column,
                       my_cores_per_node_column, alt);
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
  MPI_Comm_dup(MPI_COMM_WORLD, &EXT_MPI_COMM_WORLD);
  is_initialised = 1;
  return 0;
}

int EXT_MPI_Initialized_native() { return (is_initialised); }

int EXT_MPI_Finalize_native() {
  MPI_Comm_free(&EXT_MPI_COMM_WORLD);
  return 0;
}
