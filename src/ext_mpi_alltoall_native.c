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
#include "ext_mpi_alltoall_native.h"
#include <mpi.h>

#define NUM_BARRIERS 4

#define OPCODE_RETURN 0
#define OPCODE_MEMCPY 1
#define OPCODE_MPIIRECV 2
#define OPCODE_MPIISEND 3
#define OPCODE_MPIWAITALL 4
#define OPCODE_NODEBARRIER 5
#define OPCODE_SETNUMCORES 6
#define OPCODE_REDUCE 7
#define OPCODE_MPISENDRECV 8
#define OPCODE_MPISEND 9
#define OPCODE_MPIRECV 10
#define OPCODE_LOCALMEM 11

#define OPCODE_REDUCE_SUM_DOUBLE 0
#define OPCODE_REDUCE_SUM_LONG_INT 1

#define EXEC_RECURSIVE 0
#define EXEC_BRUCK 1

#define HANDLE_CODE_MAX 10000

static int shmemid = -1;
static char volatile *shmem = NULL;
static int shmem_size = 0;
static MPI_Comm shmem_comm_node_row = MPI_COMM_NULL;
static MPI_Comm shmem_comm_node_column = MPI_COMM_NULL;

static char *locmem = NULL;
static int locmem_size = 0;

static int handle_code_max = 10;
static char **comm_code = NULL;

static int handle_comm_max = 10;
static char **comm_comm = NULL;

static int barrier_count = 0;

void EXT_MPI_Get_variables_native(struct EXT_MPI_Variables_native *var) {
  var->shmemid = shmemid;
  var->shmem = (char *)shmem;
  var->shmem_size = shmem_size;
  var->shmem_comm_node_row = shmem_comm_node_row;
  var->shmem_comm_node_column = shmem_comm_node_column;
  var->locmem = locmem;
  var->locmem_size = locmem_size;
  var->handle_code_max = handle_code_max;
  var->barrier_count = barrier_count;
  var->comm_code = comm_code;
}

void EXT_MPI_Set_variables_native(struct EXT_MPI_Variables_native *var) {
  shmemid = var->shmemid;
  shmem = (char *)var->shmem;
  shmem_size = var->shmem_size;
  shmem_comm_node_row = var->shmem_comm_node_row;
  shmem_comm_node_column = var->shmem_comm_node_column;
  locmem = var->locmem;
  locmem_size = var->locmem_size;
  handle_code_max = var->handle_code_max;
  barrier_count = var->barrier_count;
  comm_code = var->comm_code;
}

static void code_put_char(char **code, char c, int isdryrun) {
  if (!isdryrun)
    *((char *)(*code)) = c;
  *code += sizeof(char);
}

static void code_put_int(char **code, int i, int isdryrun) {
  if (!isdryrun)
    *((int *)(*code)) = i;
  *code += sizeof(int);
}

static void code_put_pointer(char **code, void *p, int isdryrun) {
  if (!isdryrun)
    *((void **)(*code)) = p;
  *code += sizeof(void *);
}

static char code_get_char(char **code) {
  char c;
  c = *((char *)(*code));
  *code += sizeof(char);
  return c;
}

static int code_get_int(char **code) {
  int i;
  i = *((int *)(*code));
  *code += sizeof(int);
  return i;
}

static void *code_get_pointer(char **code) {
  void *p;
  p = *((void **)(*code));
  *code += sizeof(void *);
  return p;
}

#ifdef MMAP
static void gen_shared_name(MPI_Comm comm_node_row, MPI_Comm comm_node_column,
                            char *name) {
  int rank_global;
  char name_org[9000];
  MPI_Comm_rank(MPI_COMM_WORLD, &rank_global);
  MPI_Allreduce(MPI_IN_PLACE, &rank_global, 1, MPI_INT, MPI_MIN, comm_node_row);
  if (comm_node_column != MPI_COMM_NULL) {
    MPI_Allreduce(MPI_IN_PLACE, &rank_global, 1, MPI_INT, MPI_MIN,
                  comm_node_column);
  }
  strcpy(name_org, name);
  sprintf(name, "%s_%d", name_org, rank_global);
}
#endif

static void node_barrier_mpi() {
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

static int destroy_shared_memory(int *size_shared, int *shmemid,
                                 char volatile **shmem) {
  if (*shmem != NULL) {
#ifndef MMAP
    node_barrier_mpi();
    shmdt((void *)*shmem);
    node_barrier_mpi();
    if (*shmemid != -1) {
      shmctl(*shmemid, IPC_RMID, NULL);
    }
    node_barrier_mpi();
#else
    munmap((void *)*shmem, *size_shared);
#endif
    *size_shared = 0;
    *shmem = NULL;
    *shmemid = -1;
  }
  return (0);
}

static int setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row,
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
  if (shmem_comm_node_row == MPI_COMM_NULL) {
    MPI_Comm_split(comm_row, my_mpi_rank_row / my_cores_per_node_row,
                   my_mpi_rank_row % my_cores_per_node_row,
                   &shmem_comm_node_row);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                     my_mpi_rank_column % my_cores_per_node_column,
                     &shmem_comm_node_column);
    } else {
      shmem_comm_node_column = MPI_COMM_NULL;
    }
  }
  node_barrier_mpi();
  if (*size_shared_old < size_shared) {
    *size_shared_old = size_shared;
    destroy_shared_memory(size_shared_old, shmemid, shmem);
  }
  if ((*shmem) != NULL) {
    return 1;
  }
#ifndef MMAP
  if ((my_mpi_rank_row % my_cores_per_node_row == 0) &&
      (my_mpi_rank_column % my_cores_per_node_column == 0)) {
    (*shmemid) = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0600);
  }
  node_barrier_mpi();
  MPI_Bcast(shmemid, 1, MPI_INT, 0, shmem_comm_node_row);
  if (shmem_comm_node_column != MPI_COMM_NULL) {
    MPI_Bcast(shmemid, 1, MPI_INT, 0, shmem_comm_node_column);
  }
  node_barrier_mpi();
  (*shmem) = (char *)shmat(*shmemid, NULL, 0);
  if ((*shmem) == NULL)
    exit(2);
  node_barrier_mpi();
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
  node_barrier_mpi();
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0))) {
    shmem_fd = shm_open(shmem_name, O_RDWR, 0600);
    if (shmem_fd == -1) {
      printf("not enough shared memory\n");
      exit(2);
    }
  }
  node_barrier_mpi();
  *shmem = (char *)mmap(NULL, size_shared, PROT_READ | PROT_WRITE, MAP_SHARED,
                        shmem_fd, 0);
  if (shmem == MAP_FAILED) {
    printf("not enough shared memory\n");
    exit(2);
  }
  close(shmem_fd);
  node_barrier_mpi();
  memset((void *)*shmem, fill, numfill);
  (*shmemid) = -1;
#endif
  node_barrier_mpi();
  *size_shared_old = size_shared;
  return 0;
}

static int rebase_address(char **ip, char *shmem_old, int shmem_size_old,
                          char *shmem) {
  if ((*((char **)(*ip)) >= shmem_old) &&
      (*((char **)(*ip)) - shmem_old < shmem_size_old)) {
    code_put_pointer(ip, shmem + (*((char **)(*ip)) - shmem_old), 0);
    return (1);
  } else {
    code_get_pointer(ip);
    return (0);
  }
}

static int rebase_addresses(char *shmem_old, int shmem_size_old, char *shmem) {
  char instruction, *ip, *r_start;
  int handle, *rlocmem = NULL, n_r, i;
  for (handle = 0; handle < handle_code_max; handle++) {
    ip = comm_code[handle];
    if (ip != NULL) {
      do {
        instruction = code_get_char(&ip);
        switch (instruction) {
        case OPCODE_RETURN:
          if (rlocmem != NULL) {
            for (i = 0; i < n_r; i++) {
              code_put_int(&r_start, rlocmem[i], 0);
            }
            free(rlocmem);
          }
          break;
        case OPCODE_LOCALMEM:
          code_get_int(&ip);
          n_r = code_get_int(&ip);
          *r_start = *ip;
          rlocmem = (int *)malloc(n_r * sizeof(int));
          for (i = 0; i < n_r; i++) {
            rlocmem[i] = code_get_int(&ip);
          }
          break;
        case OPCODE_MEMCPY:
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          break;
        case OPCODE_MPIIRECV:
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          code_get_int(&ip);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          break;
        case OPCODE_MPIISEND:
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          code_get_int(&ip);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          break;
        case OPCODE_MPIWAITALL:
          code_get_int(&ip);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          break;
        case OPCODE_NODEBARRIER:
          break;
        case OPCODE_SETNUMCORES:
          code_get_int(&ip);
          break;
        case OPCODE_REDUCE:
          code_get_char(&ip);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          break;
        case OPCODE_MPISENDRECV:
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          code_get_int(&ip);
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          code_get_int(&ip);
          break;
        case OPCODE_MPISEND:
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          code_get_int(&ip);
          break;
        case OPCODE_MPIRECV:
          rebase_address(&ip, shmem_old, shmem_size_old, shmem);
          code_get_int(&ip);
          code_get_int(&ip);
          break;
        default:
          printf("illegal MPI_OPCODE\n");
          exit(1);
        }
      } while (instruction != OPCODE_RETURN);
    }
  }
  return (0);
}

static void setup_rank_translation(MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column,
                                   int *global_ranks) {
  MPI_Comm my_comm_node;
  int my_mpi_size_row, grank, my_mpi_size_column, my_mpi_rank_column,
      *lglobal_ranks;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
    MPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                   my_mpi_rank_column % my_cores_per_node_column,
                   &my_comm_node);
    MPI_Comm_rank(MPI_COMM_WORLD, &grank);
    lglobal_ranks = (int *)malloc(sizeof(int) * my_cores_per_node_column);
    MPI_Gather(&grank, 1, MPI_INT, lglobal_ranks, 1, MPI_INT, 0, my_comm_node);
    MPI_Bcast(lglobal_ranks, my_cores_per_node_column, MPI_INT, 0,
              my_comm_node);
    MPI_Barrier(my_comm_node);
    MPI_Comm_free(&my_comm_node);
    MPI_Gather(lglobal_ranks, my_cores_per_node_column, MPI_INT, global_ranks,
               my_cores_per_node_column, MPI_INT, 0, comm_row);
    free(lglobal_ranks);
  } else {
    MPI_Comm_rank(MPI_COMM_WORLD, &grank);
    MPI_Gather(&grank, 1, MPI_INT, global_ranks, 1, MPI_INT, 0, comm_row);
  }
  MPI_Bcast(global_ranks, my_mpi_size_row * my_cores_per_node_column, MPI_INT,
            0, comm_row);
}

static void compute_offsets(int my_num_nodes, int num_ports, int gbstep,
                            int port, int *offset, int *size) {
  int my_indices[my_num_nodes], i;
  for (i = 0; i < my_num_nodes; i++) {
    my_indices[i] = (i / gbstep) % (num_ports + 1);
  }
  (*offset) = (*size) = 0;
  for (i = 0; i < my_num_nodes; i++) {
    if (my_indices[i] < port + 1)
      (*offset)++;
    if (my_indices[i] == port + 1)
      (*size)++;
  }
}

static int get_handle(char ***comm, int *handle_max) {
  char **handles_old;
  int handle, i;
  if (*comm == NULL) {
    *comm = (char **)malloc(sizeof(char *) * (*handle_max));
    for (i = 0; i < (*handle_max); i++) {
      (*comm)[i] = NULL;
    }
  }
  handle = 0;
  while (((*comm)[handle] != NULL) && (handle < *handle_max - 1)) {
    handle++;
  }
  if (handle >= *handle_max - 1) {
    if ((*comm)[handle] != NULL) {
      handles_old = *comm;
      *handle_max *= 2;
      *comm = (char **)malloc(sizeof(char *) * (*handle_max));
      for (i = 0; i < *handle_max; i++) {
        (*comm)[i] = NULL;
      }
      for (i = 0; i < *handle_max / 2; i++) {
        (*comm)[i] = handles_old[i];
      }
      free(handles_old);
      handle++;
    }
  }
  return (handle);
}

static int local_alltoall_init(void *sendbuf, int sendcount,
                               MPI_Datatype sendtype, void *recvbuf,
                               int recvcount, MPI_Datatype recvtype,
                               MPI_Comm comm_row, int my_cores_per_node_row,
                               MPI_Comm comm_column,
                               int my_cores_per_node_column, int num_ports,
                               int num_active_ports, int chunks_throttle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
      my_mpi_size_global, my_mpi_rank_global;
  int dsize, gbstep, handle, isdryrun, num_comm, num_comm_max;
  char volatile *my_shared_sendbuf, *my_shared_recvbuf, *my_shared_middbuf,
      *ptemp;
  char *ip, *shmem_old, *locmem_old;
  int *global_ranks, i, j, port, shmem_size_old, locmem_size_old;
  int num_comm_throttle, i_throttle;
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
  MPI_Type_size(sendtype, &type_size);
  dsize = type_size * sendcount;
  handle = get_handle(&comm_code, &handle_code_max);
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  if (!setup_shared_memory(comm_row, my_cores_per_node_row, comm_column,
                           my_cores_per_node_column,
                           dsize * my_cores_per_node_row * my_mpi_size_row *
                                   my_cores_per_node_column * 3 +
                               NUM_BARRIERS,
                           &shmem_size, &shmemid, &shmem, 0, NUM_BARRIERS)) {
    if (shmem_old != NULL) {
      rebase_addresses(shmem_old, shmem_size_old, (char *)shmem);
    }
  }
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
  my_shared_sendbuf = shmem + NUM_BARRIERS;
  my_shared_recvbuf = shmem + NUM_BARRIERS +
                      dsize * my_cores_per_node_row * my_mpi_size_row *
                          my_cores_per_node_column;
  my_shared_middbuf = shmem + NUM_BARRIERS +
                      dsize * my_cores_per_node_row * my_mpi_size_row *
                          my_cores_per_node_column * 2;
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
  my_mpi_size_global = my_mpi_size_row * my_cores_per_node_column;
  my_mpi_rank_global = my_mpi_rank_row * my_cores_per_node_column +
                       my_mpi_rank_column % my_cores_per_node_column;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    if (isdryrun) {
      ip = NULL;
    } else {
      if ((int)(num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status))) >
          locmem_size) {
        locmem_old = locmem;
        locmem_size_old = locmem_size;
        locmem_size = num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status));
        locmem = (char *)malloc(sizeof(char) * locmem_size);
        if (locmem_old != NULL) {
          rebase_addresses(locmem_old, locmem_size_old, (char *)locmem);
          free(locmem_old);
        }
      }
      ip = comm_code[handle] = (char *)malloc(sizeof(char *) * ((size_t)(ip)));
    }
    code_put_char(&ip, OPCODE_SETNUMCORES, isdryrun);
    code_put_int(&ip, my_cores_per_node_row * my_cores_per_node_column,
                 isdryrun);
    num_comm_max = 0;
    num_comm = 0;
    if (my_mpi_size_row <= my_cores_per_node_row) {
      for (i = 0; i < my_cores_per_node_row; i++) {
        if (i != my_mpi_rank_row) {
          code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
          code_put_pointer(
              &ip,
              (void *)(my_shared_sendbuf +
                       ((i + my_lrank_row * my_cores_per_node_row) *
                            my_cores_per_node_column +
                        my_lrank_column) *
                           dsize),
              isdryrun);
          code_put_pointer(&ip, (void *)(((char *)sendbuf) + i * dsize),
                           isdryrun);
          code_put_int(&ip, dsize, isdryrun);
        }
      }
      code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
      code_put_pointer(
          &ip, (void *)(((char *)recvbuf) + my_mpi_rank_row * dsize), isdryrun);
      code_put_pointer(
          &ip, (void *)(((char *)sendbuf) + my_mpi_rank_row * dsize), isdryrun);
      code_put_int(&ip, dsize, isdryrun);
      code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
      for (i = 0; i < my_cores_per_node_row; i++) {
        if (i != my_lrank_row) {
          code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
          code_put_pointer(
              &ip,
              (void *)(((char *)recvbuf) +
                       (my_node * my_cores_per_node_row + i) * dsize),
              isdryrun);
          code_put_pointer(
              &ip,
              (void *)(my_shared_sendbuf +
                       ((i * my_cores_per_node_row + my_lrank_row) *
                            my_cores_per_node_column +
                        my_lrank_column) *
                           dsize),
              isdryrun);
          code_put_int(&ip, dsize, isdryrun);
        }
      }
    } else {
      int locations[my_mpi_size_row / my_cores_per_node_row],
          locations2[my_mpi_size_row / my_cores_per_node_row],
          counts[num_ports + 1], add, isize;
      for (gbstep = 1; gbstep < my_mpi_size_row / my_cores_per_node_row;
           gbstep *= (num_ports + 1)) {
        for (i_throttle = 0; i_throttle < chunks_throttle; i_throttle++) {
          num_comm = 0;
          num_comm_throttle = 0;
          if (my_lrank_node < num_active_ports) {
            for (port = my_lrank_node; port < num_ports;
                 port += num_active_ports) {
              num_comm_throttle++;
              compute_offsets(my_mpi_size_row / my_cores_per_node_row,
                              num_ports, gbstep, port, &add, &isize);
              if ((num_comm_throttle - 1) % chunks_throttle == i_throttle) {
                if (isize > 0) {
                  code_put_char(&ip, OPCODE_MPIIRECV, isdryrun);
                  code_put_pointer(
                      &ip,
                      (void *)(((char *)my_shared_recvbuf) +
                               add * dsize * my_cores_per_node_row *
                                   my_cores_per_node_row *
                                   my_cores_per_node_column),
                      isdryrun);
                  code_put_int(&ip,
                               isize * dsize * my_cores_per_node_row *
                                   my_cores_per_node_row *
                                   my_cores_per_node_column,
                               isdryrun);
                  code_put_int(
                      &ip,
                      global_ranks[(my_mpi_rank_global + my_mpi_size_global -
                                    (port + 1) * gbstep *
                                        my_cores_per_node_row *
                                        my_cores_per_node_column) %
                                   my_mpi_size_global],
                      isdryrun);
                  code_put_pointer(
                      &ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                      isdryrun);
                  num_comm++;
                }
              }
            }
          }
          if (i_throttle == 0) {
            for (i = 0; i < num_ports + 1; i++) {
              counts[i] = 0;
            }
            if (gbstep == 1) {
              for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++) {
                compute_offsets(my_mpi_size_row / my_cores_per_node_row,
                                num_ports, gbstep, j % (num_ports + 1) - 1,
                                &add, &isize);
                locations[j] = add + counts[j % (num_ports + 1)]++;
              }
              for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++) {
                int jjj =
                    locations[(j + my_mpi_size_row / my_cores_per_node_row -
                               my_node) %
                              (my_mpi_size_row / my_cores_per_node_row)];
                for (i = 0; i < my_cores_per_node_row; i++) {
                  if (i + j * my_cores_per_node_row != my_mpi_rank_row) {
                    code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
                    code_put_pointer(
                        &ip,
                        (void *)(my_shared_sendbuf +
                                 ((i + (jjj * my_cores_per_node_row +
                                        my_lrank_row) *
                                           my_cores_per_node_row) *
                                      my_cores_per_node_column +
                                  my_lrank_column) *
                                     dsize),
                        isdryrun);
                    code_put_pointer(
                        &ip,
                        (void *)(((char *)sendbuf) +
                                 (i + j * my_cores_per_node_row) * dsize),
                        isdryrun);
                    code_put_int(&ip, dsize, isdryrun);
                  }
                }
              }
            } else {
              for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++) {
                compute_offsets(
                    my_mpi_size_row / my_cores_per_node_row, num_ports, gbstep,
                    (j / gbstep) % (num_ports + 1) - 1, &add, &isize);
                int jjj = add + counts[(j / gbstep) % (num_ports + 1)]++;
                locations2[j] = jjj;
              }
              for (j = my_lrank_node + 1;
                   j < my_mpi_size_row /
                           (my_cores_per_node_row * my_cores_per_node_column);
                   j += my_cores_per_node_row * my_cores_per_node_column) {
                code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
                code_put_pointer(
                    &ip,
                    (void *)(my_shared_sendbuf +
                             locations2[j] * my_cores_per_node_row *
                                 my_cores_per_node_row *
                                 my_cores_per_node_column * dsize),
                    isdryrun);
                code_put_pointer(&ip,
                                 (void *)(my_shared_middbuf +
                                          locations[j] * my_cores_per_node_row *
                                              my_cores_per_node_row *
                                              my_cores_per_node_column * dsize),
                                 isdryrun);
                code_put_int(&ip,
                             dsize * my_cores_per_node_row *
                                 my_cores_per_node_row *
                                 my_cores_per_node_column,
                             isdryrun);
              }
              for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++) {
                locations[j] = locations2[j];
              }
            }
            code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
          }

          num_comm_throttle = 0;
          if (my_lrank_node < num_active_ports) {
            for (port = my_lrank_node; port < num_ports;
                 port += num_active_ports) {
              num_comm_throttle++;
              compute_offsets(my_mpi_size_row / my_cores_per_node_row,
                              num_ports, gbstep, port, &add, &isize);
              if ((num_comm_throttle - 1) % chunks_throttle == i_throttle) {
                if (isize > 0) {
                  code_put_char(&ip, OPCODE_MPIISEND, isdryrun);
                  code_put_pointer(
                      &ip,
                      (void *)(((char *)my_shared_sendbuf) +
                               add * dsize * my_cores_per_node_row *
                                   my_cores_per_node_row *
                                   my_cores_per_node_column),
                      isdryrun);
                  code_put_int(&ip,
                               isize * dsize * my_cores_per_node_row *
                                   my_cores_per_node_row *
                                   my_cores_per_node_column,
                               isdryrun);
                  code_put_int(&ip,
                               global_ranks[(my_mpi_rank_global +
                                             (port + 1) * gbstep *
                                                 my_cores_per_node_row *
                                                 my_cores_per_node_column) %
                                            my_mpi_size_global],
                               isdryrun);
                  code_put_pointer(
                      &ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                      isdryrun);
                  num_comm++;
                }
              }
            }
          }

          if (i_throttle == 0) {
            if (gbstep == 1) {
              code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
              code_put_pointer(
                  &ip, (void *)(((char *)recvbuf) + my_mpi_rank_row * dsize),
                  isdryrun);
              code_put_pointer(
                  &ip, (void *)(((char *)sendbuf) + my_mpi_rank_row * dsize),
                  isdryrun);
              code_put_int(&ip, dsize, isdryrun);
              for (i = 0; i < my_cores_per_node_row; i++) {
                if (i != my_lrank_row) {
                  code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
                  code_put_pointer(
                      &ip,
                      (void *)(((char *)recvbuf) +
                               (my_node * my_cores_per_node_row + i) * dsize),
                      isdryrun);
                  code_put_pointer(
                      &ip,
                      (void *)(((char *)my_shared_sendbuf) +
                               ((i * my_cores_per_node_row + my_lrank_row) *
                                    my_cores_per_node_column +
                                my_lrank_column) *
                                   dsize),
                      isdryrun);
                  code_put_int(&ip, dsize, isdryrun);
                }
              }
            }
          }
          if (num_comm > 0) {
            code_put_char(&ip, OPCODE_MPIWAITALL, isdryrun);
            code_put_int(&ip, num_comm, isdryrun);
            code_put_pointer(&ip, (void *)locmem, isdryrun);
          }
          if (num_comm > num_comm_max) {
            num_comm_max = num_comm;
          }
          num_comm = 0;
        }
        code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
        compute_offsets(my_mpi_size_row / my_cores_per_node_row, num_ports,
                        gbstep, -1, &add, &isize);
        for (j = my_lrank_node + 1; j < isize;
             j += my_cores_per_node_row * my_cores_per_node_column) {
          code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
          code_put_pointer(
              &ip,
              (void *)(my_shared_recvbuf +
                       j * my_cores_per_node_row * my_cores_per_node_row *
                           my_cores_per_node_column * dsize),
              isdryrun);
          code_put_pointer(
              &ip,
              (void *)(my_shared_sendbuf +
                       j * my_cores_per_node_row * my_cores_per_node_row *
                           my_cores_per_node_column * dsize),
              isdryrun);
          code_put_int(&ip,
                       dsize * my_cores_per_node_row * my_cores_per_node_row *
                           my_cores_per_node_column,
                       isdryrun);
        }

        if (gbstep * (num_ports + 1) >=
            my_mpi_size_row / my_cores_per_node_row) {
          for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++) {
            int jjj = (2 * my_mpi_size_row / my_cores_per_node_row -
                       locations[j] + my_node) %
                      (my_mpi_size_row / my_cores_per_node_row);
            if (locations[j] != my_node) {
              for (i = 0; i < my_cores_per_node_row; i++) {
                code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
                code_put_pointer(
                    &ip,
                    (void *)(((char *)recvbuf) +
                             (i + j * my_cores_per_node_row) * dsize),
                    isdryrun);
                code_put_pointer(&ip,
                                 (void *)(((char *)my_shared_recvbuf) +
                                          (((i + jjj * my_cores_per_node_row) *
                                                my_cores_per_node_row +
                                            my_lrank_row) *
                                               my_cores_per_node_column +
                                           my_lrank_column) *
                                              dsize),
                                 isdryrun);
                code_put_int(&ip, dsize, isdryrun);
              }
            }
          }
        }

        ptemp = my_shared_recvbuf;
        my_shared_recvbuf = my_shared_middbuf;
        my_shared_middbuf = ptemp;
      }
    }
    code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
    code_put_char(&ip, OPCODE_RETURN, isdryrun);
  }
  free(global_ranks);
  return (handle);
}

static int local_alltoallv_init_noshm(void *sendbuf, int *sendcounts,
                                      int *sdispls, MPI_Datatype sendtype,
                                      void *recvbuf, int *recvcounts,
                                      int *rdispls, MPI_Datatype recvtype,
                                      MPI_Comm comm_row, int chunks_throttle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_lrank_node, my_mpi_size_global, my_mpi_rank_global;
  int handle, isdryrun, num_comm, num_comm_max;
  int *global_ranks, i, j, k, l, m, port, new_counts_displs, add, isize;
  int locmem_size_old;
  char *ip, *locmem_old;
  int num_comm_throttle, i_throttle;
  MPI_Type_size(sendtype, &type_size);
  handle = get_handle(&comm_code, &handle_code_max);
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  my_node = my_mpi_rank_row;
  my_lrank_row = 0;
  my_lrank_node = 0;
  my_mpi_size_global = my_mpi_size_row;
  my_mpi_rank_global = my_mpi_rank_row;
  new_counts_displs = (sdispls == NULL);
  if (new_counts_displs) {
    sdispls = (int *)malloc(my_mpi_size_row * sizeof(int));
    recvcounts = (int *)malloc(my_mpi_size_row * sizeof(int));
    rdispls = (int *)malloc(my_mpi_size_row * sizeof(int));
    MPI_Alltoall(sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, comm_row);
    sdispls[0] = 0;
    rdispls[0] = 0;
    for (i = 0; i < my_mpi_size_row - 1; i++) {
      sdispls[i + 1] = sdispls[i] + sendcounts[i];
      rdispls[i + 1] = rdispls[i] + recvcounts[i];
    }
  }
  global_ranks = (int *)malloc(sizeof(int) * my_mpi_size_row);
  setup_rank_translation(comm_row, 1, MPI_COMM_NULL, 1, global_ranks);
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    if (isdryrun) {
      ip = NULL;
    } else {
      if ((int)(num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status))) >
          locmem_size) {
        locmem_old = locmem;
        locmem_size_old = locmem_size;
        locmem_size = num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status));
        locmem = (char *)malloc(sizeof(char) * locmem_size);
        if (locmem_old != NULL) {
          rebase_addresses(locmem_old, locmem_size_old, (char *)locmem);
          free(locmem_old);
        }
      }
      ip = comm_code[handle] = (char *)malloc(sizeof(char *) * ((size_t)(ip)));
    }
    code_put_char(&ip, OPCODE_SETNUMCORES, isdryrun);
    code_put_int(&ip, 1, isdryrun);
    num_comm_max = 0;
    for (i_throttle = 0; i_throttle < chunks_throttle; i_throttle++) {
      num_comm = 0;
      num_comm_throttle = 0;
      for (port = 0; port < my_mpi_size_row - 1; port++) {
        num_comm_throttle++;
        if ((num_comm_throttle - 1) % chunks_throttle == i_throttle) {
          isize = recvcounts[(my_mpi_rank_global + my_mpi_size_global -
                              (port + 1)) %
                             my_mpi_size_global];
          if (isize) {
            code_put_char(&ip, OPCODE_MPIIRECV, isdryrun);
            code_put_pointer(
                &ip,
                (void *)(((char *)recvbuf) +
                         rdispls[(my_mpi_rank_global + my_mpi_size_global -
                                  (port + 1)) %
                                 my_mpi_size_global] *
                             type_size),
                isdryrun);
            code_put_int(&ip, isize * type_size, isdryrun);
            code_put_int(&ip,
                         global_ranks[(my_mpi_rank_global + my_mpi_size_global -
                                       (port + 1)) %
                                      my_mpi_size_global],
                         isdryrun);
            code_put_pointer(&ip,
                             (void *)(locmem + num_comm * sizeof(MPI_Request)),
                             isdryrun);
            num_comm++;
          }
          if ((i_throttle == 0) && (port == 0)) {
            isize = sendcounts[(my_mpi_rank_global + my_mpi_size_global) %
                               my_mpi_size_global];
            if (isize) {
              code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
              code_put_pointer(
                  &ip,
                  (void *)(((char *)recvbuf) +
                           rdispls[(my_mpi_rank_global + my_mpi_size_global) %
                                   my_mpi_size_global] *
                               type_size),
                  isdryrun);
              code_put_pointer(
                  &ip,
                  (void *)(((char *)sendbuf) +
                           sdispls[(my_mpi_rank_global + my_mpi_size_global) %
                                   my_mpi_size_global] *
                               type_size),
                  isdryrun);
              code_put_int(&ip, isize * type_size, isdryrun);
            }
          }
          isize = sendcounts[(my_mpi_rank_global + my_mpi_size_global +
                              (port + 1)) %
                             my_mpi_size_global];
          if (isize) {
            code_put_char(&ip, OPCODE_MPIISEND, isdryrun);
            code_put_pointer(
                &ip,
                (void *)(((char *)sendbuf) +
                         sdispls[(my_mpi_rank_global + my_mpi_size_global +
                                  (port + 1)) %
                                 my_mpi_size_global] *
                             type_size),
                isdryrun);
            code_put_int(&ip, isize * type_size, isdryrun);
            code_put_int(&ip,
                         global_ranks[(my_mpi_rank_global + my_mpi_size_global +
                                       (port + 1)) %
                                      my_mpi_size_global],
                         isdryrun);
            code_put_pointer(&ip,
                             (void *)(locmem + num_comm * sizeof(MPI_Request)),
                             isdryrun);
            num_comm++;
          }
        }
      }
      if (num_comm > 0) {
        code_put_char(&ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(&ip, num_comm, isdryrun);
        code_put_pointer(&ip, (void *)locmem, isdryrun);
      }
      if (num_comm > num_comm_max) {
        num_comm_max = num_comm;
      }
      num_comm = 0;
    }
    code_put_char(&ip, OPCODE_RETURN, isdryrun);
  }

  if (new_counts_displs) {
    free(rdispls);
    free(recvcounts);
    free(sdispls);
  }
  free(global_ranks);
  return (handle);
}

static int local_alltoallv_init(void *sendbuf, int *sendcounts, int *sdispls,
                                MPI_Datatype sendtype, void *recvbuf,
                                int *recvcounts, int *rdispls,
                                MPI_Datatype recvtype, MPI_Comm comm_row,
                                int my_cores_per_node_row, MPI_Comm comm_column,
                                int my_cores_per_node_column,
                                int num_active_ports, int chunks_throttle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
      my_mpi_size_global, my_mpi_rank_global;
  int handle, isdryrun, num_comm, num_comm_max;
  char volatile *my_shared_sendbuf, *my_shared_recvbuf;
  int *global_ranks, i, j, k, l, m, port, new_counts_displs, add, isize,
      my_size_shared_sendbuf, my_size_shared_recvbuf;
  char *ip, *shmem_old, *locmem_old;
  int lshmemid, shmem_size_old, locmem_size_old, lshmem_size_old;
  int volatile *lshmem_sendcounts, *lshmem_recvcounts, *lshmem = NULL;
  int num_comm_throttle, i_throttle;
  if (my_cores_per_node_row * my_cores_per_node_column == 1) {
    return (local_alltoallv_init_noshm(sendbuf, sendcounts, sdispls, sendtype,
                                       recvbuf, recvcounts, rdispls, recvtype,
                                       comm_row, chunks_throttle));
  }
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
  MPI_Type_size(sendtype, &type_size);
  handle = get_handle(&comm_code, &handle_code_max);
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
  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
  my_mpi_size_global = my_mpi_size_row * my_cores_per_node_column;
  my_mpi_rank_global = my_mpi_rank_row * my_cores_per_node_column +
                       my_mpi_rank_column % my_cores_per_node_column;
  new_counts_displs = (sdispls == NULL);
  if (new_counts_displs) {
    sdispls = (int *)malloc(my_mpi_size_row * sizeof(int));
    recvcounts = (int *)malloc(my_mpi_size_row * sizeof(int));
    rdispls = (int *)malloc(my_mpi_size_row * sizeof(int));
    MPI_Alltoall(sendcounts, 1, MPI_INT, recvcounts, 1, MPI_INT, comm_row);
    sdispls[0] = 0;
    rdispls[0] = 0;
    for (i = 0; i < my_mpi_size_row - 1; i++) {
      sdispls[i + 1] = sdispls[i] + sendcounts[i];
      rdispls[i + 1] = rdispls[i] + recvcounts[i];
    }
  }
  lshmem_size_old = 0;
  setup_shared_memory(
      comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
      my_mpi_size_row * my_cores_per_node_row * my_cores_per_node_column * 2 *
          sizeof(int),
      &lshmem_size_old, &lshmemid, (volatile char **)(&lshmem), 0, 0);
  lshmem_sendcounts = lshmem;
  lshmem_recvcounts = lshmem + my_mpi_size_row * my_cores_per_node_row *
                                   my_cores_per_node_column;
  for (j = 0; j < my_mpi_size_row / my_cores_per_node_row; j++) {
    for (i = 0; i < my_cores_per_node_row; i++) {
      lshmem_sendcounts
          [(i + ((my_mpi_size_row / my_cores_per_node_row + j - my_node) %
                 (my_mpi_size_row / my_cores_per_node_row)) *
                    my_cores_per_node_row) *
               my_cores_per_node_row * my_cores_per_node_column +
           my_lrank_row * my_cores_per_node_column + my_lrank_column] =
              sendcounts[i + j * my_cores_per_node_row];
      lshmem_recvcounts[(my_lrank_row +
                         ((my_mpi_size_row / my_cores_per_node_row - j +
                           my_node) %
                          (my_mpi_size_row / my_cores_per_node_row)) *
                             my_cores_per_node_row) *
                            my_cores_per_node_row * my_cores_per_node_column +
                        i * my_cores_per_node_column + my_lrank_column] =
          recvcounts[i + j * my_cores_per_node_row];
    }
  }
  MPI_Barrier(comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Barrier(comm_column);
    MPI_Barrier(comm_row);
  }
  my_size_shared_sendbuf = 0;
  my_size_shared_recvbuf = 0;
  for (i = 0;
       i < my_mpi_size_row * my_cores_per_node_row * my_cores_per_node_column;
       i++) {
    my_size_shared_sendbuf += lshmem_sendcounts[i];
    my_size_shared_recvbuf += lshmem_recvcounts[i];
  }
  my_size_shared_sendbuf *= type_size;
  my_size_shared_recvbuf *= type_size;
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  if (!setup_shared_memory(comm_row, my_cores_per_node_row, comm_column,
                           my_cores_per_node_column,
                           my_size_shared_sendbuf + my_size_shared_recvbuf +
                               NUM_BARRIERS,
                           &shmem_size, &shmemid, &shmem, 0, NUM_BARRIERS)) {
    if (shmem_old != NULL) {
      rebase_addresses(shmem_old, shmem_size_old, (char *)shmem);
    }
  }
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
  my_shared_sendbuf = shmem + NUM_BARRIERS;
  my_shared_recvbuf = shmem + NUM_BARRIERS + my_size_shared_sendbuf;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    if (isdryrun) {
      ip = NULL;
    } else {
      if ((int)(num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status))) >
          locmem_size) {
        locmem_old = locmem;
        locmem_size_old = locmem_size;
        locmem_size = num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status));
        locmem = (char *)malloc(sizeof(char) * locmem_size);
        if (locmem_old != NULL) {
          rebase_addresses(locmem_old, locmem_size_old, (char *)locmem);
          free(locmem_old);
        }
      }
      ip = comm_code[handle] = (char *)malloc(sizeof(char *) * ((size_t)(ip)));
    }
    code_put_char(&ip, OPCODE_SETNUMCORES, isdryrun);
    code_put_int(&ip, my_cores_per_node_row * my_cores_per_node_column,
                 isdryrun);
    num_comm_max = 0;
    for (i_throttle = 0; i_throttle < chunks_throttle; i_throttle++) {
      num_comm = 0;
      num_comm_throttle = 0;
      if (my_lrank_node < num_active_ports) {
        for (port = my_lrank_node;
             port < my_mpi_size_row / my_cores_per_node_row - 1;
             port += num_active_ports) {
          num_comm_throttle++;
          add = 0;
          m = 0;
          for (i = 0; i < my_cores_per_node_row * my_cores_per_node_row *
                              my_cores_per_node_column * (port + 1);
               i++) {
            add += lshmem_recvcounts[m++];
          }
          isize = 0;
          for (i = 0; i < my_cores_per_node_row * my_cores_per_node_row *
                              my_cores_per_node_column;
               i++) {
            isize +=
                lshmem_recvcounts[my_cores_per_node_row *
                                      my_cores_per_node_row *
                                      my_cores_per_node_column * (port + 1) +
                                  i];
          }
          if ((num_comm_throttle - 1) % chunks_throttle == i_throttle) {
            if (isize > 0) {
              code_put_char(&ip, OPCODE_MPIIRECV, isdryrun);
              code_put_pointer(
                  &ip, (void *)(((char *)my_shared_recvbuf) + add * type_size),
                  isdryrun);
              code_put_int(&ip, isize * type_size, isdryrun);
              code_put_int(
                  &ip,
                  global_ranks[(my_mpi_rank_global + my_mpi_size_global -
                                (port + 1) * my_cores_per_node_row *
                                    my_cores_per_node_column) %
                               my_mpi_size_global],
                  isdryrun);
              code_put_pointer(
                  &ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                  isdryrun);
              num_comm++;
            }
          }
        }
      }

      if (i_throttle == 0) {
        add = 0;
        m = 0;
        for (i = 0;
             i < my_lrank_row * my_cores_per_node_column + my_lrank_column;
             i++) {
          add += lshmem_sendcounts[m++];
        }
        for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++) {
          for (i = 0; i < my_cores_per_node_row; i++) {
            j = (my_mpi_size_row / my_cores_per_node_row + k + my_node) %
                (my_mpi_size_row / my_cores_per_node_row);
            if (i + j * my_cores_per_node_row != my_mpi_rank_row) {
              code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
              code_put_pointer(
                  &ip, (void *)(my_shared_sendbuf + add * type_size), isdryrun);
              code_put_pointer(
                  &ip,
                  (void *)(((char *)sendbuf) +
                           sdispls[i + j * my_cores_per_node_row] * type_size),
                  isdryrun);
              code_put_int(
                  &ip, sendcounts[i + j * my_cores_per_node_row] * type_size,
                  isdryrun);
            }
            for (j = 0; j < my_cores_per_node_row * my_cores_per_node_column;
                 j++) {
              add += lshmem_sendcounts[m++];
            }
          }
        }
        code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
      }
      num_comm_throttle = 0;
      if (my_lrank_node < num_active_ports) {
        for (port = my_lrank_node;
             port < my_mpi_size_row / my_cores_per_node_row - 1;
             port += num_active_ports) {
          num_comm_throttle++;
          add = 0;
          m = 0;
          for (i = 0; i < my_cores_per_node_row * my_cores_per_node_row *
                              my_cores_per_node_column * (port + 1);
               i++) {
            add += lshmem_sendcounts[m++];
          }
          isize = 0;
          for (i = 0; i < my_cores_per_node_row * my_cores_per_node_row *
                              my_cores_per_node_column;
               i++) {
            isize += lshmem_sendcounts[m++];
          }
          if ((num_comm_throttle - 1) % chunks_throttle == i_throttle) {
            if (isize > 0) {
              code_put_char(&ip, OPCODE_MPIISEND, isdryrun);
              code_put_pointer(
                  &ip, (void *)(((char *)my_shared_sendbuf) + add * type_size),
                  isdryrun);
              code_put_int(&ip, isize * type_size, isdryrun);
              code_put_int(&ip,
                           global_ranks[(my_mpi_rank_global +
                                         (port + 1) * my_cores_per_node_row *
                                             my_cores_per_node_column) %
                                        my_mpi_size_global],
                           isdryrun);
              code_put_pointer(
                  &ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                  isdryrun);
              num_comm++;
            }
          }
        }
      }

      if (i_throttle == chunks_throttle - 1) {
        code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
        code_put_pointer(
            &ip,
            (void *)(((char *)recvbuf) + rdispls[my_mpi_rank_row] * type_size),
            isdryrun);
        code_put_pointer(
            &ip,
            (void *)(((char *)sendbuf) + sdispls[my_mpi_rank_row] * type_size),
            isdryrun);
        code_put_int(&ip, sendcounts[my_mpi_rank_row] * type_size, isdryrun);
        for (i = 0; i < my_cores_per_node_row; i++) {
          if (i != my_lrank_row) {
            add = 0;
            m = 0;
            for (j = 0; j < i * my_cores_per_node_column + my_lrank_column;
                 j++) {
              add += lshmem_recvcounts[m++];
            }
            for (k = 0; k < my_lrank_row; k++) {
              for (j = 0; j < my_cores_per_node_row * my_cores_per_node_column;
                   j++) {
                add += lshmem_recvcounts[m++];
              }
            }
            for (j = 0;
                 j < my_cores_per_node_row * my_cores_per_node_row *
                         my_cores_per_node_column * (i / my_cores_per_node_row);
                 j++) {
              add += lshmem_recvcounts[m++];
            }
            j = (my_mpi_size_row / my_cores_per_node_row - 0 + my_node) %
                (my_mpi_size_row / my_cores_per_node_row);
            code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
            code_put_pointer(
                &ip,
                (void *)(((char *)recvbuf) +
                         rdispls[i + j * my_cores_per_node_row] * type_size),
                isdryrun);
            code_put_pointer(&ip, (void *)(my_shared_sendbuf + add * type_size),
                             isdryrun);
            code_put_int(&ip,
                         recvcounts[i + j * my_cores_per_node_row] * type_size,
                         isdryrun);
          }
        }
      }
      if (num_comm > 0) {
        code_put_char(&ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(&ip, num_comm, isdryrun);
        code_put_pointer(&ip, (void *)locmem, isdryrun);
      }
      if (num_comm > num_comm_max) {
        num_comm_max = num_comm;
      }
      num_comm = 0;
    }
    code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);

    for (k = 1; k < my_mpi_size_row / my_cores_per_node_row; k++) {
      for (l = 0; l < my_cores_per_node_row; l++) {
        add = 0;
        m = 0;
        for (i = 0; i < l * my_cores_per_node_column + my_lrank_column; i++) {
          add += lshmem_recvcounts[m++];
        }
        for (j = 0; j < my_lrank_row; j++) {
          for (i = 0; i < my_cores_per_node_row * my_cores_per_node_column;
               i++) {
            add += lshmem_recvcounts[m++];
          }
        }
        for (i = 0; i < my_cores_per_node_row * my_cores_per_node_row *
                            my_cores_per_node_column * k;
             i++) {
          add += lshmem_recvcounts[m++];
        }
        j = (my_mpi_size_row / my_cores_per_node_row - k + my_node) %
            (my_mpi_size_row / my_cores_per_node_row);
        code_put_char(&ip, OPCODE_MEMCPY, isdryrun);
        code_put_pointer(
            &ip,
            (void *)(((char *)recvbuf) +
                     rdispls[l + j * my_cores_per_node_row] * type_size),
            isdryrun);
        code_put_pointer(
            &ip, (void *)(((char *)my_shared_recvbuf) + add * type_size),
            isdryrun);
        code_put_int(&ip, recvcounts[l + j * my_cores_per_node_row] * type_size,
                     isdryrun);
      }
    }
    code_put_char(&ip, OPCODE_NODEBARRIER, isdryrun);
    code_put_char(&ip, OPCODE_RETURN, isdryrun);
  }

  if (new_counts_displs) {
    free(rdispls);
    free(recvcounts);
    free(sdispls);
  }
  destroy_shared_memory(&lshmem_size_old, &lshmemid, (char volatile **)&lshmem);
  node_barrier_mpi();
  free(global_ranks);
  return (handle);
}

static void node_barrier(int num_cores) {
  __sync_fetch_and_add(shmem + barrier_count, 1);
  while (shmem[barrier_count] != num_cores) {
    ;
  }
  shmem[(barrier_count + NUM_BARRIERS - 1) % NUM_BARRIERS] = 0;
  barrier_count = (barrier_count + 1) % NUM_BARRIERS;
}

static int local_alltoall_nonblocking(int handle) {
  char instruction, *ip2, *ip;
  int handle2, isdryrun, numwaits;
  handle2 = get_handle(&comm_code, &handle_code_max);
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    numwaits = 0;
    if (isdryrun) {
      ip = comm_code[handle];
      ip2 = NULL;
    } else {
      comm_code[handle2] = (char *)malloc(sizeof(char *) * ((size_t)(ip2)));
      ip2 = ip = comm_code[handle];
    }
    do {
      instruction = code_get_char(&ip);
      switch (instruction) {
      case OPCODE_RETURN:
        code_put_char(&ip2, OPCODE_RETURN, isdryrun);
        break;
      case OPCODE_MEMCPY:
        code_put_char(&ip2, OPCODE_MEMCPY, isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        break;
      case OPCODE_MPIIRECV:
        code_put_char(&ip2, OPCODE_MPIIRECV, isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        break;
      case OPCODE_MPIISEND:
        code_put_char(&ip2, OPCODE_MPIISEND, isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        break;
      case OPCODE_MPIWAITALL:
        numwaits++;
        if (numwaits > 1) {
          printf("multiple MPI_Waitall are not supported\n");
          exit(1);
        }
        if (!isdryrun) {
          code_put_char(&ip2, OPCODE_RETURN, isdryrun);
          ip2 = comm_code[handle2];
        }
        code_put_char(&ip2, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        break;
      case OPCODE_NODEBARRIER:
        code_put_char(&ip2, OPCODE_NODEBARRIER, isdryrun);
        break;
      case OPCODE_SETNUMCORES:
        code_put_char(&ip2, OPCODE_SETNUMCORES, isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        break;
      case OPCODE_REDUCE:
        code_put_char(&ip2, OPCODE_REDUCE, isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        code_put_pointer(&ip2, code_get_pointer(&ip), isdryrun);
        code_put_int(&ip2, code_get_int(&ip), isdryrun);
        break;
      case OPCODE_LOCALMEM:
        printf("local memory not supported\n");
        exit(1);
        break;
      default:
        printf("illegal MPI_OPCODE\n");
        exit(1);
      }
    } while (instruction != OPCODE_RETURN);
    if (!isdryrun && (ip == ip2)) {
      ip2 = comm_code[handle2];
      code_put_char(&ip2, OPCODE_RETURN, isdryrun);
    }
  }
  return (handle2);
}

static int size_of_code(int handle) {
  char *ip, instruction;
  int n_r, i;
  ip = comm_code[handle];
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
      code_get_int(&ip);
      code_get_pointer(&ip);
      break;
    case OPCODE_MPIISEND:
      code_get_pointer(&ip);
      code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      break;
    case OPCODE_MPIWAITALL:
      code_get_int(&ip);
      code_get_pointer(&ip);
      break;
    case OPCODE_NODEBARRIER:
      break;
    case OPCODE_SETNUMCORES:
      code_get_int(&ip);
      break;
    case OPCODE_REDUCE:
      code_get_char(&ip);
      code_get_pointer(&ip);
      code_get_pointer(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_MPISENDRECV:
      code_get_pointer(&ip);
      code_get_int(&ip);
      code_get_int(&ip);
      code_get_pointer(&ip);
      code_get_int(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_MPISEND:
      code_get_pointer(&ip);
      code_get_int(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_MPIRECV:
      code_get_pointer(&ip);
      code_get_int(&ip);
      code_get_int(&ip);
      break;
    case OPCODE_LOCALMEM:
      code_get_int(&ip);
      n_r = code_get_int(&ip);
      for (i = 0; i < n_r; i++) {
        code_get_int(&ip);
      }
      break;
    default:
      printf("illegal MPI_OPCODE\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return (ip - comm_code[handle]);
}

int EXT_MPI_Merge_collectives_native(int handle1, int handle2) {
  int handle, size1, size2;
  handle = get_handle(&comm_code, &handle_code_max);
  size1 = size_of_code(handle1);
  size2 = size_of_code(handle2);
  comm_code[handle] = (char *)malloc(sizeof(char *) * (size1 + size2));
  memcpy(comm_code[handle], comm_code[handle1], size1);
  memcpy(comm_code[handle] + size1 - 1, comm_code[handle2], size2);
  EXT_MPI_Done_native(handle1);
  EXT_MPI_Done_native(handle2);
  return (handle);
}

int EXT_MPI_Alltoall_init_native(void *sendbuf, int sendcount,
                                 MPI_Datatype sendtype, void *recvbuf,
                                 int recvcount, MPI_Datatype recvtype,
                                 MPI_Comm comm_row, int my_cores_per_node_row,
                                 MPI_Comm comm_column,
                                 int my_cores_per_node_column, int num_ports,
                                 int num_active_ports, int chunks_throttle) {
  return (local_alltoall_init(sendbuf, sendcount, sendtype, recvbuf, recvcount,
                              recvtype, comm_row, my_cores_per_node_row,
                              comm_column, my_cores_per_node_column, num_ports,
                              num_active_ports, chunks_throttle));
}

int EXT_MPI_Alltoallv_init_native(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int num_active_ports, int chunks_throttle) {
  return (local_alltoallv_init(
      sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
      recvtype, comm_row, my_cores_per_node_row, comm_column,
      my_cores_per_node_column, num_active_ports, chunks_throttle));
}

void EXT_MPI_Ialltoall_init_native(void *sendbuf, int sendcount,
                                   MPI_Datatype sendtype, void *recvbuf,
                                   int recvcount, MPI_Datatype recvtype,
                                   MPI_Comm comm_row, int my_cores_per_node_row,
                                   MPI_Comm comm_column,
                                   int my_cores_per_node_column,
                                   int num_active_ports, int *handle_begin,
                                   int *handle_wait) {
  int my_mpi_size_row, num_ports, chunks_throttle = 1;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  num_ports = my_mpi_size_row / my_cores_per_node_row - 1;
  *handle_begin = local_alltoall_init(
      sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm_row,
      my_cores_per_node_row, comm_column, my_cores_per_node_column, num_ports,
      num_active_ports, chunks_throttle);
  *handle_wait = local_alltoall_nonblocking(*handle_begin);
}

void EXT_MPI_Ialltoallv_init_native(
    void *sendbuf, int *sendcounts, int *sdispls, MPI_Datatype sendtype,
    void *recvbuf, int *recvcounts, int *rdispls, MPI_Datatype recvtype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int num_active_ports, int *handle_begin,
    int *handle_wait) {
  int chunks_throttle = 1;
  *handle_begin = local_alltoallv_init(
      sendbuf, sendcounts, sdispls, sendtype, recvbuf, recvcounts, rdispls,
      recvtype, comm_row, my_cores_per_node_row, comm_column,
      my_cores_per_node_column, num_active_ports, chunks_throttle);
  *handle_wait = local_alltoall_nonblocking(*handle_begin);
}

static int num_cores = -1;

static int exec_native(char *ip) {
  char instruction, instruction2, *r_start, *r_temp, *ipl;
  void volatile *p1, *p2;
  void *rlocmem = NULL;
  int i1, i2, i3, n_r, s_r, i;
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      if (rlocmem != NULL) {
        ipl = r_start;
        for (i = 0; i < n_r; i++) {
          i1 = code_get_int(&ipl);
          r_temp = r_start + i1;
          p1 = code_get_pointer(&r_temp);
          r_temp = r_start + i1;
          code_put_pointer(&r_temp, ((void *)p1 - (rlocmem - NULL)), 0);
        }
        free(rlocmem);
      }
      break;
    case OPCODE_LOCALMEM:
      s_r = code_get_int(&ip);
      n_r = code_get_int(&ip);
      rlocmem = (int *)malloc(s_r);
      r_start = ip;
      for (i = 0; i < n_r; i++) {
        i1 = code_get_int(&ip);
        r_temp = r_start + i1;
        p1 = code_get_pointer(&r_temp);
        r_temp = r_start + i1;
        code_put_pointer(&r_temp, ((void *)p1 + (rlocmem - NULL)), 0);
      }
      break;
    case OPCODE_MEMCPY:
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      memcpy((void *)p1, (void *)p2, code_get_int(&ip));
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      MPI_Irecv((void *)p1, i1, MPI_CHAR, i2, 0, MPI_COMM_WORLD,
                (MPI_Request *)code_get_pointer(&ip));
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      MPI_Isend((void *)p1, i1, MPI_CHAR, i2, 0, MPI_COMM_WORLD,
                (MPI_Request *)code_get_pointer(&ip));
      break;
    case OPCODE_MPIWAITALL:
      i1 = code_get_int(&ip);
      p1 = code_get_pointer(&ip);
      MPI_Waitall(i1, (MPI_Request *)p1, MPI_STATUSES_IGNORE);
      break;
    case OPCODE_NODEBARRIER:
      node_barrier(num_cores);
      break;
    case OPCODE_SETNUMCORES:
      num_cores = code_get_int(&ip);
      break;
    case OPCODE_REDUCE:
      instruction2 = code_get_char(&ip);
      switch (instruction2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        p1 = code_get_pointer(&ip);
        p2 = code_get_pointer(&ip);
        i1 = code_get_int(&ip);
        for (i2 = 0; i2 < i1; i2++) {
          ((double *)p1)[i2] += ((double *)p2)[i2];
        }
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        p1 = code_get_pointer(&ip);
        p2 = code_get_pointer(&ip);
        i1 = code_get_int(&ip);
        for (i2 = 0; i2 < i1; i2++) {
          ((long int *)p1)[i2] += ((long int *)p2)[i2];
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
                   code_get_int(&ip), 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      break;
    case OPCODE_MPISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      MPI_Send((void *)p1, i1, MPI_CHAR, code_get_int(&ip), 0, MPI_COMM_WORLD);
      break;
    case OPCODE_MPIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      MPI_Recv((void *)p1, i1, MPI_CHAR, code_get_int(&ip), 0, MPI_COMM_WORLD,
               MPI_STATUS_IGNORE);
      break;
    default:
      printf("illegal MPI_OPCODE\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return (0);
}

int EXT_MPI_Exec_native(int handle) { return (exec_native(comm_code[handle])); }

int EXT_MPI_Done_native(int handle) {
  int i;
  node_barrier_mpi();
  free(comm_code[handle]);
  comm_code[handle] = NULL;
  for (i = 0; i < handle_code_max; i++) {
    if (comm_code[i] != NULL) {
      return (0);
    }
  }
  destroy_shared_memory(&shmem_size, &shmemid, &shmem);
  node_barrier_mpi();
  if (shmem_comm_node_row != MPI_COMM_NULL) {
    MPI_Comm_free(&shmem_comm_node_row);
    shmem_comm_node_row = MPI_COMM_NULL;
  }
  if (shmem_comm_node_column != MPI_COMM_NULL) {
    MPI_Comm_free(&shmem_comm_node_column);
    shmem_comm_node_column = MPI_COMM_NULL;
  }
  shmem = NULL;
  shmem_size = 0;
  shmemid = -1;
  free(locmem);
  locmem = NULL;
  locmem_size = 0;
  free(comm_code);
  comm_code = NULL;
  return (0);
}

static void exec_single(char **code, char *code_start, int single,
                        int isdryrun) {
  char *code_temp;
  if (single && (!isdryrun)) {
    code_temp = code_start;
    code_put_char(code, OPCODE_RETURN, isdryrun);
    exec_native(code_start);
    *code = code_temp;
  }
}

static void allgatherv_exec_recursive_copyin(
    void *sendbuf, char volatile *my_shared_buf, int volatile *lshmem_counts,
    int my_node, int my_lrank_row, int my_lrank_column,
    int my_cores_per_node_row, int my_cores_per_node_column, int recvcount,
    char **ip, int single_instruction, int isdryrun) {
  int add, i, j, k;
  char *i_start;
  if (recvcount < 0) {
    add = 0;
    for (k = 0; k < my_node; k++) {
      for (i = 0; i < my_cores_per_node_row; i++) {
        for (j = 0; j < my_cores_per_node_column; j++) {
          add += lshmem_counts[(k * my_cores_per_node_row + i) *
                                   my_cores_per_node_column +
                               j];
        }
      }
    }
    for (i = 0; i < my_cores_per_node_row; i++) {
      for (j = 0; j < my_lrank_column; j++) {
        add += lshmem_counts[(my_node * my_cores_per_node_row + i) *
                                 my_cores_per_node_column +
                             j];
      }
    }
    for (j = 0; j < my_lrank_row; j++) {
      add += lshmem_counts[(my_node * my_cores_per_node_row + j) *
                               my_cores_per_node_column +
                           my_lrank_column];
    }
    i = (lshmem_counts[(my_node * my_cores_per_node_row + my_lrank_row) *
                           my_cores_per_node_column +
                       my_lrank_column] > 0);
  } else {
    add = (my_node * my_cores_per_node_row * my_cores_per_node_column +
           my_cores_per_node_row * my_lrank_column + my_lrank_row) *
          recvcount;
    i = 1;
  }
  if (i) {
    i_start = *ip;
    code_put_char(ip, OPCODE_MEMCPY, isdryrun);
    code_put_pointer(ip, (void *)(my_shared_buf + add), isdryrun);
    code_put_pointer(ip, (void *)(char *)sendbuf, isdryrun);
    if (recvcount < 0) {
      code_put_int(
          ip,
          lshmem_counts[(my_node * my_cores_per_node_row + my_lrank_row) *
                            my_cores_per_node_column +
                        my_lrank_column],
          isdryrun);
    } else {
      code_put_int(ip, recvcount, isdryrun);
    }
    exec_single(ip, i_start, single_instruction, isdryrun);
  }
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
    i_start = *ip;
    code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
    exec_single(ip, i_start, single_instruction, isdryrun);
  }
}

static void allgatherv_exec_recursive_copyout(
    int element, void *recvbuf, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int my_node, int my_lrank_row,
    int my_lrank_column, int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int *displs_perm, int type_size,
    int recvcount, char **ip, int single_instruction, int isdryrun) {
  int add, i, k, l, m;
  char *i_start;
  for (i = 0; i < my_cores_per_node_row; i++) {
    if (recvcount < 0) {
      add = 0;
      for (m = 0; m < my_cores_per_node_column; m++) {
        for (k = 0; k < my_cores_per_node_row; k++) {
          for (l = 0; l < element; l++) {
            add += lshmem_counts[(l * my_cores_per_node_row + k) *
                                     my_cores_per_node_column +
                                 m];
          }
        }
      }
      for (l = 0; l < my_cores_per_node_row; l++) {
        for (k = 0; k < my_lrank_column; k++) {
          add += lshmem_counts[(element * my_cores_per_node_row + l) *
                                   my_cores_per_node_column +
                               k];
        }
      }
      for (l = 0; l < i; l++) {
        add += lshmem_counts[(element * my_cores_per_node_row + l) *
                                 my_cores_per_node_column +
                             my_lrank_column];
      }
      m = (lshmem_counts[(element * my_cores_per_node_row + i) *
                             my_cores_per_node_column +
                         my_lrank_column] > 0);
    } else {
      add = (my_cores_per_node_column * my_cores_per_node_row * element +
             my_cores_per_node_row * my_lrank_column + i) *
            recvcount;
      m = 1;
    }
    if (m) {
      i_start = *ip;
      if (recvcount < 0) {
        code_put_char(ip, OPCODE_MEMCPY, isdryrun);
        code_put_pointer(
            ip,
            (void *)(((char *)recvbuf) +
                     displs_perm[element * my_cores_per_node_row + i] *
                         type_size),
            isdryrun);
        code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add), isdryrun);
        code_put_int(ip,
                     lshmem_counts[(element * my_cores_per_node_row + i) *
                                       my_cores_per_node_column +
                                   my_lrank_column],
                     isdryrun);
      } else {
        code_put_char(ip, OPCODE_MEMCPY, isdryrun);
        code_put_pointer(
            ip,
            (void *)(((char *)recvbuf) +
                     (element * my_cores_per_node_row + i) * recvcount),
            isdryrun);
        code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add), isdryrun);
        code_put_int(ip, recvcount, isdryrun);
      }
      exec_single(ip, i_start, single_instruction, isdryrun);
    }
  }
}

static void allgatherv_exec_recursive_send_recv_indices(
    int my_node, int port, int num_ports, int gbstep, int *coarse_counts,
    int *coarse_displs, int *t_node, int *stepsizem1, int *sr_lrank,
    int send_or_recv, int *isize, int *add, int num_active_ports,
    int recvcount_coarse) {
  int t0_node, t2_node, i;
  t0_node = (my_node - my_node % ((num_ports + 1) * gbstep));
  (*t_node) = t0_node + port * gbstep;
  if (((*t_node) - my_node) / gbstep >= 0) {
    (*t_node) += gbstep;
  }
  (*t_node) += my_node % gbstep;
  if (send_or_recv == 0) {
    if (my_node < (*t_node)) {
      (*stepsizem1) = ((*t_node) - my_node) / gbstep - 1;
    } else {
      (*stepsizem1) =
          ((*t_node) - my_node + (num_ports + 1) * gbstep) / gbstep - 1;
    }
  } else {
    if (*t_node < (my_node)) {
      (*stepsizem1) = ((my_node) - *t_node) / gbstep - 1;
    } else {
      (*stepsizem1) =
          ((my_node) - *t_node + (num_ports + 1) * gbstep) / gbstep - 1;
    }
  }
  for (i = 0; i < num_ports; i++) {
    t2_node = ((*t_node) - (*t_node) % ((num_ports + 1) * gbstep)) + i * gbstep;
    if ((t2_node - (*t_node)) / gbstep >= 0) {
      t2_node += gbstep;
    }
    t2_node += (*t_node) % gbstep;
    if (t2_node == my_node) {
      (*sr_lrank) = i % num_active_ports;
    }
  }
  if (recvcount_coarse < 0) {
    if (send_or_recv == 0) {
      *isize = 0;
      for (i = 0; i < gbstep; i++) {
        *isize += coarse_counts[(*t_node / gbstep) * gbstep + i];
      }
      *add = coarse_displs[(*t_node / gbstep) * gbstep];
    } else {
      *isize = 0;
      for (i = 0; i < gbstep; i++) {
        *isize += coarse_counts[(my_node / gbstep) * gbstep + i];
      }
      *add = coarse_displs[(my_node / gbstep) * gbstep];
    }
  } else {
    *isize = gbstep * recvcount_coarse;
    if (send_or_recv == 0) {
      *add = (*t_node / gbstep) * gbstep * recvcount_coarse;
    } else {
      *add = (my_node / gbstep) * gbstep * recvcount_coarse;
    }
  }
}

static void allgatherv_exec_recursive(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int my_lrank_node, int my_lrank_row,
    int my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs,
    int *num_ports, int num_active_ports, int *global_ranks,
    int *rank_back_perm, int *displs_perm, char **ip, int single_instruction,
    int isdryrun, int *num_comm_max, int num_throttle, int allreduce,
    int recvcount) {
  int copy_list[my_mpi_size_row / my_cores_per_node_row];
  int gbstep, igbstep, port, num_comm, i;
  int add, isize, t_node, sr_lrank, stepsizem1, throttle;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# allgatherv_exec_recursive %d %d\n", my_cores_per_node_row,
           my_cores_per_node_column);
  }
#endif
  num_throttle *= num_active_ports;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    copy_list[i] = 0;
  }
  *num_comm_max = 0;
  num_comm = 0;
  igbstep = 0;
  for (gbstep = 1; gbstep < my_mpi_size_row / my_cores_per_node_row;
       gbstep *= (num_ports[igbstep++] + 1)) {
    for (throttle = 0; throttle < my_mpi_size_row / my_cores_per_node_row;
         throttle += num_throttle) {
      num_comm = 0;
      if ((my_lrank_node < num_active_ports) &&
          (my_lrank_node < num_ports[igbstep])) {
        for (port = my_lrank_node; port < num_ports[igbstep];
             port +=
             ((num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                      : num_ports[igbstep])) {
          allgatherv_exec_recursive_send_recv_indices(
              my_node, port, num_ports[igbstep], gbstep, coarse_counts,
              coarse_displs, &t_node, &stepsizem1, &sr_lrank, 0, &isize, &add,
              num_active_ports,
              recvcount * my_cores_per_node_row * my_cores_per_node_column);
          if (((stepsizem1 - throttle) < num_throttle) &&
              ((stepsizem1 - throttle) >= 0)) {
            if (isize > 0) {
              i_start = *ip;
              code_put_char(ip, OPCODE_MPIIRECV, isdryrun);
              code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                               isdryrun);
              code_put_int(ip, isize, isdryrun);
              if (rank_back_perm != NULL) {
                code_put_int(ip,
                             global_ranks[rank_back_perm[t_node] *
                                              my_cores_per_node_row *
                                              my_cores_per_node_column +
                                          sr_lrank],
                             isdryrun);
              } else {
                code_put_int(ip,
                             global_ranks[t_node * my_cores_per_node_row *
                                              my_cores_per_node_column +
                                          sr_lrank],
                             isdryrun);
              }
              code_put_pointer(
                  ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                  isdryrun);
              exec_single(ip, i_start, single_instruction, isdryrun);
              num_comm++;
            }
          }
        }
      }
      for (port = 0; port < num_ports[igbstep]; port++) {
        allgatherv_exec_recursive_send_recv_indices(
            my_node, port, num_ports[igbstep], gbstep, coarse_counts,
            coarse_displs, &t_node, &stepsizem1, &sr_lrank, 0, &isize, &add,
            num_active_ports,
            recvcount * my_cores_per_node_row * my_cores_per_node_column);
        if (((stepsizem1 - throttle) < num_throttle) &&
            ((stepsizem1 - throttle) >= 0)) {
          for (i = 0; i < gbstep; i++) {
            copy_list[(t_node / gbstep) * gbstep + i] = 1;
          }
        }
      }
      if ((gbstep == 1) && (throttle == 0)) {
        if (!allreduce) {
          allgatherv_exec_recursive_copyin(
              sendbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
              my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
              recvcount, ip, single_instruction, isdryrun);
        }
        copy_list[my_node] = 2;
      }
      if ((my_lrank_node < num_active_ports) &&
          (my_lrank_node < num_ports[igbstep])) {
        for (port = my_lrank_node; port < num_ports[igbstep];
             port +=
             ((num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                      : num_ports[igbstep])) {
          allgatherv_exec_recursive_send_recv_indices(
              my_node, port, num_ports[igbstep], gbstep, coarse_counts,
              coarse_displs, &t_node, &stepsizem1, &sr_lrank, 1, &isize, &add,
              num_active_ports,
              recvcount * my_cores_per_node_row * my_cores_per_node_column);
          if (((stepsizem1 - throttle) < num_throttle) &&
              ((stepsizem1 - throttle) >= 0)) {
            if (isize > 0) {
              i_start = *ip;
              code_put_char(ip, OPCODE_MPIISEND, isdryrun);
              code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                               isdryrun);
              code_put_int(ip, isize, isdryrun);
              if (rank_back_perm != NULL) {
                code_put_int(ip,
                             global_ranks[rank_back_perm[t_node] *
                                              my_cores_per_node_row *
                                              my_cores_per_node_column +
                                          sr_lrank],
                             isdryrun);
              } else {
                code_put_int(ip,
                             global_ranks[t_node * my_cores_per_node_row *
                                              my_cores_per_node_column +
                                          sr_lrank],
                             isdryrun);
              }
              code_put_pointer(
                  ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                  isdryrun);
              exec_single(ip, i_start, single_instruction, isdryrun);
              num_comm++;
            }
          }
        }
      }
      for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
        if (copy_list[i] == 2) {
          if (recvbuf != my_shared_buf) {
            allgatherv_exec_recursive_copyout(
                i, recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
                my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
                my_cores_per_node_column, displs_perm, type_size, recvcount, ip,
                single_instruction, isdryrun);
          }
          copy_list[i] = 3;
        }
      }
      if (num_comm > 0) {
        i_start = *ip;
        code_put_char(ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(ip, num_comm, isdryrun);
        code_put_pointer(ip, (void *)locmem, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
        if (copy_list[i] == 1) {
          copy_list[i] = 2;
        }
      }
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        i_start = *ip;
        code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      if (num_comm > *num_comm_max) {
        *num_comm_max = num_comm;
      }
      num_comm = 0;
    }
  }
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    if (copy_list[i] == 2) {
      if (recvbuf != my_shared_buf) {
        allgatherv_exec_recursive_copyout(
            i, recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
            my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
            my_cores_per_node_column, displs_perm, type_size, recvcount, ip,
            single_instruction, isdryrun);
      }
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void allgatherv_exec_recursive_ring_send_recv_indices(
    int my_node, int port, int num_ports, int gbstep, int *coarse_counts,
    int *coarse_displs, int *t_node, int send_or_recv, int *isize, int *add,
    int num_active_ports, int recvcount_coarse) {
  int t0_node, i;
  t0_node = (my_node - my_node % ((num_ports + 1) * gbstep));
  if (recvcount_coarse < 0) {
    if (send_or_recv == 0) {
      *t_node = ((num_ports + 1) * gbstep + my_node - gbstep) %
                    ((num_ports + 1) * gbstep) +
                t0_node;
      *isize = 0;
      for (i = 0; i < gbstep; i++) {
        *isize += coarse_counts[((num_ports + 1 + my_node / gbstep - port - 1) %
                                 (num_ports + 1)) *
                                    gbstep +
                                t0_node + i];
      }
      *add = coarse_displs[((num_ports + 1 + my_node / gbstep - port - 1) %
                            (num_ports + 1)) *
                               gbstep +
                           t0_node];
    } else {
      *t_node = (my_node + gbstep) % ((num_ports + 1) * gbstep) + t0_node;
      *isize = 0;
      for (i = 0; i < gbstep; i++) {
        *isize += coarse_counts[((num_ports + 1 + my_node / gbstep - port) %
                                 (num_ports + 1)) *
                                    gbstep +
                                t0_node + i];
      }
      *add = coarse_displs[((num_ports + 1 + my_node / gbstep - port) %
                            (num_ports + 1)) *
                               gbstep +
                           t0_node];
    }
  } else {
    *isize = gbstep * recvcount_coarse;
    if (send_or_recv == 0) {
      *t_node = ((num_ports + 1) * gbstep + my_node - gbstep) %
                    ((num_ports + 1) * gbstep) +
                t0_node;
      *add =
          (((num_ports + 1 + my_node / gbstep - port - 1) % (num_ports + 1)) *
               gbstep +
           t0_node) *
          recvcount_coarse;
    } else {
      *t_node = (my_node + gbstep) % ((num_ports + 1) * gbstep) + t0_node;
      *add = (((num_ports + 1 + my_node / gbstep - port) % (num_ports + 1)) *
                  gbstep +
              t0_node) *
             recvcount_coarse;
    }
  }
}

static void allgatherv_exec_recursive_ring(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int my_lrank_node, int my_lrank_row,
    int my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs,
    int *num_ports, int num_active_ports, int *global_ranks,
    int *rank_back_perm, int *displs_perm, char **ip, int single_instruction,
    int isdryrun, int *num_comm_max, int allreduce, int recvcount) {
  int gbstep, igbstep, port, num_comm, i;
  int add, isize, t_node, isize2, add2, t_node2;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# allgatherv_exec_recursive_ring %d %d\n", my_cores_per_node_row,
           my_cores_per_node_column);
  }
#endif
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  *num_comm_max = 0;
  num_comm = 0;
  igbstep = 0;
  if (!allreduce) {
    allgatherv_exec_recursive_copyin(
        sendbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
        my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
        recvcount, ip, single_instruction, isdryrun);
  }
  for (gbstep = 1; gbstep < my_mpi_size_row / my_cores_per_node_row;
       gbstep *= (num_ports[igbstep++] + 1)) {
    for (port = 0; port < num_ports[igbstep]; port++) {
      num_comm = 0;
      allgatherv_exec_recursive_ring_send_recv_indices(
          my_node, port, num_ports[igbstep], gbstep, coarse_counts,
          coarse_displs, &t_node, 0, &isize, &add, num_active_ports,
          recvcount * my_cores_per_node_row * my_cores_per_node_column);
      allgatherv_exec_recursive_ring_send_recv_indices(
          my_node, port, num_ports[igbstep], gbstep, coarse_counts,
          coarse_displs, &t_node2, 1, &isize2, &add2, num_active_ports,
          recvcount * my_cores_per_node_row * my_cores_per_node_column);
      if ((isize > 0) && (isize2 > 0)) {
        code_put_char(ip, OPCODE_MPISENDRECV, isdryrun);
      } else {
        if (isize2 > 0) {
          code_put_char(ip, OPCODE_MPISEND, isdryrun);
        } else {
          if (isize > 0) {
            code_put_char(ip, OPCODE_MPIRECV, isdryrun);
          }
        }
      }
      if (isize2 > 0) {
        i_start = *ip;
        code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add2),
                         isdryrun);
        code_put_int(ip, isize2, isdryrun);
        if (recvcount < 0) {
          code_put_int(
              ip,
              global_ranks[rank_back_perm[t_node2] * my_cores_per_node_row *
                           my_cores_per_node_column],
              isdryrun);
        } else {
          code_put_int(ip,
                       global_ranks[t_node2 * my_cores_per_node_row *
                                    my_cores_per_node_column],
                       isdryrun);
        }
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      if (isize > 0) {
        i_start = *ip;
        code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add), isdryrun);
        code_put_int(ip, isize, isdryrun);
        if (recvcount < 0) {
          code_put_int(
              ip,
              global_ranks[rank_back_perm[t_node] * my_cores_per_node_row *
                           my_cores_per_node_column],
              isdryrun);
        } else {
          code_put_int(ip,
                       global_ranks[t_node * my_cores_per_node_row *
                                    my_cores_per_node_column],
                       isdryrun);
        }
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      if (num_comm > *num_comm_max) {
        *num_comm_max = num_comm;
      }
      num_comm = 0;
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    if (recvbuf != my_shared_buf) {
      allgatherv_exec_recursive_copyout(
          i, recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
          my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
          my_cores_per_node_column, displs_perm, type_size, recvcount, ip,
          single_instruction, isdryrun);
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void reduce_scatter_exec_copyin_long(
    int nelements, int type_size, char **input_bufs,
    char volatile **output_bufs, int base_size_shared, int *sizes,
    int my_lrank_row, int my_cores_per_node_row, int reduction_op, char **ip,
    int single_instruction, int isdryrun) {
  int sizespp[my_cores_per_node_row];
  int sum, si, n, i, j, k;
  int i_start, i_end, j_start, j_end;
  char *i__start;
  sum = 0;
  for (i = 0; i < nelements; i++) {
    sum += sizes[i];
  }
  sum /= type_size;
  for (i = 0; i < my_cores_per_node_row; i++) {
    sizespp[i] = sum / my_cores_per_node_row;
  }
  for (i = 0; i < sum % my_cores_per_node_row; i++) {
    sizespp[i]++;
  }
  for (n = 0; n < my_cores_per_node_row; n++) {
    j = 0;
    for (k = 0; k < (n + my_lrank_row) % my_cores_per_node_row; k++) {
      j += sizespp[k % my_cores_per_node_row];
    }
    i_start = 0;
    si = 0;
    while (si < j) {
      si += sizes[(i_start++) % nelements] / type_size;
    }
    if (si > j) {
      i_start = i_start - 1;
      j_start = sizes[i_start % nelements] / type_size - (si - j);
    } else {
      j_start = 0;
    }
    j = 0;
    for (k = 0; k < (n + my_lrank_row) % my_cores_per_node_row + 1; k++) {
      j += sizespp[k % my_cores_per_node_row];
    }
    i_end = 0;
    si = 0;
    while (si < j) {
      si += sizes[(i_end++) % nelements] / type_size;
    }
    if (si > j) {
      i_end = i_end - 1;
      j_end = sizes[i_end % nelements] / type_size - (si - j);
    } else {
      j_end = 0;
    }
    for (i = i_start; i < i_end + 1; i++) {
      j = 0;
      si = sizes[i % nelements] / type_size;
      if (i == i_start) {
        j = j_start;
        si -= j;
      }
      if (i == i_end) {
        si -= (sizes[i % nelements] / type_size - j_end);
      }
      j *= type_size;
      if (si > 0) {
        i__start = *ip;
        if (n == 0) {
          code_put_char(ip, OPCODE_MEMCPY, isdryrun);
          code_put_pointer(ip, (void *)(output_bufs[i % nelements] + j),
                           isdryrun);
          code_put_pointer(ip, (void *)(input_bufs[i % nelements] + j),
                           isdryrun);
          code_put_int(ip, si * type_size, isdryrun);
        } else {
          code_put_char(ip, OPCODE_REDUCE, isdryrun);
          code_put_char(ip, reduction_op, isdryrun);
          code_put_pointer(ip, (void *)(output_bufs[i % nelements] + j),
                           isdryrun);
          code_put_pointer(ip, (void *)(input_bufs[i % nelements] + j),
                           isdryrun);
          code_put_int(ip, si, isdryrun);
        }
        exec_single(ip, i__start, single_instruction, isdryrun);
      }
    }
    i__start = *ip;
    code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
    exec_single(ip, i__start, single_instruction, isdryrun);
  }
}

static void reduce_scatter_exec_copyin_short(
    int nelements, int type_size, char **input_bufs,
    char volatile **output_bufs, int base_size_shared, int *sizes,
    int my_lrank_row, int my_cores_per_node_row, int reduction_op, char **ip,
    int single_instruction, int isdryrun) {
  int size, add;
  int n, i, j;
  char *i_start;
  for (i = 0; i < nelements; i++) {
    if (sizes[i] > 0) {
      i_start = *ip;
      code_put_char(ip, OPCODE_MEMCPY, isdryrun);
      code_put_pointer(
          ip, (void *)(output_bufs[i] + base_size_shared * my_lrank_row),
          isdryrun);
      code_put_pointer(ip, (void *)input_bufs[i], isdryrun);
      code_put_int(ip, sizes[i], isdryrun);
      exec_single(ip, i_start, single_instruction, isdryrun);
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  for (n = 1; n < my_cores_per_node_row; n++) {
    for (i = 0; i < nelements; i++) {
      size = (sizes[i] / type_size) / my_cores_per_node_row;
      j = (sizes[i] / type_size) % my_cores_per_node_row;
      add = size * my_lrank_row;
      if (my_lrank_row < j) {
        size++;
        add += my_lrank_row;
      } else {
        add += j;
      }
      add *= type_size;
      if (size > 0) {
        i_start = *ip;
        code_put_char(ip, OPCODE_REDUCE, isdryrun);
        code_put_char(ip, reduction_op, isdryrun);
        code_put_pointer(ip, (void *)(output_bufs[i] + add), isdryrun);
        code_put_pointer(ip,
                         (void *)(output_bufs[i] + add + base_size_shared * n),
                         isdryrun);
        code_put_int(ip, size, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void reduce_scatter_exec_copyin(
    int nelements, int type_size, char **input_bufs,
    char volatile **output_bufs, int base_size_shared, int *sizes,
    int my_lrank_row, int my_cores_per_node_row, int reduction_op, int size,
    int copyin, char **ip, int single_instruction, int isdryrun) {
  if (((size > 2500) && (copyin == 0)) || (copyin == 2)) {
    reduce_scatter_exec_copyin_long(
        nelements, type_size, input_bufs, output_bufs, base_size_shared, sizes,
        my_lrank_row, my_cores_per_node_row, reduction_op, ip,
        single_instruction, isdryrun);
  } else {
    reduce_scatter_exec_copyin_short(
        nelements, type_size, input_bufs, output_bufs, base_size_shared, sizes,
        my_lrank_row, my_cores_per_node_row, reduction_op, ip,
        single_instruction, isdryrun);
  }
}

static void reduce_scatter_exec_recursive_copyin(
    int selement, int eelement, void *sendbuf, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int base_size_shared, int my_node,
    int my_lrank_row, int my_lrank_column, int my_mpi_size_row,
    int my_cores_per_node_row, int my_cores_per_node_column, int *displs_perm,
    int type_size, int reduction_op, int copyin, int recvcount, char **ip,
    int single_instruction, int isdryrun) {
  char *input_bufs[(eelement - selement + 1) * my_cores_per_node_row];
  volatile char *output_bufs[(eelement - selement + 1) * my_cores_per_node_row];
  int sizes[(eelement - selement + 1) * my_cores_per_node_row];
  int element, add, i, k, l, m;
  int size[my_cores_per_node_column];
  for (element = selement; element < eelement + 1; element++) {
    for (i = 0; i < my_cores_per_node_row; i++) {
      if (recvcount < 0) {
        add = 0;
        for (m = 0; m < my_cores_per_node_column; m++) {
          for (k = 0; k < my_cores_per_node_row; k++) {
            for (l = 0; l < element; l++) {
              add += lshmem_counts[(l * my_cores_per_node_row + k) *
                                       my_cores_per_node_column +
                                   m];
            }
          }
        }
        for (l = 0; l < my_cores_per_node_row; l++) {
          for (k = 0; k < my_lrank_column; k++) {
            add += lshmem_counts[(element * my_cores_per_node_row + l) *
                                     my_cores_per_node_column +
                                 k];
          }
        }
        for (l = 0; l < i; l++) {
          add += lshmem_counts[(element * my_cores_per_node_row + l) *
                                   my_cores_per_node_column +
                               my_lrank_column];
        }
        input_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)sendbuf) +
             displs_perm[element * my_cores_per_node_row + i] * type_size);
        output_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)my_shared_buf) + add);
        sizes[(element - selement) * my_cores_per_node_row + i] =
            lshmem_counts[(element * my_cores_per_node_row + i) *
                              my_cores_per_node_column +
                          my_lrank_column];
      } else {
        add = (my_cores_per_node_column * my_cores_per_node_row * element +
               my_cores_per_node_row * my_lrank_column + i) *
              recvcount;
        input_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)sendbuf) +
             (element * my_cores_per_node_row + i) * recvcount);
        output_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)my_shared_buf) + add);
        sizes[(element - selement) * my_cores_per_node_row + i] = recvcount;
      }
    }
  }
  for (l = 0; l < my_cores_per_node_column; l++) {
    if (recvcount < 0) {
      size[l] = 0;
      for (element = selement; element < eelement + 1; element++) {
        for (i = 0; i < my_cores_per_node_row; i++) {
          size[l] += lshmem_counts[(element * my_cores_per_node_row + i) *
                                       my_cores_per_node_column +
                                   l];
        }
      }
    } else {
      size[l] = (eelement + 1 - selement) * my_cores_per_node_row * recvcount;
    }
  }
  for (l = 1; l < my_cores_per_node_column; l++) {
    if (size[l] > size[0]) {
      size[0] = size[l];
    }
  }
  reduce_scatter_exec_copyin(
      (eelement - selement + 1) * my_cores_per_node_row, type_size, input_bufs,
      output_bufs, base_size_shared, sizes, my_lrank_row, my_cores_per_node_row,
      reduction_op, size[0], copyin, ip, single_instruction, isdryrun);
}

static void reduce_scatter_exec_recursive_copyout(
    void *recvbuf, char volatile *my_shared_buf, int volatile *lshmem_counts,
    int my_node, int my_lrank_row, int my_lrank_column,
    int my_cores_per_node_row, int my_cores_per_node_column, int recvcount,
    char **ip, int single_instruction, int isdryrun) {
  int add, i, j, k;
  char *i_start;
  if (recvcount < 0) {
    add = 0;
    for (k = 0; k < my_node; k++) {
      for (i = 0; i < my_cores_per_node_row; i++) {
        for (j = 0; j < my_cores_per_node_column; j++) {
          add += lshmem_counts[(k * my_cores_per_node_row + i) *
                                   my_cores_per_node_column +
                               j];
        }
      }
    }
    for (i = 0; i < my_cores_per_node_row; i++) {
      for (j = 0; j < my_lrank_column; j++) {
        add += lshmem_counts[(my_node * my_cores_per_node_row + i) *
                                 my_cores_per_node_column +
                             j];
      }
    }
    for (j = 0; j < my_lrank_row; j++) {
      add += lshmem_counts[(my_node * my_cores_per_node_row + j) *
                               my_cores_per_node_column +
                           my_lrank_column];
    }
  } else {
    add = (my_node * my_cores_per_node_row * my_cores_per_node_column +
           my_cores_per_node_row * my_lrank_column + my_lrank_row) *
          recvcount;
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_MEMCPY, isdryrun);
  code_put_pointer(ip, (void *)(char *)recvbuf, isdryrun);
  code_put_pointer(ip, (void *)(my_shared_buf + add), isdryrun);
  if (recvcount < 0) {
    code_put_int(
        ip,
        lshmem_counts[(my_node * my_cores_per_node_row + my_lrank_row) *
                          my_cores_per_node_column +
                      my_lrank_column],
        isdryrun);
  } else {
    code_put_int(ip, recvcount, isdryrun);
  }
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void reduce_scatter_exec_recursive_ring(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    char volatile *recv_buf_temp, int volatile *lshmem_counts,
    int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int my_lrank_node, int my_lrank_row,
    int my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs,
    int *num_ports, int num_active_ports, int *global_ranks,
    int *rank_back_perm, int *displs_perm, int reduction_op, int copyin,
    char **ip, int single_instruction, int isdryrun, int *num_comm_max,
    int allreduce, int recvcount) {
  int gbstep, igbstep, port, igbstep_max, gbstep_max;
  int add, isize, t_node, isize2, add2, t_node2;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# reduce_scatter_exec_recursive_ring %d %d\n",
           my_cores_per_node_row, my_cores_per_node_column);
  }
#endif
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  *num_comm_max = 0;
  reduce_scatter_exec_recursive_copyin(
      0, my_mpi_size_row / my_cores_per_node_row - 1, sendbuf, my_shared_buf,
      lshmem_counts, recv_buf_temp - my_shared_buf, my_node, my_lrank_row,
      my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
      my_cores_per_node_column, displs_perm, type_size, reduction_op, copyin,
      recvcount, ip, single_instruction, isdryrun);
  if (my_lrank_node == 0) {
    igbstep_max = 0;
    for (gbstep_max = 1; gbstep_max < my_mpi_size_row / my_cores_per_node_row;
         gbstep_max *= (num_ports[igbstep_max++] + 1)) {
    }
    gbstep = gbstep_max;
    for (igbstep = igbstep_max - 1; igbstep >= 0; igbstep--) {
      gbstep /= (num_ports[igbstep] + 1);
      for (port = num_ports[igbstep] - 1; port >= 0; port--) {
        allgatherv_exec_recursive_ring_send_recv_indices(
            my_node, port, num_ports[igbstep], gbstep, coarse_counts,
            coarse_displs, &t_node, 1, &isize, &add, num_active_ports,
            recvcount * my_cores_per_node_row * my_cores_per_node_column);
        allgatherv_exec_recursive_ring_send_recv_indices(
            my_node, port, num_ports[igbstep], gbstep, coarse_counts,
            coarse_displs, &t_node2, 0, &isize2, &add2, num_active_ports,
            recvcount * my_cores_per_node_row * my_cores_per_node_column);
        i_start = *ip;
        if ((isize > 0) && (isize2 > 0)) {
          code_put_char(ip, OPCODE_MPISENDRECV, isdryrun);
        } else {
          if (isize2 > 0) {
            code_put_char(ip, OPCODE_MPISEND, isdryrun);
          } else {
            if (isize > 0) {
              code_put_char(ip, OPCODE_MPIRECV, isdryrun);
            }
          }
        }
        if (isize2 > 0) {
          code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add2),
                           isdryrun);
          code_put_int(ip, isize2, isdryrun);
          if (recvcount < 0) {
            code_put_int(
                ip,
                global_ranks[rank_back_perm[t_node2] * my_cores_per_node_row *
                             my_cores_per_node_column],
                isdryrun);
          } else {
            code_put_int(ip,
                         global_ranks[t_node2 * my_cores_per_node_row *
                                      my_cores_per_node_column],
                         isdryrun);
          }
        }
        if (isize > 0) {
          code_put_pointer(ip, (void *)(((char *)recv_buf_temp) + add),
                           isdryrun);
          code_put_int(ip, isize, isdryrun);
          if (recvcount < 0) {
            code_put_int(
                ip,
                global_ranks[rank_back_perm[t_node] * my_cores_per_node_row *
                             my_cores_per_node_column],
                isdryrun);
          } else {
            code_put_int(ip,
                         global_ranks[t_node * my_cores_per_node_row *
                                      my_cores_per_node_column],
                         isdryrun);
          }
          code_put_char(ip, OPCODE_REDUCE, isdryrun);
          code_put_char(ip, reduction_op, isdryrun);
          code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                           isdryrun);
          code_put_pointer(ip, (void *)(((char *)recv_buf_temp) + add),
                           isdryrun);
          code_put_int(ip, isize / type_size, isdryrun);
        }
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
    }
  }
  if (recvbuf != my_shared_buf) {
    if (!allreduce) {
      reduce_scatter_exec_recursive_copyout(
          recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
          my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
          recvcount, ip, single_instruction, isdryrun);
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void reduce_scatter_exec_recursive(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    char volatile *recv_buf_temp, int volatile *lshmem_counts,
    int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int my_lrank_node, int my_lrank_row,
    int my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs,
    int *num_ports, int num_active_ports, int *global_ranks,
    int *rank_back_perm, int *displs_perm, int reduction_op, int copyin,
    char **ip, int single_instruction, int isdryrun, int *num_comm_max,
    int allreduce, int recvcount) {
  int gbstep, igbstep, port, i, j, igbstep_max, gbstep_max;
  int add, isize, t_node, sr_lrank, stepsizem1, add2, isize2, t_node2,
      sr_lrank2;
  int buf_offset, redsteps;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# reduce_scatter_exec_recursive %d %d\n", my_cores_per_node_row,
           my_cores_per_node_column);
  }
#endif
  buf_offset = recv_buf_temp - my_shared_buf;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  reduce_scatter_exec_recursive_copyin(
      0, my_mpi_size_row / my_cores_per_node_row - 1, sendbuf, my_shared_buf,
      lshmem_counts, recv_buf_temp - my_shared_buf, my_node, my_lrank_row,
      my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
      my_cores_per_node_column, displs_perm, type_size, reduction_op, copyin,
      recvcount, ip, single_instruction, isdryrun);
  *num_comm_max = 0;
  igbstep_max = 0;
  for (gbstep_max = 1; gbstep_max < my_mpi_size_row / my_cores_per_node_row;
       gbstep_max *= (num_ports[igbstep_max++] + 1)) {
  }
  gbstep = gbstep_max;
  for (igbstep = igbstep_max - 1; igbstep >= 0; igbstep--) {
    gbstep /= (num_ports[igbstep] + 1);
    for (port = my_lrank_node; port < num_ports[igbstep] + my_lrank_node;
         port +=
         ((num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                  : num_ports[igbstep])) {
      redsteps = (num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                         : num_ports[igbstep];
      if (port < num_ports[igbstep]) {
        if ((my_lrank_node < num_active_ports) &&
            (my_lrank_node < num_ports[igbstep])) {
          allgatherv_exec_recursive_send_recv_indices(
              my_node, port, num_ports[igbstep], gbstep, coarse_counts,
              coarse_displs, &t_node, &stepsizem1, &sr_lrank, 1, &isize, &add,
              num_active_ports,
              recvcount * my_cores_per_node_row * my_cores_per_node_column);
          allgatherv_exec_recursive_send_recv_indices(
              my_node, port, num_ports[igbstep], gbstep, coarse_counts,
              coarse_displs, &t_node2, &stepsizem1, &sr_lrank2, 0, &isize2,
              &add2, num_active_ports,
              recvcount * my_cores_per_node_row * my_cores_per_node_column);
          i_start = *ip;
          if ((isize > 0) && (isize2 > 0)) {
            code_put_char(ip, OPCODE_MPISENDRECV, isdryrun);
          } else {
            if (isize2 > 0) {
              code_put_char(ip, OPCODE_MPISEND, isdryrun);
            } else {
              if (isize > 0) {
                code_put_char(ip, OPCODE_MPIRECV, isdryrun);
              }
            }
          }
          if (isize2 > 0) {
            code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add2),
                             isdryrun);
            code_put_int(ip, isize2, isdryrun);
            if (recvcount < 0) {
              code_put_int(
                  ip,
                  global_ranks[rank_back_perm[t_node2] * my_cores_per_node_row *
                                   my_cores_per_node_column +
                               sr_lrank2],
                  isdryrun);
            } else {
              code_put_int(ip,
                           global_ranks[t_node2 * my_cores_per_node_row *
                                            my_cores_per_node_column +
                                        sr_lrank2],
                           isdryrun);
            }
          }
          if (isize > 0) {
            code_put_pointer(ip,
                             (void *)(((char *)recv_buf_temp) +
                                      my_lrank_node * buf_offset + add),
                             isdryrun);
            code_put_int(ip, isize, isdryrun);
            if (recvcount < 0) {
              code_put_int(
                  ip,
                  global_ranks[rank_back_perm[t_node] * my_cores_per_node_row *
                                   my_cores_per_node_column +
                               sr_lrank],
                  isdryrun);
            } else {
              code_put_int(ip,
                           global_ranks[t_node * my_cores_per_node_row *
                                            my_cores_per_node_column +
                                        sr_lrank],
                           isdryrun);
            }
          }
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
      }
      if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
          (num_active_ports > 1)) {
        i_start = *ip;
        code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      for (i = 0; i < redsteps; i++) {
        allgatherv_exec_recursive_send_recv_indices(
            my_node, i, num_ports[igbstep], gbstep, coarse_counts,
            coarse_displs, &t_node, &stepsizem1, &sr_lrank, 1, &isize, &add,
            num_active_ports,
            recvcount * my_cores_per_node_row * my_cores_per_node_column);
        isize2 = (isize / type_size) /
                 (my_cores_per_node_row * my_cores_per_node_column);
        j = (isize / type_size) %
            (my_cores_per_node_row * my_cores_per_node_column);
        if (my_lrank_node < j) {
          add += (isize2 * my_lrank_node + my_lrank_node) * type_size;
          isize2++;
        } else {
          add += (isize2 * my_lrank_node + j) * type_size;
        }
        if (isize2 > 0) {
          i_start = *ip;
          code_put_char(ip, OPCODE_REDUCE, isdryrun);
          code_put_char(ip, reduction_op, isdryrun);
          code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                           isdryrun);
          code_put_pointer(
              ip, (void *)(((char *)recv_buf_temp) + i * buf_offset + add),
              isdryrun);
          code_put_int(ip, isize2, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
        if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
            (num_active_ports > 1)) {
          i_start = *ip;
          code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
      }
    }
  }
  if (recvbuf != my_shared_buf) {
    if (!allreduce) {
      reduce_scatter_exec_recursive_copyout(
          recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
          my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
          recvcount, ip, single_instruction, isdryrun);
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void allgatherv_exec_bruck_copyin(
    void *sendbuf, char volatile *my_shared_buf, int volatile *lshmem_counts,
    int my_node, int my_lrank_row, int my_lrank_column,
    int my_cores_per_node_row, int my_cores_per_node_column, int recvcount,
    char **ip, int single_instruction, int isdryrun) {
  int add, i, j;
  char *i_start;
  if (recvcount < 0) {
    add = 0;
    for (i = 0; i < my_cores_per_node_row; i++) {
      for (j = 0; j < my_lrank_column; j++) {
        add += lshmem_counts[(my_node * my_cores_per_node_row + i) *
                                 my_cores_per_node_column +
                             j];
      }
    }
    for (j = 0; j < my_lrank_row; j++) {
      add += lshmem_counts[(my_node * my_cores_per_node_row + j) *
                               my_cores_per_node_column +
                           my_lrank_column];
    }
  } else {
    add = (my_cores_per_node_row * my_lrank_column + my_lrank_row) * recvcount;
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_MEMCPY, isdryrun);
  code_put_pointer(ip, (void *)(my_shared_buf + add), isdryrun);
  code_put_pointer(ip, (void *)(char *)sendbuf, isdryrun);
  if (recvcount < 0) {
    code_put_int(
        ip,
        lshmem_counts[(my_node * my_cores_per_node_row + my_lrank_row) *
                          my_cores_per_node_column +
                      my_lrank_column],
        isdryrun);
  } else {
    code_put_int(ip, recvcount, isdryrun);
  }
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void allgatherv_exec_bruck_copyout(
    int element, void *recvbuf, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int my_node, int my_lrank_row,
    int my_lrank_column, int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int *displs_perm, int type_size,
    int recvcount, char **ip, int single_instruction, int isdryrun) {
  int add, i, k, l, m;
  char *i_start;
  for (i = 0; i < my_cores_per_node_row; i++) {
    i_start = *ip;
    if (recvcount < 0) {
      add = 0;
      for (m = 0; m < my_cores_per_node_column; m++) {
        for (k = 0; k < my_cores_per_node_row; k++) {
          for (l = 0; l < element; l++) {
            add += lshmem_counts
                [((l * my_cores_per_node_row + k) * my_cores_per_node_column +
                  m +
                  my_node * my_cores_per_node_row * my_cores_per_node_column) %
                 (my_mpi_size_row * my_cores_per_node_column)];
          }
        }
      }
      for (l = 0; l < my_cores_per_node_row; l++) {
        for (k = 0; k < my_lrank_column; k++) {
          add += lshmem_counts[((element * my_cores_per_node_row + l) *
                                    my_cores_per_node_column +
                                k +
                                my_node * my_cores_per_node_row *
                                    my_cores_per_node_column) %
                               (my_mpi_size_row * my_cores_per_node_column)];
        }
      }
      for (l = 0; l < i; l++) {
        add += lshmem_counts
            [((element * my_cores_per_node_row + l) * my_cores_per_node_column +
              my_lrank_column +
              my_node * my_cores_per_node_row * my_cores_per_node_column) %
             (my_mpi_size_row * my_cores_per_node_column)];
      }
      code_put_char(ip, OPCODE_MEMCPY, isdryrun);
      code_put_pointer(
          ip,
          (void *)(((char *)recvbuf) +
                   displs_perm[((element + my_node) %
                                (my_mpi_size_row / my_cores_per_node_row)) *
                                   my_cores_per_node_row +
                               i] *
                       type_size),
          isdryrun);
      code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add), isdryrun);
      code_put_int(ip,
                   lshmem_counts[((element * my_cores_per_node_row + i) *
                                      my_cores_per_node_column +
                                  my_lrank_column +
                                  my_node * my_cores_per_node_row *
                                      my_cores_per_node_column) %
                                 (my_mpi_size_row * my_cores_per_node_column)],
                   isdryrun);
    } else {
      add = (my_cores_per_node_column * my_cores_per_node_row * element +
             my_cores_per_node_row * my_lrank_column + i) *
            recvcount;
      code_put_char(ip, OPCODE_MEMCPY, isdryrun);
      code_put_pointer(ip,
                       (void *)(((char *)recvbuf) +
                                (((element + my_node) %
                                  (my_mpi_size_row / my_cores_per_node_row)) *
                                     my_cores_per_node_row +
                                 i) *
                                    recvcount),
                       isdryrun);
      code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add), isdryrun);
      code_put_int(ip, recvcount, isdryrun);
    }
    exec_single(ip, i_start, single_instruction, isdryrun);
  }
}

static void allgatherv_exec_bruck(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int my_lrank_node, int my_lrank_row,
    int my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs,
    int *num_ports, int *num_parallel, int num_active_ports, int *global_ranks,
    int *rank_back_perm, int *displs_perm, char **ip, int single_instruction,
    int isdryrun, int *num_comm_max, int num_throttle, int allreduce,
    int recvcount) {
  int copy_list[my_mpi_size_row / my_cores_per_node_row];
  int gbstep, igbstep, port, num_comm, throttle, i;
  int add, isize, port_port, port_parallel;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# allgatherv_exec_bruck %d %d\n", my_cores_per_node_row,
           my_cores_per_node_column);
  }
#endif
  num_throttle *= num_active_ports;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    copy_list[i] = 0;
  }
  *num_comm_max = 0;
  num_comm = 0;
  if (my_mpi_size_row / my_cores_per_node_row == 1) {
    if (!allreduce) {
      allgatherv_exec_bruck_copyin(
          sendbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
          my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
          recvcount, ip, single_instruction, isdryrun);
    }
    copy_list[0] = 2;
  }
  igbstep = 0;
  for (gbstep = 1; gbstep < my_mpi_size_row / my_cores_per_node_row;
       gbstep *= (num_ports[igbstep++] + 1)) {
    for (throttle = 0; throttle < my_mpi_size_row / my_cores_per_node_row;
         throttle += num_throttle) {
      num_comm = 0;
      if ((my_lrank_node < num_active_ports) &&
          (my_lrank_node < num_ports[igbstep] * num_parallel[igbstep])) {
        for (port = my_lrank_node;
             port < num_ports[igbstep] * num_parallel[igbstep];
             port +=
             ((num_active_ports < num_ports[igbstep] * num_parallel[igbstep])
                  ? num_active_ports
                  : num_ports[igbstep] * num_parallel[igbstep])) {
          port_port = (port - my_lrank_node) / num_parallel[igbstep] +
                      my_lrank_node / num_parallel[igbstep];
          port_parallel = my_lrank_node % num_parallel[igbstep];
          int mgbstep = gbstep;
          if (mgbstep + mgbstep * (port_port + 1) >
              my_mpi_size_row / my_cores_per_node_row)
            mgbstep = my_mpi_size_row / my_cores_per_node_row -
                      mgbstep * (port_port + 1);
          if (recvcount < 0) {
            isize = 0;
            for (i = 0; i < mgbstep; i++) {
              isize += coarse_counts[(i + gbstep * (port_port + 1) + my_node) %
                                     (my_mpi_size_row / my_cores_per_node_row)];
            }
            add = 0;
            for (i = 0; i < gbstep * (port_port + 1); i++) {
              add += coarse_counts[(i + my_node) %
                                   (my_mpi_size_row / my_cores_per_node_row)];
            }
          } else {
            isize = mgbstep * my_cores_per_node_row * my_cores_per_node_column *
                    recvcount;
            add = gbstep * (port_port + 1) * my_cores_per_node_row *
                  my_cores_per_node_column * recvcount;
          }
          add += (isize / num_parallel[igbstep]) * port_parallel;
          if (port_parallel < num_parallel[igbstep] - 1) {
            isize /= num_parallel[igbstep];
          } else {
            isize = isize - (num_parallel[igbstep] - 1) *
                                (isize / num_parallel[igbstep]);
          }
          if (((port_port - throttle) < num_throttle) &&
              ((port_port - throttle) >= 0)) {
            if (isize > 0) {
              i_start = *ip;
              code_put_char(ip, OPCODE_MPIIRECV, isdryrun);
              code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                               isdryrun);
              code_put_int(ip, isize, isdryrun);
              if ((recvcount < 0) && (rank_back_perm != NULL)) {
                code_put_int(
                    ip,
                    global_ranks[(rank_back_perm[(my_node +
                                                  (port_port + 1) * gbstep) %
                                                 (my_mpi_size_row /
                                                  my_cores_per_node_row)] *
                                      my_cores_per_node_row +
                                  my_lrank_row) *
                                     my_cores_per_node_column +
                                 my_lrank_column],
                    isdryrun);
              } else {
                code_put_int(
                    ip,
                    global_ranks[(((my_node + (port_port + 1) * gbstep) %
                                   (my_mpi_size_row / my_cores_per_node_row)) *
                                      my_cores_per_node_row +
                                  my_lrank_row) *
                                     my_cores_per_node_column +
                                 my_lrank_column],
                    isdryrun);
              }
              code_put_pointer(
                  ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                  isdryrun);
              exec_single(ip, i_start, single_instruction, isdryrun);
              num_comm++;
            }
          }
        }
      }
      for (port = 0; port < num_ports[igbstep]; port++) {
        if (((port - throttle) < num_throttle) && ((port - throttle) >= 0)) {
          int mgbstep = gbstep;
          if (mgbstep + mgbstep * (port + 1) >
              my_mpi_size_row / my_cores_per_node_row) {
            mgbstep =
                my_mpi_size_row / my_cores_per_node_row - mgbstep * (port + 1);
          }
          for (i = gbstep * (port + 1); i < gbstep * (port + 1) + mgbstep;
               i++) {
            copy_list[i] = 1;
          }
        }
      }
      if ((gbstep == 1) && (throttle == 0)) {
        if (!allreduce) {
          allgatherv_exec_bruck_copyin(
              sendbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
              my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
              recvcount, ip, single_instruction, isdryrun);
        }
        copy_list[0] = 2;
      }
      if ((my_lrank_node < num_active_ports) &&
          (my_lrank_node < num_ports[igbstep] * num_parallel[igbstep])) {
        for (port = my_lrank_node;
             port < num_ports[igbstep] * num_parallel[igbstep];
             port +=
             ((num_active_ports < num_ports[igbstep] * num_parallel[igbstep])
                  ? num_active_ports
                  : num_ports[igbstep] * num_parallel[igbstep])) {
          port_port = (port - my_lrank_node) / num_parallel[igbstep] +
                      my_lrank_node / num_parallel[igbstep];
          port_parallel = my_lrank_node % num_parallel[igbstep];
          int mgbstep = gbstep;
          if (mgbstep + mgbstep * (port_port + 1) >
              my_mpi_size_row / my_cores_per_node_row)
            mgbstep = my_mpi_size_row / my_cores_per_node_row -
                      mgbstep * (port_port + 1);
          if (recvcount < 0) {
            isize = 0;
            for (i = 0; i < mgbstep; i++) {
              isize += coarse_counts[(i + my_node) %
                                     (my_mpi_size_row / my_cores_per_node_row)];
            }
            add = 0;
          } else {
            isize = mgbstep * my_cores_per_node_row * my_cores_per_node_column *
                    recvcount;
            add = 0;
          }
          add += (isize / num_parallel[igbstep]) * port_parallel;
          if (port_parallel < num_parallel[igbstep] - 1) {
            isize /= num_parallel[igbstep];
          } else {
            isize = isize - (num_parallel[igbstep] - 1) *
                                (isize / num_parallel[igbstep]);
          }
          if (((port_port - throttle) < num_throttle) &&
              ((port_port - throttle) >= 0)) {
            if (isize > 0) {
              i_start = *ip;
              code_put_char(ip, OPCODE_MPIISEND, isdryrun);
              code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                               isdryrun);
              code_put_int(ip, isize, isdryrun);
              if ((recvcount < 0) && (rank_back_perm != NULL)) {
                code_put_int(
                    ip,
                    global_ranks
                        [(rank_back_perm
                                  [(my_mpi_size_row / my_cores_per_node_row +
                                    my_node - (port_port + 1) * gbstep) %
                                   (my_mpi_size_row / my_cores_per_node_row)] *
                              my_cores_per_node_row +
                          my_lrank_row) *
                             my_cores_per_node_column +
                         my_lrank_column],
                    isdryrun);
              } else {
                code_put_int(
                    ip,
                    global_ranks[(((my_mpi_size_row / my_cores_per_node_row +
                                    my_node - (port_port + 1) * gbstep) %
                                   (my_mpi_size_row / my_cores_per_node_row)) *
                                      my_cores_per_node_row +
                                  my_lrank_row) *
                                     my_cores_per_node_column +
                                 my_lrank_column],
                    isdryrun);
              }
              code_put_pointer(
                  ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                  isdryrun);
              exec_single(ip, i_start, single_instruction, isdryrun);
              num_comm++;
            }
          }
        }
      }
      for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
        if (copy_list[i] == 2) {
          allgatherv_exec_bruck_copyout(
              i, recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
              my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
              my_cores_per_node_column, displs_perm, type_size, recvcount, ip,
              single_instruction, isdryrun);
          copy_list[i] = 3;
        }
      }
      if (num_comm > 0) {
        i_start = *ip;
        code_put_char(ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(ip, num_comm, isdryrun);
        code_put_pointer(ip, (void *)locmem, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
        if (copy_list[i] == 1) {
          copy_list[i] = 2;
        }
      }
      code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
      if (num_comm > *num_comm_max) {
        *num_comm_max = num_comm;
      }
      num_comm = 0;
    }
  }
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    if (copy_list[i] == 2) {
      allgatherv_exec_bruck_copyout(
          i, recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
          my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
          my_cores_per_node_column, displs_perm, type_size, recvcount, ip,
          single_instruction, isdryrun);
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void allreduce_exec_copyin(
    void *sendbuf, char volatile *my_shared_buf, int base_size_shared,
    int count, int my_lrank_row, int my_lrank_column, int my_cores_per_node_row,
    int my_cores_per_node_column, int reduction_op, int type_size, int osize,
    int copyin, char **ip, int single_instruction, int isdryrun) {
  int size;
  size = count * type_size;
  reduce_scatter_exec_copyin(1, type_size, (char **)(&sendbuf), &my_shared_buf,
                             base_size_shared, &size, my_lrank_row,
                             my_cores_per_node_row, reduction_op, osize, copyin,
                             ip, single_instruction, isdryrun);
}

static void allreduce_exec_copyout(void *recvbuf, char volatile *my_shared_buf,
                                   int line, int *counts, int my_lrank_column,
                                   int type_size, char **ip,
                                   int single_instruction, int isdryrun) {
  int add, size, i;
  char *i_start;
  size = counts[my_lrank_column] * type_size;
  add = 0;
  for (i = 0; i < my_lrank_column; i++) {
    add += counts[i] * type_size;
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_MEMCPY, isdryrun);
  code_put_pointer(ip, (void *)(char *)recvbuf, isdryrun);
  code_put_pointer(ip, (void *)(my_shared_buf + line * size + add), isdryrun);
  code_put_int(ip, size, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void lines_required(int *num_ports, int *num_ports_limit, int nlines,
                           int *line_order) {
  int line_required[nlines];
  int igbstep, i, j, k;
  for (i = 0; i < nlines; i++) {
    line_required[i] = 0;
  }
  j = 1;
  igbstep = 0;
  while (j < num_ports_limit[igbstep] + 1) {
    for (k = 0; k < num_ports[igbstep]; k++) {
      if (j * (k + 1) - 1 < nlines) {
        line_required[j * (k + 1) - 1] = 1;
      }
    }
    j *= (num_ports[igbstep] + 1);
    igbstep++;
  }
  igbstep--;
  j = num_ports_limit[igbstep] + 1;
  while (!line_required[j - 1]) {
    k = 0;
    while (!line_required[j - 1 - k]) {
      k = k + 1;
    }
    line_required[j - 1] = 1;
    j = k;
  }
  for (i = 0; i < nlines; i++) {
    line_order[i] = i;
  }
  for (i = 0; i < nlines; i++) {
    if (!line_required[i]) {
      line_order[i] = -1;
      for (j = i + 1; j < nlines; j++) {
        line_order[j]--;
      }
    }
  }
}

static void allreduce_exec_bruck(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    int base_size_shared, int *counts, int my_mpi_size_row,
    int my_cores_per_node_row, int my_cores_per_node_column, int my_lrank_node,
    int my_lrank_row, int my_lrank_column, int my_node, int coarse_count,
    int *num_ports, int *num_ports_limit, int num_active_ports,
    int *global_ranks, int reduction_op, int copyin, char **ip,
    int single_instruction, int isdryrun, int *num_comm_max, int num_throttle) {
  int line_order[my_mpi_size_row / my_cores_per_node_row];
  int gbstep, igbstep, port, num_comm, throttle, i, j, mgbstep, lgbstep;
  int add, isize, isize2, osize, add2;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# allreduce_exec_bruck %d %d\n", my_cores_per_node_row,
           my_cores_per_node_column);
  }
#endif
  num_throttle *= num_active_ports;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  *num_comm_max = 0;
  num_comm = 0;
  osize = 0;
  for (i = 0; i < my_cores_per_node_column; i++) {
    if (counts[i] * type_size > osize) {
      osize = counts[i] * type_size;
    }
  }
  if (my_mpi_size_row / my_cores_per_node_row == 1) {
    allreduce_exec_copyin(sendbuf, my_shared_buf, base_size_shared,
                          counts[my_lrank_column], my_lrank_row,
                          my_lrank_column, my_cores_per_node_row,
                          my_cores_per_node_column, reduction_op, type_size,
                          osize, copyin, ip, single_instruction, isdryrun);
  }
  lines_required(num_ports, num_ports_limit,
                 my_mpi_size_row / my_cores_per_node_row, line_order);
  igbstep = 0;
  gbstep = lgbstep = 1;
  while (gbstep < my_mpi_size_row / my_cores_per_node_row) {
    for (throttle = 0; throttle < my_mpi_size_row / my_cores_per_node_row;
         throttle += num_throttle) {
      num_comm = 0;
      if ((my_lrank_node < num_active_ports) &&
          (my_lrank_node < num_ports[igbstep])) {
        for (port = my_lrank_node; port < num_ports[igbstep];
             port +=
             ((num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                      : num_ports[igbstep])) {
          mgbstep = lgbstep;
          if (mgbstep + mgbstep * (port + 1) > num_ports_limit[igbstep] + 1)
            mgbstep = num_ports_limit[igbstep] + 1 - mgbstep * (port + 1);
          if (mgbstep > 0) {
            isize = mgbstep;
            add = lgbstep * (port + 1);
            j = isize;
            for (i = add; i < add + j; i++) {
              if (line_order[i] < 0) {
                isize--;
              }
            }
            while (line_order[add] < 0) {
              add++;
            }
            isize *= coarse_count;
            add = line_order[add] * coarse_count;
            if (((port - throttle) < num_throttle) &&
                ((port - throttle) >= 0)) {
              isize *= type_size;
              add *= type_size;
              if (isize > 0) {
                i_start = *ip;
                code_put_char(ip, OPCODE_MPIIRECV, isdryrun);
                code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                                 isdryrun);
                code_put_int(ip, isize, isdryrun);
                code_put_int(
                    ip,
                    global_ranks[((my_node + (port + 1) * gbstep * lgbstep) %
                                      (my_mpi_size_row /
                                       my_cores_per_node_row) *
                                      my_cores_per_node_row +
                                  my_lrank_row) *
                                     my_cores_per_node_column +
                                 my_lrank_column],
                    isdryrun);
                code_put_pointer(
                    ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                    isdryrun);
                exec_single(ip, i_start, single_instruction, isdryrun);
                num_comm++;
              }
            }
          }
        }
      }
      if ((gbstep == 1) && (lgbstep == 1) && (throttle == 0)) {
        allreduce_exec_copyin(sendbuf, my_shared_buf, base_size_shared,
                              counts[my_lrank_column], my_lrank_row,
                              my_lrank_column, my_cores_per_node_row,
                              my_cores_per_node_column, reduction_op, type_size,
                              osize, copyin, ip, single_instruction, isdryrun);
      }
      if ((my_lrank_node < num_active_ports) &&
          (my_lrank_node < num_ports[igbstep])) {
        for (port = my_lrank_node; port < num_ports[igbstep];
             port +=
             ((num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                      : num_ports[igbstep])) {
          mgbstep = lgbstep;
          if (mgbstep + mgbstep * (port + 1) > num_ports_limit[igbstep] + 1)
            mgbstep = num_ports_limit[igbstep] + 1 - mgbstep * (port + 1);
          if (mgbstep > 0) {
            isize = mgbstep;
            add = lgbstep * (port + 1);
            j = isize;
            for (i = add; i < add + j; i++) {
              if (line_order[i] < 0) {
                isize--;
              }
            }
            while (line_order[add] < 0) {
              add++;
            }
            add = line_order[add - lgbstep * (port + 1)] * coarse_count;
            if (((port - throttle) < num_throttle) &&
                ((port - throttle) >= 0)) {
              isize *= coarse_count;
              isize *= type_size;
              add *= type_size;
              if (isize > 0) {
                i_start = *ip;
                code_put_char(ip, OPCODE_MPIISEND, isdryrun);
                code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                                 isdryrun);
                code_put_int(ip, isize, isdryrun);
                code_put_int(
                    ip,
                    global_ranks[((my_mpi_size_row / my_cores_per_node_row +
                                   my_node - (port + 1) * gbstep * lgbstep) %
                                      (my_mpi_size_row /
                                       my_cores_per_node_row) *
                                      my_cores_per_node_row +
                                  my_lrank_row) *
                                     my_cores_per_node_column +
                                 my_lrank_column],
                    isdryrun);
                code_put_pointer(
                    ip, (void *)(locmem + num_comm * sizeof(MPI_Request)),
                    isdryrun);
                exec_single(ip, i_start, single_instruction, isdryrun);
                num_comm++;
              }
            }
          }
        }
      }
      if (num_comm > 0) {
        i_start = *ip;
        code_put_char(ip, OPCODE_MPIWAITALL, isdryrun);
        code_put_int(ip, num_comm, isdryrun);
        code_put_pointer(ip, (void *)locmem, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
          (num_active_ports > 1)) {
        i_start = *ip;
        code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      if (throttle + num_throttle >= my_mpi_size_row / my_cores_per_node_row) {
        for (port = 0; port < num_ports[igbstep]; port++) {
          mgbstep = lgbstep;
          if (mgbstep + mgbstep * (port + 1) > num_ports_limit[igbstep] + 1)
            mgbstep = num_ports_limit[igbstep] + 1 - mgbstep * (port + 1);
          if (mgbstep > 0) {
            isize = mgbstep;
          } else {
            isize = 0;
          }
          add = lgbstep * (port + 1);
          for (i = add; i < add + isize; i++) {
            if (line_order[i] >= 0) {
              add2 = 0;
              isize2 = coarse_count /
                       (my_cores_per_node_row * my_cores_per_node_column);
              j = coarse_count %
                  (my_cores_per_node_row * my_cores_per_node_column);
              if (my_lrank_node < j) {
                add2 += (isize2 * my_lrank_node + my_lrank_node) * type_size;
                isize2++;
              } else {
                add2 += (isize2 * my_lrank_node + j) * type_size;
              }
              if (isize2 > 0) {
                i_start = *ip;
                code_put_char(ip, OPCODE_REDUCE, isdryrun);
                code_put_char(ip, reduction_op, isdryrun);
                code_put_pointer(
                    ip,
                    (void *)(((char *)my_shared_buf) +
                             line_order[i] * coarse_count * type_size + add2),
                    isdryrun);
                code_put_pointer(
                    ip,
                    (void *)(((char *)my_shared_buf) +
                             line_order[add - 1] * coarse_count * type_size +
                             add2),
                    isdryrun);
                code_put_int(ip, isize2, isdryrun);
                exec_single(ip, i_start, single_instruction, isdryrun);
              }
            }
          }
        }
        if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
            (num_active_ports > 1)) {
          i_start = *ip;
          code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
      }
      if (num_comm > *num_comm_max) {
        *num_comm_max = num_comm;
      }
      num_comm = 0;
    }
    if (lgbstep * (num_ports[igbstep] + 1) >= num_ports_limit[igbstep] + 1) {
      if (gbstep * (num_ports_limit[igbstep] + 1) <
          my_mpi_size_row / my_cores_per_node_row) {
        add = 0;
        isize2 =
            coarse_count / (my_cores_per_node_row * my_cores_per_node_column);
        j = coarse_count % (my_cores_per_node_row * my_cores_per_node_column);
        if (my_lrank_node < j) {
          add += (isize2 * my_lrank_node + my_lrank_node) * type_size;
          isize2++;
        } else {
          add += (isize2 * my_lrank_node + j) * type_size;
        }
        if (isize2 > 0) {
          isize2 *= type_size;
          i_start = *ip;
          code_put_char(ip, OPCODE_MEMCPY, isdryrun);
          code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                           isdryrun);
          code_put_pointer(ip,
                           (void *)(((char *)my_shared_buf) +
                                    line_order[num_ports_limit[igbstep]] *
                                        coarse_count * type_size +
                                    add),
                           isdryrun);
          code_put_int(ip, isize2, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
        if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
            (num_active_ports > 1)) {
          i_start = *ip;
          code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
        lines_required(num_ports + igbstep + 1, num_ports_limit + igbstep + 1,
                       my_mpi_size_row / my_cores_per_node_row, line_order);
      }
      lgbstep = 1;
      gbstep *= (num_ports_limit[igbstep] + 1);
    } else {
      lgbstep *= (num_ports[igbstep] + 1);
      for (port = 0; port < num_ports[igbstep + 1]; port++) {
        mgbstep = lgbstep;
        if (mgbstep + mgbstep * (port + 1) > num_ports_limit[igbstep + 1] + 1)
          mgbstep = num_ports_limit[igbstep + 1] + 1 - mgbstep * (port + 1);
        if (mgbstep > 0) {
          isize = mgbstep;
          add = lgbstep * (port + 1);
          j = isize;
          for (i = add; i < add + j; i++) {
            if (line_order[i] < 0) {
              isize--;
            }
          }
          while (line_order[add] < 0) {
            add++;
          }
          if (isize > 1) {
            add2 = add + 1;
            while (line_order[add2] < 0) {
              add2++;
            }
            i = line_order[add - lgbstep * (port + 1)];
            add2 = line_order[add2 - lgbstep * (port + 1)];
            if (i + 1 < add2) {
              line_order[add - lgbstep * (port + 1)] = add2 - 1;
              add = 0;
              isize2 = coarse_count /
                       (my_cores_per_node_row * my_cores_per_node_column);
              j = coarse_count %
                  (my_cores_per_node_row * my_cores_per_node_column);
              if (my_lrank_node < j) {
                add += (isize2 * my_lrank_node + my_lrank_node) * type_size;
                isize2++;
              } else {
                add += (isize2 * my_lrank_node + j) * type_size;
              }
              if (isize2 > 0) {
                isize2 *= type_size;
                i_start = *ip;
                code_put_char(ip, OPCODE_MEMCPY, isdryrun);
                code_put_pointer(
                    ip,
                    (void *)(((char *)my_shared_buf) +
                             (add2 - 1) * coarse_count * type_size + add),
                    isdryrun);
                code_put_pointer(ip,
                                 (void *)(((char *)my_shared_buf) +
                                          i * coarse_count * type_size + add),
                                 isdryrun);
                code_put_int(ip, isize2, isdryrun);
                exec_single(ip, i_start, single_instruction, isdryrun);
              }
              port = num_ports[igbstep + 1];
            }
          }
        }
      }
      if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
          (num_active_ports > 1)) {
        i_start = *ip;
        code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
    }
    igbstep++;
  }
  allreduce_exec_copyout(
      recvbuf, my_shared_buf, line_order[num_ports_limit[igbstep - 1]], counts,
      my_lrank_column, type_size, ip, single_instruction, isdryrun);
  if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
      (num_active_ports > 1)) {
    i_start = *ip;
    code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
    exec_single(ip, i_start, single_instruction, isdryrun);
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void reduce_scatter_exec_bruck_copyin(
    int selement, int eelement, void *sendbuf, char volatile *my_shared_buf,
    int volatile *lshmem_counts, int base_size_shared, int my_node,
    int my_lrank_row, int my_lrank_column, int my_mpi_size_row,
    int my_cores_per_node_row, int my_cores_per_node_column, int *displs_perm,
    int type_size, int reduction_op, int copyin, int recvcount, char **ip,
    int single_instruction, int isdryrun) {
  char *input_bufs[(eelement - selement + 1) * my_cores_per_node_row];
  volatile char *output_bufs[(eelement - selement + 1) * my_cores_per_node_row];
  int sizes[(eelement - selement + 1) * my_cores_per_node_row];
  int element, add, i, k, l, m;
  int size[my_cores_per_node_column];
  for (element = selement; element < eelement + 1; element++) {
    for (i = 0; i < my_cores_per_node_row; i++) {
      if (recvcount < 0) {
        add = 0;
        for (m = 0; m < my_cores_per_node_column; m++) {
          for (k = 0; k < my_cores_per_node_row; k++) {
            for (l = 0; l < element; l++) {
              add +=
                  lshmem_counts[((l * my_cores_per_node_row + k) *
                                     my_cores_per_node_column +
                                 m +
                                 my_node * my_cores_per_node_row *
                                     my_cores_per_node_column) %
                                (my_mpi_size_row * my_cores_per_node_column)];
            }
          }
        }
        for (l = 0; l < my_cores_per_node_row; l++) {
          for (k = 0; k < my_lrank_column; k++) {
            add += lshmem_counts[((element * my_cores_per_node_row + l) *
                                      my_cores_per_node_column +
                                  k +
                                  my_node * my_cores_per_node_row *
                                      my_cores_per_node_column) %
                                 (my_mpi_size_row * my_cores_per_node_column)];
          }
        }
        for (l = 0; l < i; l++) {
          add += lshmem_counts[((element * my_cores_per_node_row + l) *
                                    my_cores_per_node_column +
                                my_lrank_column +
                                my_node * my_cores_per_node_row *
                                    my_cores_per_node_column) %
                               (my_mpi_size_row * my_cores_per_node_column)];
        }
        input_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)sendbuf) +
             displs_perm[((element + my_node) %
                          (my_mpi_size_row / my_cores_per_node_row)) *
                             my_cores_per_node_row +
                         i] *
                 type_size);
        output_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)my_shared_buf) + add);
        sizes[(element - selement) * my_cores_per_node_row + i] = lshmem_counts
            [((element * my_cores_per_node_row + i) * my_cores_per_node_column +
              my_lrank_column +
              my_node * my_cores_per_node_row * my_cores_per_node_column) %
             (my_mpi_size_row * my_cores_per_node_column)];
      } else {
        add = (my_cores_per_node_column * my_cores_per_node_row * element +
               my_cores_per_node_row * my_lrank_column + i) *
              recvcount;
        input_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)sendbuf) + (((element + my_node) %
                                   (my_mpi_size_row / my_cores_per_node_row)) *
                                      my_cores_per_node_row +
                                  i) *
                                     recvcount);
        output_bufs[(element - selement) * my_cores_per_node_row + i] =
            (((char *)my_shared_buf) + add);
        sizes[(element - selement) * my_cores_per_node_row + i] = recvcount;
      }
    }
  }
  for (l = 0; l < my_cores_per_node_column; l++) {
    if (recvcount < 0) {
      size[l] = 0;
      for (element = selement; element < eelement + 1; element++) {
        for (i = 0; i < my_cores_per_node_row; i++) {
          size[l] +=
              lshmem_counts[((element * my_cores_per_node_row + i) *
                                 my_cores_per_node_column +
                             l +
                             my_node * my_cores_per_node_row *
                                 my_cores_per_node_column) %
                            (my_mpi_size_row * my_cores_per_node_column)];
        }
      }
    } else {
      size[l] = (eelement + 1 - selement) * my_cores_per_node_row * recvcount;
    }
  }
  for (l = 1; l < my_cores_per_node_column; l++) {
    if (size[l] > size[0]) {
      size[0] = size[l];
    }
  }
  reduce_scatter_exec_copyin(
      (eelement - selement + 1) * my_cores_per_node_row, type_size, input_bufs,
      output_bufs, base_size_shared, sizes, my_lrank_row, my_cores_per_node_row,
      reduction_op, size[0], copyin, ip, single_instruction, isdryrun);
}

static void reduce_scatter_exec_bruck_copyout(
    void *recvbuf, char volatile *my_shared_buf, int volatile *lshmem_counts,
    int my_node, int my_lrank_row, int my_lrank_column,
    int my_cores_per_node_row, int my_cores_per_node_column, int recvcount,
    char **ip, int single_instruction, int isdryrun) {
  int add, i, j;
  char *i_start;
  if (recvcount < 0) {
    add = 0;
    for (i = 0; i < my_cores_per_node_row; i++) {
      for (j = 0; j < my_lrank_column; j++) {
        add += lshmem_counts[(my_node * my_cores_per_node_row + i) *
                                 my_cores_per_node_column +
                             j];
      }
    }
    for (j = 0; j < my_lrank_row; j++) {
      add += lshmem_counts[(my_node * my_cores_per_node_row + j) *
                               my_cores_per_node_column +
                           my_lrank_column];
    }
  } else {
    add = (my_cores_per_node_row * my_lrank_column + my_lrank_row) * recvcount;
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_MEMCPY, isdryrun);
  code_put_pointer(ip, (void *)(char *)recvbuf, isdryrun);
  code_put_pointer(ip, (void *)(my_shared_buf + add), isdryrun);
  if (recvcount < 0) {
    code_put_int(
        ip,
        lshmem_counts[(my_node * my_cores_per_node_row + my_lrank_row) *
                          my_cores_per_node_column +
                      my_lrank_column],
        isdryrun);
  } else {
    code_put_int(ip, recvcount, isdryrun);
  }
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static void reduce_scatter_exec_bruck(
    void *sendbuf, void *recvbuf, int type_size, char volatile *my_shared_buf,
    char volatile *recv_buf_temp, int volatile *lshmem_counts,
    int my_mpi_size_row, int my_cores_per_node_row,
    int my_cores_per_node_column, int my_lrank_node, int my_lrank_row,
    int my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs,
    int *num_ports, int *num_parallel, int num_active_ports, int *global_ranks,
    int *rank_back_perm, int *displs_perm, int reduction_op, int copyin,
    char **ip, int single_instruction, int isdryrun, int *num_comm_max,
    int allreduce, int recvcount) {
  int gbstep, igbstep, port, i, j, gbstep_max, igbstep_max;
  int add, isize, add2, isize2, mgbstep;
  int buf_offset, redsteps;
  char *i_start;
#ifdef VERBOSE
  if (isdryrun && (my_node == 0) && (my_lrank_node == 0)) {
    printf("# reduce_scatter_exec_bruck %d %d\n", my_cores_per_node_row,
           my_cores_per_node_column);
  }
#endif
  buf_offset = recv_buf_temp - my_shared_buf;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, my_cores_per_node_row * my_cores_per_node_column, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  *num_comm_max = 0;
  reduce_scatter_exec_bruck_copyin(
      0, my_mpi_size_row / my_cores_per_node_row - 1, sendbuf, my_shared_buf,
      lshmem_counts, recv_buf_temp - my_shared_buf, my_node, my_lrank_row,
      my_lrank_column, my_mpi_size_row, my_cores_per_node_row,
      my_cores_per_node_column, displs_perm, type_size, reduction_op, copyin,
      recvcount, ip, single_instruction, isdryrun);
  igbstep_max = 0;
  for (gbstep_max = 1; gbstep_max < my_mpi_size_row / my_cores_per_node_row;
       gbstep_max *= (num_ports[igbstep_max++] + 1)) {
  }
  gbstep = gbstep_max;
  for (igbstep = igbstep_max - 1; igbstep >= 0; igbstep--) {
    gbstep /= (num_ports[igbstep] + 1);
    for (port = my_lrank_node; port < num_ports[igbstep] + my_lrank_node;
         port +=
         ((num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                  : num_ports[igbstep])) {
      redsteps = (num_active_ports < num_ports[igbstep]) ? num_active_ports
                                                         : num_ports[igbstep];
      if (port < num_ports[igbstep]) {
        if ((my_lrank_node < num_active_ports) &&
            (my_lrank_node < num_ports[igbstep])) {
          mgbstep = gbstep;
          if (mgbstep + mgbstep * (port + 1) >
              my_mpi_size_row / my_cores_per_node_row)
            mgbstep =
                my_mpi_size_row / my_cores_per_node_row - mgbstep * (port + 1);
          if (recvcount < 0) {
            isize = 0;
            for (i = 0; i < mgbstep; i++) {
              isize += coarse_counts[(i + my_node) %
                                     (my_mpi_size_row / my_cores_per_node_row)];
            }
            add = 0;
            isize2 = 0;
            for (i = 0; i < mgbstep; i++) {
              isize2 +=
                  coarse_counts[(i + gbstep * (port + 1) + my_node) %
                                (my_mpi_size_row / my_cores_per_node_row)];
            }
            add2 = 0;
            for (i = 0; i < gbstep * (port + 1); i++) {
              add2 += coarse_counts[(i + my_node) %
                                    (my_mpi_size_row / my_cores_per_node_row)];
            }
          } else {
            isize = mgbstep * my_cores_per_node_row * my_cores_per_node_column *
                    recvcount;
            add = 0;
            isize2 = mgbstep * my_cores_per_node_row *
                     my_cores_per_node_column * recvcount;
            add2 = gbstep * (port + 1) * my_cores_per_node_row *
                   my_cores_per_node_column * recvcount;
          }
          i_start = *ip;
          if ((isize > 0) && (isize2 > 0)) {
            code_put_char(ip, OPCODE_MPISENDRECV, isdryrun);
          } else {
            if (isize2 > 0) {
              code_put_char(ip, OPCODE_MPISEND, isdryrun);
            } else {
              if (isize > 0) {
                code_put_char(ip, OPCODE_MPIRECV, isdryrun);
              }
            }
          }
          if (isize2 > 0) {
            code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add2),
                             isdryrun);
            code_put_int(ip, isize2, isdryrun);
            if ((recvcount < 0) && (rank_back_perm != NULL)) {
              code_put_int(
                  ip,
                  global_ranks[(rank_back_perm[(my_node + (port + 1) * gbstep) %
                                               (my_mpi_size_row /
                                                my_cores_per_node_row)] *
                                    my_cores_per_node_row +
                                my_lrank_row) *
                                   my_cores_per_node_column +
                               my_lrank_column],
                  isdryrun);
            } else {
              code_put_int(
                  ip,
                  global_ranks[(((my_node + (port + 1) * gbstep) %
                                 (my_mpi_size_row / my_cores_per_node_row)) *
                                    my_cores_per_node_row +
                                my_lrank_row) *
                                   my_cores_per_node_column +
                               my_lrank_column],
                  isdryrun);
            }
          }
          if (isize > 0) {
            code_put_pointer(ip,
                             (void *)(((char *)recv_buf_temp) +
                                      my_lrank_node * buf_offset + add),
                             isdryrun);
            code_put_int(ip, isize, isdryrun);
            if ((recvcount < 0) && (rank_back_perm != NULL)) {
              code_put_int(
                  ip,
                  global_ranks[(rank_back_perm[(my_mpi_size_row /
                                                    my_cores_per_node_row +
                                                my_node - (port + 1) * gbstep) %
                                               (my_mpi_size_row /
                                                my_cores_per_node_row)] *
                                    my_cores_per_node_row +
                                my_lrank_row) *
                                   my_cores_per_node_column +
                               my_lrank_column],
                  isdryrun);
            } else {
              code_put_int(
                  ip,
                  global_ranks[(((my_mpi_size_row / my_cores_per_node_row +
                                  my_node - (port + 1) * gbstep) %
                                 (my_mpi_size_row / my_cores_per_node_row)) *
                                    my_cores_per_node_row +
                                my_lrank_row) *
                                   my_cores_per_node_column +
                               my_lrank_column],
                  isdryrun);
            }
          }
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
      }
      if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
          (num_active_ports > 1)) {
        i_start = *ip;
        code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
        exec_single(ip, i_start, single_instruction, isdryrun);
      }
      for (i = 0; i < redsteps; i++) {
        mgbstep = gbstep;
        if (mgbstep + mgbstep * (i + 1) >
            my_mpi_size_row / my_cores_per_node_row)
          mgbstep = my_mpi_size_row / my_cores_per_node_row - mgbstep * (i + 1);
        if (recvcount < 0) {
          isize = 0;
          for (j = 0; j < mgbstep; j++) {
            isize += coarse_counts[(j + my_node) %
                                   (my_mpi_size_row / my_cores_per_node_row)];
          }
        } else {
          isize = mgbstep * my_cores_per_node_row * my_cores_per_node_column *
                  recvcount;
        }
        add = 0;
        isize2 = (isize / type_size) /
                 (my_cores_per_node_row * my_cores_per_node_column);
        j = (isize / type_size) %
            (my_cores_per_node_row * my_cores_per_node_column);
        if (my_lrank_node < j) {
          add += (isize2 * my_lrank_node + my_lrank_node) * type_size;
          isize2++;
        } else {
          add += (isize2 * my_lrank_node + j) * type_size;
        }
        if (isize2 > 0) {
          i_start = *ip;
          code_put_char(ip, OPCODE_REDUCE, isdryrun);
          code_put_char(ip, reduction_op, isdryrun);
          code_put_pointer(ip, (void *)(((char *)my_shared_buf) + add),
                           isdryrun);
          code_put_pointer(
              ip, (void *)(((char *)recv_buf_temp) + i * buf_offset + add),
              isdryrun);
          code_put_int(ip, isize2, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
        if ((my_cores_per_node_row * my_cores_per_node_column > 1) &&
            (num_active_ports > 1)) {
          i_start = *ip;
          code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
          exec_single(ip, i_start, single_instruction, isdryrun);
        }
      }
    }
  }
  if (!allreduce) {
    reduce_scatter_exec_bruck_copyout(
        recvbuf, my_shared_buf, lshmem_counts, my_node, my_lrank_row,
        my_lrank_column, my_cores_per_node_row, my_cores_per_node_column,
        recvcount, ip, single_instruction, isdryrun);
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_NODEBARRIER, isdryrun);
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

static int EXT_MPI_Allgatherv_init_local(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int *rank_perm, int exec_scheme, int allreduce,
    int recvcount) {
  int *rank_back_perm, *recvcounts_perm, *displs_perm;
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
      my_mpi_rank_world;
  int handle, isdryrun, num_comm_max;
  char volatile *my_shared_buf;
  int *global_ranks, i, j, k, new_counts_displs, my_size_shared_buf,
      *coarse_counts, *coarse_displs;
  char *ip, *shmem_old, *locmem_old;
  int lshmemid, shmem_size_old, locmem_size_old, lshmem_size_old;
  int volatile *lshmem_counts, *lshmem = NULL;
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &my_mpi_rank_world);
  MPI_Type_size(sendtype, &type_size);
  handle = get_handle(&comm_code, &handle_code_max);
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
  if (recvcount < 0) {
    rank_back_perm =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      rank_back_perm[rank_perm[i]] = i;
    }
    my_node = rank_perm[my_node];
  } else {
    rank_back_perm = NULL;
  }
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  /*  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
   * why wrong ? */
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  new_counts_displs = ((displs == NULL) && (recvcount < 0));
  if (new_counts_displs) {
    displs = (int *)malloc(my_mpi_size_row * sizeof(int));
    recvcounts = (int *)malloc(my_mpi_size_row * sizeof(int));
    MPI_Allgather(&sendcount, 1, MPI_INT, recvcounts, 1, MPI_INT, comm_row);
    displs[0] = 0;
    for (i = 0; i < my_mpi_size_row - 1; i++) {
      displs[i + 1] = displs[i] + recvcounts[i];
    }
  }
  if (recvcount < 0) {
    recvcounts_perm = (int *)malloc(my_mpi_size_row * sizeof(int));
    displs_perm = (int *)malloc(my_mpi_size_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      for (j = 0; j < my_cores_per_node_row; j++) {
        recvcounts_perm[rank_perm[i] * my_cores_per_node_row + j] =
            recvcounts[i * my_cores_per_node_row + j];
        displs_perm[rank_perm[i] * my_cores_per_node_row + j] =
            displs[i * my_cores_per_node_row + j];
      }
    }
    lshmem_size_old = 0;
    setup_shared_memory(
        comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
        my_mpi_size_row * my_cores_per_node_column * sizeof(int),
        &lshmem_size_old, &lshmemid, (volatile char **)(&lshmem), 0, 0);
    lshmem_counts = lshmem;
    if (my_lrank_row == 0) {
      for (j = 0; j < my_mpi_size_row; j++) {
        lshmem_counts[j * my_cores_per_node_column + my_lrank_column] =
            recvcounts_perm[j] * type_size;
      }
    }
    MPI_Barrier(comm_row);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Barrier(comm_column);
      MPI_Barrier(comm_row);
    }
    coarse_counts =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++) {
      coarse_counts[k] = 0;
      for (j = 0; j < my_cores_per_node_row; j++) {
        for (i = 0; i < my_cores_per_node_column; i++) {
          coarse_counts[k] += lshmem_counts[(k * my_cores_per_node_row + j) *
                                                my_cores_per_node_column +
                                            i];
        }
      }
    }
    coarse_displs = (int *)malloc(
        (my_mpi_size_row / my_cores_per_node_row + 1) * sizeof(int));
    coarse_displs[0] = 0;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      coarse_displs[i + 1] = coarse_displs[i] + coarse_counts[i];
    }
    my_size_shared_buf = 0;
    for (i = 0; i < my_mpi_size_row * my_cores_per_node_column; i++) {
      my_size_shared_buf += lshmem_counts[i];
    }
  } else {
    my_size_shared_buf =
        my_mpi_size_row * my_cores_per_node_column * recvcount * type_size;
  }
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  if (!setup_shared_memory(comm_row, my_cores_per_node_row, comm_column,
                           my_cores_per_node_column,
                           my_size_shared_buf + NUM_BARRIERS, &shmem_size,
                           &shmemid, &shmem, 0, NUM_BARRIERS)) {
    if (shmem_old != NULL) {
      rebase_addresses(shmem_old, shmem_size_old, (char *)shmem);
    }
  }
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
  my_shared_buf = shmem + NUM_BARRIERS;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    if (isdryrun) {
      ip = NULL;
    } else {
      if ((int)(num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status))) >
          locmem_size) {
        locmem_old = locmem;
        locmem_size_old = locmem_size;
        locmem_size = num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status));
        locmem = (char *)malloc(sizeof(char) * locmem_size);
        if (locmem_old != NULL) {
          rebase_addresses(locmem_old, locmem_size_old, (char *)locmem);
          free(locmem_old);
        }
      }
      ip = comm_code[handle] = (char *)malloc(sizeof(char *) * ((size_t)(ip)));
    }
    switch (exec_scheme) {
    case EXEC_RECURSIVE:
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        allgatherv_exec_recursive(
            sendbuf, recvbuf, type_size, my_shared_buf, lshmem_counts,
            my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
            my_lrank_node, my_lrank_row, my_lrank_column, my_node,
            coarse_counts, coarse_displs, num_ports, num_active_ports,
            global_ranks, rank_back_perm, displs_perm, &ip, 0, isdryrun,
            &num_comm_max, 100, allreduce, recvcount * type_size);
      } else {
        if (recvcount >= 0) {
          j = 0;
        } else {
          j = 0;
          for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
            if (rank_perm[i] != i) {
              j = 1;
            }
          }
        }
        if (j || allreduce) {
          allgatherv_exec_recursive_ring(
              sendbuf, recvbuf, type_size, my_shared_buf, lshmem_counts,
              my_mpi_size_row, 1, 1, 0, 0, 0, my_node, coarse_counts,
              coarse_displs, num_ports, num_active_ports, global_ranks,
              rank_back_perm, displs_perm, &ip, 0, isdryrun, &num_comm_max,
              allreduce, recvcount * type_size);
        } else {
          allgatherv_exec_recursive_ring(
              sendbuf, recvbuf, type_size, (volatile char *)recvbuf,
              lshmem_counts, my_mpi_size_row, 1, 1, 0, 0, 0, my_node,
              coarse_counts, coarse_displs, num_ports, num_active_ports,
              global_ranks, rank_back_perm, displs_perm, &ip, 0, isdryrun,
              &num_comm_max, allreduce, recvcount * type_size);
        }
      }
      break;
    case EXEC_BRUCK:
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        allgatherv_exec_bruck(
            sendbuf, recvbuf, type_size, my_shared_buf, lshmem_counts,
            my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
            my_lrank_node, my_lrank_row, my_lrank_column, my_node,
            coarse_counts, coarse_displs, num_ports, num_parallel,
            num_active_ports, global_ranks, rank_back_perm, displs_perm, &ip, 0,
            isdryrun, &num_comm_max, 100, allreduce, recvcount * type_size);
      } else {
        allgatherv_exec_bruck(
            sendbuf, recvbuf, type_size, my_shared_buf, lshmem_counts,
            my_mpi_size_row, 1, 1, 0, 0, 0, my_node, coarse_counts,
            coarse_displs, num_ports, num_parallel, num_active_ports,
            global_ranks, rank_back_perm, displs_perm, &ip, 0, isdryrun,
            &num_comm_max, 100, allreduce, recvcount * type_size);
      }
      break;
    }
  }

  if (new_counts_displs) {
    free(recvcounts);
    free(displs);
  }
  if (recvcount < 0) {
    destroy_shared_memory(&lshmem_size_old, &lshmemid,
                          (char volatile **)&lshmem);
    free(coarse_displs);
    free(coarse_counts);
    free(displs_perm);
    free(recvcounts_perm);
    free(rank_back_perm);
  }
  free(global_ranks);
  return (handle);
}

static int EXT_MPI_Allreduce_init_local(
    void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
    MPI_Comm comm_row, int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int reduction_op, int copyin, int *num_ports,
    int *num_ports_limit, int num_active_ports, int exec_scheme) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
      my_mpi_rank_world;
  int handle, isdryrun, num_comm_max;
  char volatile *my_shared_buf;
  int *global_ranks, my_size_shared_buf, coarse_count, *counts;
  char *ip, *shmem_old, *locmem_old;
  int shmem_size_old, locmem_size_old, base_size_shared;
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &my_mpi_rank_world);
  MPI_Type_size(datatype, &type_size);
  handle = get_handle(&comm_code, &handle_code_max);
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
  /*  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
   * why wrong ? */
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  counts = (int *)malloc(my_cores_per_node_column * sizeof(int));
  if (comm_column != MPI_COMM_NULL) {
    MPI_Allreduce(&count, &coarse_count, 1, MPI_INT, MPI_SUM, comm_column);
    MPI_Allgather(&count, 1, MPI_INT, counts, 1, MPI_INT, comm_column);
  } else {
    coarse_count = count;
    counts[0] = count;
  }
  my_size_shared_buf =
      coarse_count * my_mpi_size_row * my_cores_per_node_column * type_size;
  base_size_shared = my_size_shared_buf;
  my_size_shared_buf *= (my_cores_per_node_row * my_cores_per_node_column + 1);
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  if (!setup_shared_memory(comm_row, my_cores_per_node_row, comm_column,
                           my_cores_per_node_column,
                           my_size_shared_buf + NUM_BARRIERS, &shmem_size,
                           &shmemid, &shmem, 0, NUM_BARRIERS)) {
    if (shmem_old != NULL) {
      rebase_addresses(shmem_old, shmem_size_old, (char *)shmem);
    }
  }
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
  my_shared_buf = shmem + NUM_BARRIERS;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    if (isdryrun) {
      ip = NULL;
    } else {
      if ((int)(num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status))) >
          locmem_size) {
        locmem_old = locmem;
        locmem_size_old = locmem_size;
        locmem_size = num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status));
        locmem = (char *)malloc(sizeof(char) * locmem_size);
        if (locmem_old != NULL) {
          rebase_addresses(locmem_old, locmem_size_old, (char *)locmem);
          free(locmem_old);
        }
      }
      ip = comm_code[handle] = (char *)malloc(sizeof(char *) * ((size_t)(ip)));
    }
    switch (exec_scheme) {
      /*          case EXEC_RECURSIVE:
                  if (my_cores_per_node_row*my_cores_per_node_column>1){
                    allreduce_exec_recursive(sendbuf, recvbuf, type_size,
         my_shared_buf, base_size_shared, counts, my_mpi_size_row,
         my_cores_per_node_row, my_cores_per_node_column, my_lrank_node,
         my_lrank_row, my_lrank_column, my_node, coarse_count, num_ports,
         num_ports_limit, num_active_ports, global_ranks, reduction_op, &ip, 0,
         isdryrun, &num_comm_max, 100); }else{ allreduce_exec_recursive(sendbuf,
         recvbuf, type_size, my_shared_buf, base_size_shared, counts,
         my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
         my_lrank_node, my_lrank_row, my_lrank_column, my_node, coarse_count,
         num_ports, num_ports_limit, num_active_ports, global_ranks,
         reduction_op, &ip, 0, isdryrun, &num_comm_max, 100);
                  }
                  break;
                case EXEC_BRUCK:
                  allreduce_exec_bruck(sendbuf, recvbuf, type_size,
         my_shared_buf, base_size_shared, counts, my_mpi_size_row,
         my_cores_per_node_row, my_cores_per_node_column, my_lrank_node,
         my_lrank_row, my_lrank_column, my_node, coarse_count, num_ports,
         num_ports_limit, num_active_ports, global_ranks, reduction_op, &ip, 0,
         isdryrun, &num_comm_max, 100); break;*/
    }
    allreduce_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, base_size_shared, counts,
        my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
        my_lrank_node, my_lrank_row, my_lrank_column, my_node, coarse_count,
        num_ports, num_ports_limit, num_active_ports, global_ranks,
        reduction_op, copyin, &ip, 0, isdryrun, &num_comm_max, 100);
  }

  free(counts);
  free(global_ranks);
  return (handle);
}

static int EXT_MPI_Reduce_scatter_init_local(
    void *sendbuf, void *recvbuf, int *recvcounts, int *displs,
    MPI_Datatype datatype, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int reduction_op,
    int copyin, int *num_ports, int *num_parallel, int num_active_ports,
    int *rank_perm, int exec_scheme, int allreduce, int recvcount) {
  char volatile *recv_buf_temp;
  int *rank_back_perm, *recvcounts_perm, *displs_perm;
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node,
      my_mpi_rank_world;
  int handle, isdryrun, num_comm_max;
  char volatile *my_shared_buf;
  int *global_ranks, i, j, k, my_size_shared_buf, *coarse_counts,
      *coarse_displs;
  char *ip, *shmem_old, *locmem_old;
  int lshmemid, shmem_size_old, locmem_size_old, lshmem_size_old;
  int volatile *lshmem_counts, *lshmem = NULL;
  if (num_active_ports > my_cores_per_node_row * my_cores_per_node_column) {
    num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  }
  if (num_active_ports < 1) {
    num_active_ports = 1;
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &my_mpi_rank_world);
  MPI_Type_size(datatype, &type_size);
  handle = get_handle(&comm_code, &handle_code_max);
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
  if (recvcount < 0) {
    rank_back_perm =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      rank_back_perm[rank_perm[i]] = i;
    }
    my_node = rank_perm[my_node];
  } else {
    rank_back_perm = NULL;
  }
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  /*  my_lrank_node = my_lrank_column * my_cores_per_node_row + my_lrank_row;
   * why wrong ? */
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  if (recvcount < 0) {
    recvcounts_perm = (int *)malloc(my_mpi_size_row * sizeof(int));
    displs_perm = (int *)malloc(my_mpi_size_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      for (j = 0; j < my_cores_per_node_row; j++) {
        recvcounts_perm[rank_perm[i] * my_cores_per_node_row + j] =
            recvcounts[i * my_cores_per_node_row + j];
        displs_perm[rank_perm[i] * my_cores_per_node_row + j] =
            displs[i * my_cores_per_node_row + j];
      }
    }
    lshmem_size_old = 0;
    setup_shared_memory(
        comm_row, my_cores_per_node_row, comm_column, my_cores_per_node_column,
        my_mpi_size_row * my_cores_per_node_column * sizeof(int),
        &lshmem_size_old, &lshmemid, (volatile char **)(&lshmem), 0, 0);
    lshmem_counts = lshmem;
    if (my_lrank_row == 0) {
      for (j = 0; j < my_mpi_size_row; j++) {
        lshmem_counts[j * my_cores_per_node_column + my_lrank_column] =
            recvcounts_perm[j] * type_size;
      }
    }
    MPI_Barrier(comm_row);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Barrier(comm_column);
      MPI_Barrier(comm_row);
    }
    coarse_counts =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++) {
      coarse_counts[k] = 0;
      for (j = 0; j < my_cores_per_node_row; j++) {
        for (i = 0; i < my_cores_per_node_column; i++) {
          coarse_counts[k] += lshmem_counts[(k * my_cores_per_node_row + j) *
                                                my_cores_per_node_column +
                                            i];
        }
      }
    }
    coarse_displs = (int *)malloc(
        (my_mpi_size_row / my_cores_per_node_row + 1) * sizeof(int));
    coarse_displs[0] = 0;
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      coarse_displs[i + 1] = coarse_displs[i] + coarse_counts[i];
    }
    my_size_shared_buf = 0;
    for (i = 0; i < my_mpi_size_row * my_cores_per_node_column; i++) {
      my_size_shared_buf += lshmem_counts[i];
    }
  } else {
    my_size_shared_buf =
        my_mpi_size_row * my_cores_per_node_column * recvcount * type_size;
  }
  if (num_active_ports + 1 > my_cores_per_node_row) {
    my_size_shared_buf *= (num_active_ports + 1);
  } else {
    my_size_shared_buf *= my_cores_per_node_row;
  }
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  if (!setup_shared_memory(comm_row, my_cores_per_node_row, comm_column,
                           my_cores_per_node_column,
                           my_size_shared_buf + NUM_BARRIERS, &shmem_size,
                           &shmemid, &shmem, 0, NUM_BARRIERS)) {
    if (shmem_old != NULL) {
      rebase_addresses(shmem_old, shmem_size_old, (char *)shmem);
    }
  }
  global_ranks =
      (int *)malloc(sizeof(int) * my_mpi_size_row * my_cores_per_node_column);
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks);
  my_shared_buf = shmem + NUM_BARRIERS;
  if (num_active_ports + 1 > my_cores_per_node_row) {
    recv_buf_temp = my_shared_buf + my_size_shared_buf / (num_active_ports + 1);
  } else {
    recv_buf_temp = my_shared_buf + my_size_shared_buf / my_cores_per_node_row;
  }
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    if (isdryrun) {
      ip = NULL;
    } else {
      if ((int)(num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status))) >
          locmem_size) {
        locmem_old = locmem;
        locmem_size_old = locmem_size;
        locmem_size = num_comm_max * (sizeof(MPI_Request) + sizeof(MPI_Status));
        locmem = (char *)malloc(sizeof(char) * locmem_size);
        if (locmem_old != NULL) {
          rebase_addresses(locmem_old, locmem_size_old, (char *)locmem);
          free(locmem_old);
        }
      }
      ip = comm_code[handle] = (char *)malloc(sizeof(char *) * ((size_t)(ip)));
    }
    switch (exec_scheme) {
    case EXEC_RECURSIVE:
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        reduce_scatter_exec_recursive(
            sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
            lshmem_counts, my_mpi_size_row, my_cores_per_node_row,
            my_cores_per_node_column, my_lrank_node, my_lrank_row,
            my_lrank_column, my_node, coarse_counts, coarse_displs, num_ports,
            num_active_ports, global_ranks, rank_back_perm, displs_perm,
            reduction_op, copyin, &ip, 0, isdryrun, &num_comm_max, allreduce,
            recvcount * type_size);
        //              reduce_scatter_exec_recursive_ring(sendbuf, recvbuf,
        //              type_size, my_shared_buf, recv_buf_temp, lshmem_counts,
        //              my_mpi_size_row, my_cores_per_node_row,
        //              my_cores_per_node_column, my_lrank_node, my_lrank_row,
        //              my_lrank_column, my_node, coarse_counts, coarse_displs,
        //              num_ports, num_active_ports, global_ranks,
        //              rank_back_perm, displs_perm, reduction_op, &ip, 0,
        //              isdryrun, &num_comm_max, allreduce,
        //              recvcount*type_size);
      } else {
        if (recvcount >= 0) {
          j = 0;
        } else {
          j = 0;
          for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
            if (rank_perm[i] != i) {
              j = 1;
            }
          }
        }
        if (j) {
          reduce_scatter_exec_recursive_ring(
              sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
              lshmem_counts, my_mpi_size_row, 1, 1, 0, 0, 0, my_node,
              coarse_counts, coarse_displs, num_ports, num_active_ports,
              global_ranks, rank_back_perm, displs_perm, reduction_op, copyin,
              &ip, 0, isdryrun, &num_comm_max, allreduce,
              recvcount * type_size);
        } else {
          reduce_scatter_exec_recursive_ring(
              sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
              lshmem_counts, my_mpi_size_row, 1, 1, 0, 0, 0, my_node,
              coarse_counts, coarse_displs, num_ports, num_active_ports,
              global_ranks, rank_back_perm, displs_perm, reduction_op, copyin,
              &ip, 0, isdryrun, &num_comm_max, allreduce,
              recvcount * type_size);
        }
      }
      break;
    case EXEC_BRUCK:
      if (my_cores_per_node_row * my_cores_per_node_column > 1) {
        reduce_scatter_exec_bruck(
            sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
            lshmem_counts, my_mpi_size_row, my_cores_per_node_row,
            my_cores_per_node_column, my_lrank_node, my_lrank_row,
            my_lrank_column, my_node, coarse_counts, coarse_displs, num_ports,
            num_parallel, num_active_ports, global_ranks, rank_back_perm,
            displs_perm, reduction_op, copyin, &ip, 0, isdryrun, &num_comm_max,
            allreduce, recvcount * type_size);
      } else {
        reduce_scatter_exec_bruck(
            sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
            lshmem_counts, my_mpi_size_row, 1, 1, 0, 0, 0, my_node,
            coarse_counts, coarse_displs, num_ports, num_parallel,
            num_active_ports, global_ranks, rank_back_perm, displs_perm,
            reduction_op, copyin, &ip, 0, isdryrun, &num_comm_max, allreduce,
            recvcount * type_size);
      }
      break;
    }
  }
  if (recvcount < 0) {
    destroy_shared_memory(&lshmem_size_old, &lshmemid,
                          (char volatile **)&lshmem);
    free(coarse_displs);
    free(coarse_counts);
    free(displs_perm);
    free(recvcounts_perm);
    free(rank_back_perm);
  }
  free(global_ranks);
  return (handle);
}

static int rank_perm_heuristic_compare(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

static void rank_perm_heuristic(int num_nodes, int *node_recvcounts,
                                int *rank_perm) {
  typedef struct node {
    int value, key;
  } node_t;
  node_t array[num_nodes];
  int lnode_recvcounts[num_nodes / 2], lrank_perm[num_nodes / 2],
      perm_array[num_nodes];
  int i, j, k;
  for (i = 0; i < num_nodes; i++) {
    array[i].value = node_recvcounts[i];
    array[i].key = i;
  }
  if (num_nodes > 1) {
    qsort(array, num_nodes, sizeof(node_t), rank_perm_heuristic_compare);
    for (i = 0; i < num_nodes / 2; i++) {
      lnode_recvcounts[i] =
          array[i].value + array[(num_nodes / 2) * 2 - 1 - i].value;
    }
    rank_perm_heuristic(num_nodes / 2, lnode_recvcounts, lrank_perm);
    for (i = 0; i < num_nodes / 2; i++) {
      perm_array[i * 2] = lrank_perm[i];
      perm_array[i * 2 + 1] = (num_nodes / 2) * 2 - 1 - lrank_perm[i];
    }
    if (num_nodes % 2) {
      perm_array[num_nodes - 1] = num_nodes - 1;
    }
  } else {
    perm_array[0] = 0;
  }
  for (i = 0; i < num_nodes; i++) {
    rank_perm[array[i].key] = perm_array[i];
  }
}

int EXT_MPI_Allgatherv_init_native(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm_row,
    int my_cores_per_node_row, MPI_Comm comm_column,
    int my_cores_per_node_column, int *num_ports, int *num_parallel,
    int num_active_ports, int allreduce, int bruck, int recvcount) {
  int my_mpi_size_row, *rank_perm, *node_recvcounts, handle, i, j;
  if (recvcount < 0) {
    MPI_Comm_size(comm_row, &my_mpi_size_row);
    rank_perm =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    node_recvcounts =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      node_recvcounts[i] = 0;
      for (j = 0; j < my_cores_per_node_row; j++) {
        node_recvcounts[i] += recvcounts[i * my_cores_per_node_row + j];
      }
    }
    if (comm_column != MPI_COMM_NULL) {
      MPI_Allreduce(MPI_IN_PLACE, node_recvcounts,
                    my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                    comm_column);
    }
    rank_perm_heuristic(my_mpi_size_row / my_cores_per_node_row,
                        node_recvcounts, rank_perm);
    j = 0;
    i = (num_ports[j++] + 1);
    while (i < my_mpi_size_row / my_cores_per_node_row) {
      i *= (num_ports[j++] + 1);
    }
    if ((i == my_mpi_size_row / my_cores_per_node_row) && (bruck == 0)) {
      handle = EXT_MPI_Allgatherv_init_local(
          sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
          comm_row, my_cores_per_node_row, comm_column,
          my_cores_per_node_column, num_ports, num_parallel, num_active_ports,
          rank_perm, EXEC_RECURSIVE, allreduce, -1);
    } else {
      handle = EXT_MPI_Allgatherv_init_local(
          sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype,
          comm_row, my_cores_per_node_row, comm_column,
          my_cores_per_node_column, num_ports, num_parallel, num_active_ports,
          rank_perm, EXEC_BRUCK, allreduce, -1);
    }
    free(node_recvcounts);
    free(rank_perm);
  } else {
    MPI_Comm_size(comm_row, &my_mpi_size_row);
    j = 0;
    i = (num_ports[j++] + 1);
    while (i < my_mpi_size_row / my_cores_per_node_row) {
      i *= (num_ports[j++] + 1);
    }
    if ((i == my_mpi_size_row / my_cores_per_node_row) && (bruck == 0)) {
      handle = EXT_MPI_Allgatherv_init_local(
          sendbuf, sendcount, sendtype, recvbuf, NULL, NULL, recvtype, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          num_ports, num_parallel, num_active_ports, NULL, EXEC_RECURSIVE,
          allreduce, recvcount);
    } else {
      handle = EXT_MPI_Allgatherv_init_local(
          sendbuf, sendcount, sendtype, recvbuf, NULL, NULL, recvtype, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          num_ports, num_parallel, num_active_ports, NULL, EXEC_BRUCK,
          allreduce, recvcount);
    }
  }
  return (handle);
}

int EXT_MPI_Reduce_scatter_init_native(
    void *sendbuf, void *recvbuf, int *recvcounts, MPI_Datatype datatype,
    MPI_Op op, MPI_Comm comm_row, int my_cores_per_node_row,
    MPI_Comm comm_column, int my_cores_per_node_column, int *num_ports,
    int *num_parallel, int num_active_ports, int copyin, int allreduce,
    int bruck, int recvcount) {
  int my_mpi_size_row, *rank_perm, *node_recvcounts, handle, i, j, *displs;
  int reduction_op;
  if (recvcount < 0) {
    if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
      reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
    }
    if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
      reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
    }
    MPI_Comm_size(comm_row, &my_mpi_size_row);
    rank_perm =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    node_recvcounts =
        (int *)malloc(my_mpi_size_row / my_cores_per_node_row * sizeof(int));
    for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
      node_recvcounts[i] = 0;
      for (j = 0; j < my_cores_per_node_row; j++) {
        node_recvcounts[i] += recvcounts[i * my_cores_per_node_row + j];
      }
    }
    if (comm_column != MPI_COMM_NULL) {
      MPI_Allreduce(MPI_IN_PLACE, node_recvcounts,
                    my_mpi_size_row / my_cores_per_node_row, MPI_INT, MPI_SUM,
                    comm_column);
    }
    displs = (int *)malloc(my_mpi_size_row * sizeof(int));
    displs[0] = 0;
    for (i = 0; i < my_mpi_size_row - 1; i++) {
      displs[i + 1] = displs[i] + recvcounts[i];
    }
    rank_perm_heuristic(my_mpi_size_row / my_cores_per_node_row,
                        node_recvcounts, rank_perm);
    j = 0;
    i = (num_ports[j++] + 1);
    while (i < my_mpi_size_row / my_cores_per_node_row) {
      i *= (num_ports[j++] + 1);
    }
    if ((i == my_mpi_size_row / my_cores_per_node_row) && (bruck == 0)) {
      handle = EXT_MPI_Reduce_scatter_init_local(
          sendbuf, recvbuf, recvcounts, displs, datatype, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          reduction_op, copyin, num_ports, num_parallel, num_active_ports,
          rank_perm, EXEC_RECURSIVE, allreduce, -1);
    } else {
      handle = EXT_MPI_Reduce_scatter_init_local(
          sendbuf, recvbuf, recvcounts, displs, datatype, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          reduction_op, copyin, num_ports, num_parallel, num_active_ports,
          rank_perm, EXEC_BRUCK, allreduce, -1);
    }
    free(displs);
    free(node_recvcounts);
    free(rank_perm);
  } else {
    if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
      reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
    }
    if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
      reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
    }
    MPI_Comm_size(comm_row, &my_mpi_size_row);
    j = 0;
    i = (num_ports[j++] + 1);
    while (i < my_mpi_size_row / my_cores_per_node_row) {
      i *= (num_ports[j++] + 1);
    }
    if ((i == my_mpi_size_row / my_cores_per_node_row) && (bruck == 0)) {
      handle = EXT_MPI_Reduce_scatter_init_local(
          sendbuf, recvbuf, NULL, NULL, datatype, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          reduction_op, copyin, num_ports, num_parallel, num_active_ports, NULL,
          EXEC_RECURSIVE, allreduce, recvcount);
    } else {
      handle = EXT_MPI_Reduce_scatter_init_local(
          sendbuf, recvbuf, NULL, NULL, datatype, comm_row,
          my_cores_per_node_row, comm_column, my_cores_per_node_column,
          reduction_op, copyin, num_ports, num_parallel, num_active_ports, NULL,
          EXEC_BRUCK, allreduce, recvcount);
    }
  }
  return (handle);
}

int EXT_MPI_Allreduce_init_native(void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int my_cores_per_node_row,
                                  MPI_Comm comm_column,
                                  int my_cores_per_node_column, int *num_ports,
                                  int *num_ports_limit, int num_active_ports,
                                  int copyin) {
  int my_mpi_size_row, handle, i, j;
  int reduction_op;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  j = 0;
  i = (num_ports[j++] + 1);
  while (i < my_mpi_size_row / my_cores_per_node_row) {
    i *= (num_ports[j++] + 1);
  }
  if (i == my_mpi_size_row / my_cores_per_node_row) {
    handle = EXT_MPI_Allreduce_init_local(
        sendbuf, recvbuf, count, datatype, comm_row, my_cores_per_node_row,
        comm_column, my_cores_per_node_column, reduction_op, copyin, num_ports,
        num_ports_limit, num_active_ports, EXEC_RECURSIVE);
  } else {
    handle = EXT_MPI_Allreduce_init_local(
        sendbuf, recvbuf, count, datatype, comm_row, my_cores_per_node_row,
        comm_column, my_cores_per_node_column, reduction_op, copyin, num_ports,
        num_ports_limit, num_active_ports, EXEC_BRUCK);
  }
  return (handle);
}

int EXT_MPI_Init_general_native(MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column) {
  int handle, *global_ranks_p, my_mpi_size_row, my_mpi_rank_row;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  handle = get_handle(&comm_comm, &handle_comm_max);
  global_ranks_p = (int *)malloc(
      sizeof(int) * (my_mpi_size_row * my_cores_per_node_column + 4));
  setup_rank_translation(comm_row, my_cores_per_node_row, comm_column,
                         my_cores_per_node_column, global_ranks_p + 4);
  global_ranks_p[0] = my_cores_per_node_row;
  global_ranks_p[1] = my_cores_per_node_column;
  global_ranks_p[2] = my_mpi_size_row;
  global_ranks_p[3] = my_mpi_rank_row;
  comm_comm[handle] = (char *)global_ranks_p;
  return (handle);
}

int EXT_MPI_Done_general_native(int handle) {
  int i;
  free(comm_comm[handle]);
  comm_comm[handle] = NULL;
  for (i = 0; i < handle_comm_max; i++) {
    if (comm_comm[i] != NULL) {
      return (0);
    }
  }
  free(comm_comm);
  comm_comm = NULL;
  return (0);
}

int EXT_MPI_Allgatherv_native(void *sendbuf, int sendcount,
                              MPI_Datatype sendtype, void *recvbuf,
                              int *recvcounts, int *displs,
                              MPI_Datatype recvtype, int *num_ports,
                              int *num_parallel, int handle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int isdryrun, num_comm_max;
  char volatile *my_shared_buf;
  int *global_ranks_p, i, j, k, new_counts_displs;
  char *ip;
  int lshmemid, locmem_size_old, lshmem_size_old;
  int volatile *lshmem_counts;
  int num_active_ports, my_cores_per_node_row, my_cores_per_node_column;
  char my_code[10000];
  global_ranks_p = (int *)comm_comm[handle];
  my_cores_per_node_row = global_ranks_p[0];
  my_cores_per_node_column = global_ranks_p[1];
  my_mpi_size_row = global_ranks_p[2];
  my_mpi_rank_row = global_ranks_p[3];
  num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  MPI_Type_size(sendtype, &type_size);
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  lshmem_counts = (int *)(shmem + NUM_BARRIERS);
  if (my_lrank_row == 0) {
    for (j = 0; j < my_mpi_size_row; j++) {
      lshmem_counts[j * my_cores_per_node_column + my_lrank_column] =
          recvcounts[j] * type_size;
    }
  }
  node_barrier(my_cores_per_node_row * my_cores_per_node_column);
  int coarse_counts[my_mpi_size_row / my_cores_per_node_row];
  int coarse_displs[my_mpi_size_row / my_cores_per_node_row + 1];
  for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++) {
    coarse_counts[k] = 0;
    for (j = 0; j < my_cores_per_node_row; j++) {
      for (i = 0; i < my_cores_per_node_column; i++) {
        coarse_counts[k] += lshmem_counts[(k * my_cores_per_node_row + j) *
                                              my_cores_per_node_column +
                                          i];
      }
    }
  }
  coarse_displs[0] = 0;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    coarse_displs[i + 1] = coarse_displs[i] + coarse_counts[i];
  }
  my_shared_buf = shmem +
                  my_mpi_size_row * my_cores_per_node_column * sizeof(int) +
                  NUM_BARRIERS;
  node_barrier(my_cores_per_node_row * my_cores_per_node_column);
  isdryrun = 0;
  ip = my_code;
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
    allgatherv_exec_bruck(sendbuf, recvbuf, type_size, my_shared_buf,
                          lshmem_counts, my_mpi_size_row, my_cores_per_node_row,
                          my_cores_per_node_column, my_lrank_node, my_lrank_row,
                          my_lrank_column, my_node, coarse_counts,
                          coarse_displs, num_ports, num_parallel,
                          num_active_ports, global_ranks_p + 4, NULL, displs,
                          &ip, 1, isdryrun, &num_comm_max, 100, 0, -1);
  } else {
    allgatherv_exec_bruck(sendbuf, recvbuf, type_size, my_shared_buf,
                          lshmem_counts, my_mpi_size_row, my_cores_per_node_row,
                          my_cores_per_node_column, my_lrank_node, my_lrank_row,
                          my_lrank_column, my_node, coarse_counts,
                          coarse_displs, num_ports, num_parallel,
                          num_active_ports, global_ranks_p + 4, NULL, displs,
                          &ip, 1, isdryrun, &num_comm_max, 100, 0, -1);
  }
  return (0);
}

int EXT_MPI_Allgather_native(void *sendbuf, int sendcount,
                             MPI_Datatype sendtype, void *recvbuf,
                             int recvcount, MPI_Datatype recvtype,
                             int *num_ports, int *num_parallel, int handle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int isdryrun, num_comm_max;
  char volatile *my_shared_buf;
  int *global_ranks_p, i, j, k, new_counts_displs, *coarse_counts,
      *coarse_displs;
  char *ip, *shmem_old, *locmem_old;
  int lshmemid, shmem_size_old, locmem_size_old, lshmem_size_old;
  int num_active_ports, my_cores_per_node_row, my_cores_per_node_column;
  char my_code[10000];
  global_ranks_p = (int *)comm_comm[handle];
  my_cores_per_node_row = global_ranks_p[0];
  my_cores_per_node_column = global_ranks_p[1];
  my_mpi_size_row = global_ranks_p[2];
  my_mpi_rank_row = global_ranks_p[3];
  num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  MPI_Type_size(sendtype, &type_size);
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  my_shared_buf = shmem + NUM_BARRIERS;
  isdryrun = 0;
  ip = my_code;
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
    allgatherv_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, NULL, my_mpi_size_row,
        my_cores_per_node_row, my_cores_per_node_column, my_lrank_node,
        my_lrank_row, my_lrank_column, my_node, coarse_counts, coarse_displs,
        num_ports, num_parallel, num_active_ports, global_ranks_p + 4, NULL,
        NULL, &ip, 1, isdryrun, &num_comm_max, 100, 0, recvcount * type_size);
  } else {
    allgatherv_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, NULL, my_mpi_size_row,
        my_cores_per_node_row, my_cores_per_node_column, my_lrank_node,
        my_lrank_row, my_lrank_column, my_node, coarse_counts, coarse_displs,
        num_ports, num_parallel, num_active_ports, global_ranks_p + 4, NULL,
        NULL, &ip, 1, isdryrun, &num_comm_max, 100, 0, recvcount * type_size);
  }
  return (0);
}

int EXT_MPI_Reduce_scatter_native(void *sendbuf, void *recvbuf, int *recvcounts,
                                  MPI_Datatype datatype, MPI_Op op,
                                  int *num_ports, int *num_parallel, int copyin,
                                  int handle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int isdryrun, num_comm_max, my_size_shared_buf;
  char volatile *my_shared_buf, *recv_buf_temp;
  int *global_ranks_p, i, j, k, new_counts_displs;
  char *ip;
  int lshmemid, locmem_size_old, lshmem_size_old;
  int volatile *lshmem_counts;
  int num_active_ports, my_cores_per_node_row, my_cores_per_node_column;
  char my_code[10000];
  int reduction_op;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  global_ranks_p = (int *)comm_comm[handle];
  my_cores_per_node_row = global_ranks_p[0];
  my_cores_per_node_column = global_ranks_p[1];
  my_mpi_size_row = global_ranks_p[2];
  my_mpi_rank_row = global_ranks_p[3];
  num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  MPI_Type_size(datatype, &type_size);
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  lshmem_counts = (int *)(shmem + NUM_BARRIERS);
  if (my_lrank_row == 0) {
    for (j = 0; j < my_mpi_size_row; j++) {
      lshmem_counts[j * my_cores_per_node_column + my_lrank_column] =
          recvcounts[j] * type_size;
    }
  }
  node_barrier(my_cores_per_node_row * my_cores_per_node_column);
  int coarse_counts[my_mpi_size_row / my_cores_per_node_row];
  int coarse_displs[my_mpi_size_row / my_cores_per_node_row + 1];
  int displs[my_mpi_size_row];
  for (k = 0; k < my_mpi_size_row / my_cores_per_node_row; k++) {
    coarse_counts[k] = 0;
    for (j = 0; j < my_cores_per_node_row; j++) {
      for (i = 0; i < my_cores_per_node_column; i++) {
        coarse_counts[k] += lshmem_counts[(k * my_cores_per_node_row + j) *
                                              my_cores_per_node_column +
                                          i];
      }
    }
  }
  coarse_displs[0] = 0;
  for (i = 0; i < my_mpi_size_row / my_cores_per_node_row; i++) {
    coarse_displs[i + 1] = coarse_displs[i] + coarse_counts[i];
  }
  displs[0] = 0;
  for (i = 0; i < my_mpi_size_row - 1; i++) {
    displs[i + 1] = displs[i] + recvcounts[i];
  }
  my_shared_buf = shmem +
                  my_mpi_size_row * my_cores_per_node_column * sizeof(int) +
                  NUM_BARRIERS;
  my_size_shared_buf = 0;
  for (i = 0; i < my_mpi_size_row * my_cores_per_node_column; i++) {
    my_size_shared_buf += lshmem_counts[i];
  }
  if (num_active_ports + 1 > my_cores_per_node_row) {
    recv_buf_temp = my_shared_buf + my_size_shared_buf / (num_active_ports + 1);
  } else {
    recv_buf_temp = my_shared_buf + my_size_shared_buf / my_cores_per_node_row;
  }
  node_barrier(my_cores_per_node_row * my_cores_per_node_column);
  isdryrun = 0;
  ip = my_code;
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
    reduce_scatter_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
        lshmem_counts, my_mpi_size_row, my_cores_per_node_row,
        my_cores_per_node_column, my_lrank_node, my_lrank_row, my_lrank_column,
        my_node, coarse_counts, coarse_displs, num_ports, num_parallel,
        num_active_ports, global_ranks_p + 4, NULL, displs, reduction_op,
        copyin, &ip, 1, isdryrun, &num_comm_max, 0, -1);
  } else {
    reduce_scatter_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp,
        lshmem_counts, my_mpi_size_row, my_cores_per_node_row,
        my_cores_per_node_column, my_lrank_node, my_lrank_row, my_lrank_column,
        my_node, coarse_counts, coarse_displs, num_ports, num_parallel,
        num_active_ports, global_ranks_p + 4, NULL, displs, reduction_op,
        copyin, &ip, 1, isdryrun, &num_comm_max, 0, -1);
  }
  return (0);
}

int EXT_MPI_Reduce_scatter_block_native(void *sendbuf, void *recvbuf,
                                        int recvcount, MPI_Datatype datatype,
                                        MPI_Op op, int *num_ports,
                                        int *num_parallel, int copyin,
                                        int handle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int isdryrun, num_comm_max;
  char volatile *my_shared_buf, *recv_buf_temp;
  int *global_ranks_p, i, j, k, new_counts_displs, my_size_shared_buf,
      *coarse_counts, *coarse_displs;
  char *ip, *shmem_old, *locmem_old;
  int lshmemid, shmem_size_old, locmem_size_old, lshmem_size_old;
  int num_active_ports, my_cores_per_node_row, my_cores_per_node_column;
  char my_code[10000];
  int reduction_op;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  global_ranks_p = (int *)comm_comm[handle];
  my_cores_per_node_row = global_ranks_p[0];
  my_cores_per_node_column = global_ranks_p[1];
  my_mpi_size_row = global_ranks_p[2];
  my_mpi_rank_row = global_ranks_p[3];
  num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  MPI_Type_size(datatype, &type_size);
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  my_size_shared_buf =
      my_mpi_size_row * my_cores_per_node_column * recvcount * type_size;
  shmem_old = (char *)shmem;
  shmem_size_old = shmem_size;
  my_shared_buf = shmem + NUM_BARRIERS;
  if (num_active_ports + 1 > my_cores_per_node_row) {
    recv_buf_temp = my_shared_buf + my_size_shared_buf / (num_active_ports + 1);
  } else {
    recv_buf_temp = my_shared_buf + my_size_shared_buf / my_cores_per_node_row;
  }
  isdryrun = 0;
  ip = my_code;
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
    reduce_scatter_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp, NULL,
        my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
        my_lrank_node, my_lrank_row, my_lrank_column, my_node, coarse_counts,
        coarse_displs, num_ports, num_parallel, num_active_ports,
        global_ranks_p + 4, NULL, NULL, reduction_op, copyin, &ip, 1, isdryrun,
        &num_comm_max, 0, recvcount * type_size);
  } else {
    reduce_scatter_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, recv_buf_temp, NULL,
        my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
        my_lrank_node, my_lrank_row, my_lrank_column, my_node, coarse_counts,
        coarse_displs, num_ports, num_parallel, num_active_ports,
        global_ranks_p + 4, NULL, NULL, reduction_op, copyin, &ip, 1, isdryrun,
        &num_comm_max, 0, recvcount * type_size);
  }
  return (0);
}

int EXT_MPI_Allreduce_native(void *sendbuf, void *recvbuf, int count,
                             MPI_Datatype datatype, MPI_Op op, int *num_ports,
                             int *num_ports_limit, int *num_parallel,
                             int copyin, int handle) {
  int my_mpi_rank_row, my_mpi_size_row, my_lrank_row, my_node, type_size,
      my_mpi_rank_column, my_mpi_size_column, my_lrank_column, my_lrank_node;
  int isdryrun, num_comm_max, coarse_count, counts[1000];
  char volatile *my_shared_buf, *recv_buf_temp;
  int *global_ranks_p, i, j, k, new_counts_displs, my_size_shared_buf;
  char *ip;
  int lshmemid, shmem_size_old, lshmem_size_old;
  int num_active_ports, my_cores_per_node_row, my_cores_per_node_column;
  char my_code[10000];
  int reduction_op;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  global_ranks_p = (int *)comm_comm[handle];
  my_cores_per_node_row = global_ranks_p[0];
  my_cores_per_node_column = global_ranks_p[1];
  my_mpi_size_row = global_ranks_p[2];
  my_mpi_rank_row = global_ranks_p[3];
  if (my_cores_per_node_column > 1000) {
    printf("maximum 1000 cores per column\n");
    exit(1);
  }
  num_active_ports = my_cores_per_node_row * my_cores_per_node_column;
  MPI_Type_size(datatype, &type_size);
  my_node = my_mpi_rank_row / my_cores_per_node_row;
  my_lrank_row = my_mpi_rank_row % my_cores_per_node_row;
  my_lrank_column = my_mpi_rank_column % my_cores_per_node_column;
  my_lrank_node = my_lrank_row * my_cores_per_node_column + my_lrank_column;
  my_shared_buf = shmem + NUM_BARRIERS;
  *(((int *)my_shared_buf) + my_lrank_column) = count;
  node_barrier(my_cores_per_node_row * my_cores_per_node_column);
  coarse_count = 0;
  for (i = 0; i < my_cores_per_node_column; i++) {
    counts[i] = *(((int *)my_shared_buf) + i);
    coarse_count += counts[i];
  }
  node_barrier(my_cores_per_node_row * my_cores_per_node_column);
  my_size_shared_buf =
      my_mpi_size_row * my_cores_per_node_column * coarse_count * type_size;
  isdryrun = 0;
  ip = my_code;
  if (my_cores_per_node_row * my_cores_per_node_column > 1) {
    allreduce_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, my_size_shared_buf, counts,
        my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
        my_lrank_node, my_lrank_row, my_lrank_column, my_node, coarse_count,
        num_ports, num_ports_limit, num_active_ports, global_ranks_p + 4,
        reduction_op, copyin, &ip, 1, isdryrun, &num_comm_max, 100);
  } else {
    allreduce_exec_bruck(
        sendbuf, recvbuf, type_size, my_shared_buf, my_size_shared_buf, counts,
        my_mpi_size_row, my_cores_per_node_row, my_cores_per_node_column,
        my_lrank_node, my_lrank_row, my_lrank_column, my_node, coarse_count,
        num_ports, num_ports_limit, num_active_ports, global_ranks_p + 4,
        reduction_op, copyin, &ip, 1, isdryrun, &num_comm_max, 100);
  }
  return (0);
}

int EXT_MPI_Get_size_rank_native(int handle, int *my_cores_per_node_row,
                                 int *my_cores_per_node_column,
                                 int *my_mpi_size_row, int *my_mpi_rank_row) {
  int *global_ranks_p;
  global_ranks_p = (int *)comm_comm[handle];
  *my_cores_per_node_row = global_ranks_p[0];
  *my_cores_per_node_column = global_ranks_p[1];
  *my_mpi_size_row = global_ranks_p[2];
  *my_mpi_rank_row = global_ranks_p[3];
  return (0);
}

int EXT_MPI_Allocate_native(MPI_Comm comm_row, int my_cores_per_node_row,
                            MPI_Comm comm_column, int my_cores_per_node_column,
                            int sharedmem_size, int locmem_size_) {
  setup_shared_memory(comm_row, my_cores_per_node_row, comm_column,
                      my_cores_per_node_column, sharedmem_size + NUM_BARRIERS,
                      &shmem_size, &shmemid, &shmem, 0, NUM_BARRIERS);
  locmem_size = locmem_size_;
  locmem = (char *)malloc(sizeof(char) * locmem_size);
  return (0);
}

static int counters_num_memcpy = 0;
static int counters_size_memcpy = 0;
static int counters_num_reduce = 0;
static int counters_size_reduce = 0;
static int counters_num_MPI_Irecv = 0;
static int counters_size_MPI_Irecv = 0;
static int counters_num_MPI_Isend = 0;
static int counters_size_MPI_Isend = 0;
static int counters_num_MPI_Recv = 0;
static int counters_size_MPI_Recv = 0;
static int counters_num_MPI_Send = 0;
static int counters_size_MPI_Send = 0;
static int counters_num_MPI_Sendrecv = 0;
static int counters_size_MPI_Sendrecv = 0;
static int counters_size_MPI_Sendrecvb = 0;

void EXT_MPI_Get_counters_native(struct EXT_MPI_Counters_native *var) {
  var->counters_num_memcpy = counters_num_memcpy;
  var->counters_size_memcpy = counters_size_memcpy;
  var->counters_num_reduce = counters_num_reduce;
  var->counters_size_reduce = counters_size_reduce;
  var->counters_num_MPI_Irecv = counters_num_MPI_Irecv;
  var->counters_size_MPI_Irecv = counters_size_MPI_Irecv;
  var->counters_num_MPI_Isend = counters_num_MPI_Isend;
  var->counters_size_MPI_Isend = counters_size_MPI_Isend;
  var->counters_num_MPI_Recv = counters_num_MPI_Recv;
  var->counters_size_MPI_Recv = counters_size_MPI_Recv;
  var->counters_num_MPI_Send = counters_num_MPI_Send;
  var->counters_size_MPI_Send = counters_size_MPI_Send;
  var->counters_num_MPI_Sendrecv = counters_num_MPI_Sendrecv;
  var->counters_size_MPI_Sendrecv = counters_size_MPI_Sendrecv;
  var->counters_size_MPI_Sendrecvb = counters_size_MPI_Sendrecvb;
}

void EXT_MPI_Set_counters_zero_native() {
  counters_num_memcpy = 0;
  counters_size_memcpy = 0;
  counters_num_reduce = 0;
  counters_size_reduce = 0;
  counters_num_MPI_Irecv = 0;
  counters_size_MPI_Irecv = 0;
  counters_num_MPI_Isend = 0;
  counters_size_MPI_Isend = 0;
  counters_num_MPI_Recv = 0;
  counters_size_MPI_Recv = 0;
  counters_num_MPI_Send = 0;
  counters_size_MPI_Send = 0;
  counters_num_MPI_Sendrecv = 0;
  counters_size_MPI_Sendrecv = 0;
  counters_size_MPI_Sendrecvb = 0;
}

static int simulate_native(char *ip) {
  char instruction, instruction2;
  void volatile *p1, *p2;
  int i1, i2, i3, i4, n_r, i;
  do {
    instruction = code_get_char(&ip);
    switch (instruction) {
    case OPCODE_RETURN:
      break;
    case OPCODE_MEMCPY:
      p1 = code_get_pointer(&ip);
      p2 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      counters_num_memcpy++;
      counters_size_memcpy += i1;
#ifdef VERBOSE
      printf("memcpy %p %p %d\n", (void *)p1, (void *)p2, i1);
#endif
      break;
    case OPCODE_MPIIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      p2 = code_get_pointer(&ip);
      counters_num_MPI_Irecv++;
      counters_size_MPI_Irecv += i1;
#ifdef VERBOSE
      printf("MPI_Irecv %p %d %d %p\n", (void *)p1, i1, i2, (void *)p2);
#endif
      break;
    case OPCODE_MPIISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      p2 = code_get_pointer(&ip);
      counters_num_MPI_Isend++;
      counters_size_MPI_Isend += i1;
#ifdef VERBOSE
      printf("MPI_Isend %p %d %d %p\n", (void *)p1, i1, i2, (void *)p2);
#endif
      break;
    case OPCODE_MPIWAITALL:
      i1 = code_get_int(&ip);
      p1 = code_get_pointer(&ip);
#ifdef VERBOSE
      printf("MPI_Waitall %d %p\n", i1, p1);
#endif
      break;
    case OPCODE_NODEBARRIER:
#ifdef VERBOSE
      printf("node_barrier\n");
#endif
      break;
    case OPCODE_SETNUMCORES:
      i1 = code_get_int(&ip);
#ifdef VERBOSE
      printf("num_cores %d\n", i1);
#endif
      break;
    case OPCODE_REDUCE:
      instruction2 = code_get_char(&ip);
      switch (instruction2) {
      case OPCODE_REDUCE_SUM_DOUBLE:
        p1 = code_get_pointer(&ip);
        p2 = code_get_pointer(&ip);
        i1 = code_get_int(&ip);
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(double);
#ifdef VERBOSE
        printf("REDUCE_SUM_DOUBLE %p %p %d\n", p1, p2, i1);
#endif
        break;
      case OPCODE_REDUCE_SUM_LONG_INT:
        p1 = code_get_pointer(&ip);
        p2 = code_get_pointer(&ip);
        i1 = code_get_int(&ip);
        counters_num_reduce++;
        counters_size_reduce += i1 * sizeof(long int);
#ifdef VERBOSE
        printf("REDUCE_SUM_LONG_INT %p %p %d\n", p1, p2, i1);
#endif
        break;
      }
      break;
    case OPCODE_MPISENDRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      p2 = code_get_pointer(&ip);
      i3 = code_get_int(&ip);
      i4 = code_get_int(&ip);
      counters_num_MPI_Sendrecv++;
      counters_size_MPI_Sendrecv += i1;
      counters_size_MPI_Sendrecvb += i3;
#ifdef VERBOSE
      printf("MPI_Sendrecv %p %d %d %p %d %d\n", (void *)p1, i1, i2, (void *)p2,
             i3, i4);
#endif
      break;
    case OPCODE_MPISEND:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      counters_num_MPI_Send++;
      counters_size_MPI_Send += i1;
#ifdef VERBOSE
      printf("MPI_Send %p %d %d\n", (void *)p1, i1, i2);
#endif
      break;
    case OPCODE_MPIRECV:
      p1 = code_get_pointer(&ip);
      i1 = code_get_int(&ip);
      i2 = code_get_int(&ip);
      counters_num_MPI_Recv++;
      counters_size_MPI_Recv += i1;
#ifdef VERBOSE
      printf("MPI_Recv %p %d %d\n", (void *)p1, i1, i2);
#endif
      break;
    case OPCODE_LOCALMEM:
      i1 = code_get_int(&ip);
      n_r = code_get_int(&ip);
#ifdef VERBOSE
      printf("relocation %d %d ", i1, n_r);
#endif
      for (i = 0; i < n_r; i++) {
        i2 = code_get_int(&ip);
#ifdef VERBOSE
        printf("%d ", i2);
#endif
      }
#ifdef VERBOSE
      printf("\n");
#endif
      break;
    default:
      printf("illegal MPI_OPCODE\n");
      exit(1);
    }
  } while (instruction != OPCODE_RETURN);
  return (0);
}

int EXT_MPI_Simulate_native(int handle) {
  return (simulate_native(comm_code[handle]));
}

int EXT_MPI_Dry_allgatherv_native(int mpi_size, int *counts, int *num_ports) {
  // allgatherv_exec_bruck(void *sendbuf, void *recvbuf, int type_size, char
  // volatile *my_shared_buf, int volatile *lshmem_counts, int my_mpi_size_row,
  // int my_cores_per_node_row, int my_cores_per_node_column, int my_lrank_node,
  // int my_lrank_row, int my_lrank_column, int my_node, int *coarse_counts, int
  // *coarse_displs, int *num_ports, int *num_parallel, int num_active_ports, int
  // *global_ranks, int *rank_back_perm, int *displs_perm, char **ip, int
  // single_instruction, int isdryrun, int *num_comm_max, int num_throttle, int
  // allreduce, int recvcount)
  char *ip;
  int *global_ranks, *num_parallel, *rank_back_perm, *displs_perm, *displs;
  int handle, num_comm_max, isdryrun, i;
  global_ranks = (int *)malloc(mpi_size * sizeof(int));
  num_parallel = (int *)malloc(mpi_size * sizeof(int));
  rank_back_perm = (int *)malloc(mpi_size * sizeof(int));
  displs_perm = (int *)malloc(mpi_size * sizeof(int));
  displs = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    global_ranks[i] = i;
    displs[i] = 0;
    displs_perm[i] = 0;
    rank_back_perm[i] = i;
    num_parallel[i] = 1;
  }
  handle = get_handle(&comm_code, &handle_code_max);
  ip = NULL;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    allgatherv_exec_bruck(NULL, NULL, sizeof(long int), NULL, counts, mpi_size,
                          1, 1, 0, 0, 0, 0, counts, displs, num_ports,
                          num_parallel, 1, global_ranks, rank_back_perm,
                          displs_perm, &ip, 0, isdryrun, &num_comm_max, 100, 1,
                          -1);
    if (isdryrun) {
      ip = comm_code[handle] = (char *)malloc(((void *)ip) - NULL);
    }
  }
  free(displs);
  free(displs_perm);
  free(rank_back_perm);
  free(num_parallel);
  free(global_ranks);
  return (handle);
}

int EXT_MPI_Dry_reduce_scatter_native(int mpi_size, int *counts,
                                      int *num_ports) {
  // reduce_scatter_exec_bruck(void *sendbuf, void *recvbuf, int type_size, char
  // volatile *my_shared_buf, char volatile *recv_buf_temp, int volatile
  // *lshmem_counts, int my_mpi_size_row, int my_cores_per_node_row, int
  // my_cores_per_node_column, int my_lrank_node, int my_lrank_row, int
  // my_lrank_column, int my_node, int *coarse_counts, int *coarse_displs, int
  // *num_ports, int *num_parallel, int num_active_ports, int *global_ranks, int
  // *rank_back_perm, int *displs_perm, int reduction_op, int copyin, char **ip,
  // int single_instruction, int isdryrun, int *num_comm_max, int allreduce, int
  // recvcount)
  char *ip;
  int *global_ranks, *num_parallel, *rank_back_perm, *displs_perm, *displs;
  int handle, num_comm_max, isdryrun, i;
  global_ranks = (int *)malloc(mpi_size * sizeof(int));
  num_parallel = (int *)malloc(mpi_size * sizeof(int));
  rank_back_perm = (int *)malloc(mpi_size * sizeof(int));
  displs_perm = (int *)malloc(mpi_size * sizeof(int));
  displs = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    global_ranks[i] = i;
    displs[i] = 0;
    displs_perm[i] = 0;
    rank_back_perm[i] = i;
    num_parallel[i] = 1;
  }
  handle = get_handle(&comm_code, &handle_code_max);
  ip = NULL;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    reduce_scatter_exec_bruck(
        NULL, NULL, sizeof(long int), NULL, NULL, counts, mpi_size, 1, 1, 0, 0,
        0, 0, counts, displs, num_ports, num_parallel, 1, global_ranks,
        rank_back_perm, displs_perm, OPCODE_REDUCE_SUM_LONG_INT, 0, &ip, 0,
        isdryrun, &num_comm_max, 1, -1);
    if (isdryrun) {
      ip = comm_code[handle] = (char *)malloc(((void *)ip) - NULL);
    }
  }
  free(displs);
  free(displs_perm);
  free(rank_back_perm);
  free(num_parallel);
  free(global_ranks);
  return (handle);
}

int EXT_MPI_Dry_allreduce_native(int mpi_size, int count, int *num_ports,
                                 int *num_ports_limit) {
  // allreduce_exec_bruck(void *sendbuf, void *recvbuf, int type_size, char
  // volatile *my_shared_buf, int base_size_shared, int *counts, int
  // my_mpi_size_row, int my_cores_per_node_row, int my_cores_per_node_column,
  // int my_lrank_node, int my_lrank_row, int my_lrank_column, int my_node, int
  // coarse_count, int *num_ports, int *num_ports_limit, int num_active_ports,
  // int *global_ranks, int reduction_op, int copyin, char **ip, int
  // single_instruction, int isdryrun, int *num_comm_max, int num_throttle);
  char *ip;
  int *global_ranks;
  int handle, num_comm_max, isdryrun, i;
  global_ranks = (int *)malloc(mpi_size * sizeof(int));
  for (i = 0; i < mpi_size; i++) {
    global_ranks[i] = i;
  }
  handle = get_handle(&comm_code, &handle_code_max);
  ip = NULL;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    allreduce_exec_bruck(
        NULL, NULL, sizeof(long int), NULL, 0, &count, mpi_size, 1, 1, 0, 0, 0,
        0, count, num_ports, num_ports_limit, 1, global_ranks,
        OPCODE_REDUCE_SUM_LONG_INT, 0, &ip, 0, isdryrun, &num_comm_max, 100);
    if (isdryrun) {
      ip = comm_code[handle] = (char *)malloc(((void *)ip) - NULL);
    }
  }
  free(global_ranks);
  return (handle);
}

static void my_allgatherv_init_native_big(
    void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf,
    int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm,
    int recvcount, int single_instruction, int mpi_size, int mpi_rank,
    int type_size, int *global_ranks, char **ip, int isdryrun) {
  char *i_start;
  int step, i;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, 1, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  i_start = *ip;
  code_put_char(ip, OPCODE_MEMCPY, isdryrun);
  code_put_pointer(
      ip, (void *)(((char *)recvbuf) + displs[mpi_rank] * type_size), isdryrun);
  code_put_pointer(ip, (void *)(char *)sendbuf, isdryrun);
  code_put_int(ip, sendcount * type_size, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  for (step = 1; step < mpi_size; step++) {
    i_start = *ip;
    if ((sendcount > 0) &&
        (recvcounts[(mpi_rank + mpi_size - step) % mpi_size] > 0)) {
      code_put_char(ip, OPCODE_MPISENDRECV, isdryrun);
    } else {
      if (sendcount > 0) {
        code_put_char(ip, OPCODE_MPISEND, isdryrun);
      } else {
        if (recvcounts[(mpi_rank + mpi_size - step) % mpi_size] > 0) {
          code_put_char(ip, OPCODE_MPIRECV, isdryrun);
        }
      }
    }
    if (sendcount > 0) {
      code_put_pointer(ip, (void *)(char *)sendbuf, isdryrun);
      code_put_int(ip, sendcount * type_size, isdryrun);
      code_put_int(ip, global_ranks[(mpi_rank + step) % mpi_size], isdryrun);
    }
    if (recvcounts[(mpi_rank + mpi_size - step) % mpi_size] > 0) {
      code_put_pointer(
          ip,
          (void *)(((char *)recvbuf) +
                   displs[(mpi_rank + mpi_size - step) % mpi_size] * type_size),
          isdryrun);
      code_put_int(
          ip, recvcounts[(mpi_rank + mpi_size - step) % mpi_size] * type_size,
          isdryrun);
      code_put_int(ip, global_ranks[(mpi_rank + mpi_size - step) % mpi_size],
                   isdryrun);
    }
    exec_single(ip, i_start, single_instruction, isdryrun);
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
}

int EXT_MPI_Allgatherv_init_native_big(void *sendbuf, int sendcount,
                                       MPI_Datatype sendtype, void *recvbuf,
                                       int *recvcounts, int *displs,
                                       MPI_Datatype recvtype, MPI_Comm comm) {
  char *ip;
  int *global_ranks, handle, isdryrun;
  int mpi_size, mpi_rank, type_size;
  MPI_Comm_size(comm, &mpi_size);
  MPI_Comm_rank(comm, &mpi_rank);
  MPI_Type_size(sendtype, &type_size);
  global_ranks = (int *)malloc(sizeof(int) * mpi_size);
  setup_rank_translation(comm, 1, MPI_COMM_NULL, 1, global_ranks);
  handle = get_handle(&comm_code, &handle_code_max);
  ip = NULL;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    my_allgatherv_init_native_big(sendbuf, sendcount, sendtype, recvbuf,
                                  recvcounts, displs, recvtype, comm, -1, 0,
                                  mpi_size, mpi_rank, type_size, global_ranks,
                                  &ip, isdryrun);
    if (isdryrun) {
      ip = comm_code[handle] = (char *)malloc(((void *)ip) - NULL);
    }
  }
  free(global_ranks);
  return (handle);
}

int EXT_MPI_Allgatherv_native_big(void *sendbuf, int sendcount,
                                  MPI_Datatype sendtype, void *recvbuf,
                                  int *recvcounts, int *displs,
                                  MPI_Datatype recvtype, MPI_Comm comm,
                                  int recvcount) {
  return (0);
}

void my_reduce_scatter_init_native_big(
    void *sendbuf, void *recvbuf, int *recvcounts, int *displs,
    MPI_Datatype datatype, MPI_Comm comm, int reduction_op, int recvcount,
    int single_instruction, int mpi_size, int mpi_rank, int type_size,
    int *global_ranks, char **ip, int isdryrun) {
  char *i_start, *m_start, *m_count, *rlocmem;
  int recvcount_max, step, i;
  i_start = *ip;
  code_put_char(ip, OPCODE_SETNUMCORES, isdryrun);
  code_put_int(ip, 1, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  recvcount_max = 0;
  for (i = 0; i < mpi_size; i++) {
    if (recvcounts[i] > recvcount_max) {
      recvcount_max = recvcounts[i];
    }
  }
  if (single_instruction) {
    rlocmem = (char *)malloc(recvcount_max * type_size);
  } else {
    rlocmem = NULL;
    code_put_char(ip, OPCODE_LOCALMEM, isdryrun);
    code_put_int(ip, recvcount_max * type_size, isdryrun);
    code_put_int(ip, (mpi_size - 1) * 2, isdryrun);
    m_start = m_count = *ip;
    for (i = 0; i < (mpi_size - 1) * 2; i++) {
      code_put_int(ip, 0, isdryrun);
    }
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_MEMCPY, isdryrun);
  code_put_pointer(ip, (void *)(char *)recvbuf, isdryrun);
  code_put_pointer(
      ip, (void *)(((char *)sendbuf) + displs[mpi_rank] * type_size), isdryrun);
  code_put_int(ip, recvcounts[mpi_rank] * type_size, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  for (step = 1; step < mpi_size; step++) {
    i_start = *ip;
    if ((recvcounts[mpi_rank] > 0) &&
        (recvcounts[(mpi_rank + mpi_size - step) % mpi_size] > 0)) {
      code_put_char(ip, OPCODE_MPISENDRECV, isdryrun);
    } else {
      if (recvcounts[mpi_rank] > 0) {
        code_put_char(ip, OPCODE_MPISEND, isdryrun);
      } else {
        if (recvcounts[(mpi_rank + mpi_size - step) % mpi_size] > 0) {
          code_put_char(ip, OPCODE_MPIRECV, isdryrun);
        }
      }
    }
    if (recvcounts[(mpi_rank + step) % mpi_size] > 0) {
      code_put_pointer(
          ip, (void *)(((char *)sendbuf) + displs[mpi_rank] * type_size),
          isdryrun);
      code_put_int(ip, recvcounts[(mpi_rank + step) % mpi_size] * type_size,
                   isdryrun);
      code_put_int(ip, global_ranks[(mpi_rank + step) % mpi_size], isdryrun);
    }
    if (recvcounts[mpi_rank] > 0) {
      code_put_int(&m_count, ((void *)*ip) - ((void *)m_start), isdryrun);
      code_put_pointer(ip, (void *)rlocmem, isdryrun);
      code_put_int(ip, recvcounts[mpi_rank] * type_size, isdryrun);
      code_put_int(ip, global_ranks[(mpi_rank + mpi_size - step) % mpi_size],
                   isdryrun);
      code_put_char(ip, OPCODE_REDUCE, isdryrun);
      code_put_char(ip, reduction_op, isdryrun);
      code_put_pointer(ip, (void *)(char *)recvbuf, isdryrun);
      code_put_int(&m_count, ((void *)*ip) - ((void *)m_start), isdryrun);
      code_put_pointer(ip, (void *)rlocmem, isdryrun);
      code_put_int(ip, recvcounts[mpi_rank], isdryrun);
    }
    exec_single(ip, i_start, single_instruction, isdryrun);
  }
  i_start = *ip;
  code_put_char(ip, OPCODE_RETURN, isdryrun);
  exec_single(ip, i_start, single_instruction, isdryrun);
  if (single_instruction) {
    free(rlocmem);
  }
}

int EXT_MPI_Reduce_scatter_init_native_big(void *sendbuf, void *recvbuf,
                                           int *recvcounts, int *displs,
                                           MPI_Datatype datatype, MPI_Comm comm,
                                           MPI_Op op) {
  int reduction_op, handle, isdryrun;
  int *global_ranks, mpi_size, mpi_rank, type_size;
  char *ip;
  int step, i;
  if ((op == MPI_SUM) && (datatype == MPI_DOUBLE)) {
    reduction_op = OPCODE_REDUCE_SUM_DOUBLE;
  }
  if ((op == MPI_SUM) && (datatype == MPI_LONG)) {
    reduction_op = OPCODE_REDUCE_SUM_LONG_INT;
  }
  MPI_Comm_size(comm, &mpi_size);
  MPI_Comm_rank(comm, &mpi_rank);
  MPI_Type_size(datatype, &type_size);
  global_ranks = (int *)malloc(sizeof(int) * mpi_size);
  setup_rank_translation(comm, 1, MPI_COMM_NULL, 1, global_ranks);
  handle = get_handle(&comm_code, &handle_code_max);
  ip = NULL;
  for (isdryrun = 1; isdryrun >= 0; isdryrun--) {
    my_reduce_scatter_init_native_big(
        sendbuf, recvbuf, recvcounts, displs, datatype, comm, reduction_op, -1,
        0, mpi_size, mpi_rank, type_size, global_ranks, &ip, isdryrun);
    if (isdryrun) {
      ip = comm_code[handle] = (char *)malloc(((void *)ip) - NULL);
    }
  }
  free(global_ranks);
  return (handle);
}
