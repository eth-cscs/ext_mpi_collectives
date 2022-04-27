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
#include "constants.h"
#include "byte_code.h"
#include "shmem.h"
#include <mpi.h>

void ext_mpi_node_barrier_mpi(int handle, MPI_Comm shmem_comm_node_row,
                              MPI_Comm shmem_comm_node_column, char **comm_code) {
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

int ext_mpi_destroy_shared_memory(int handle, int *size_shared, int *shmemid,
                                  volatile char **shmem, char **comm_code) {
  if (*shmem != NULL) {
#ifndef MMAP
    ext_mpi_node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
    shmdt((void *)*shmem);
    ext_mpi_node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
    if (*shmemid != -1) {
      shmctl(*shmemid, IPC_RMID, NULL);
    }
    ext_mpi_node_barrier_mpi(handle, MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
#else
    munmap((void *)*shmem, *size_shared);
#endif
    *size_shared = 0;
    *shmem = NULL;
    *shmemid = -1;
  }
  return (0);
}

int ext_mpi_setup_shared_memory(MPI_Comm *shmem_comm_node_row,
                                MPI_Comm *shmem_comm_node_column,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int size_shared,
                                int *size_shared_old, int *shmemid,
                                volatile char **shmem, char fill, int numfill,
				char **comm_code) {
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
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
#ifndef MMAP
  if ((my_mpi_rank_row % my_cores_per_node_row == 0) &&
      (my_mpi_rank_column % my_cores_per_node_column == 0)) {
    (*shmemid) = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0600);
  }
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  MPI_Bcast(shmemid, 1, MPI_INT, 0, *shmem_comm_node_row);
  if (*shmem_comm_node_column != MPI_COMM_NULL) {
    MPI_Bcast(shmemid, 1, MPI_INT, 0, *shmem_comm_node_column);
  }
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  (*shmem) = (char *)shmat(*shmemid, NULL, 0);
  if ((*shmem) == NULL)
    goto error;
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0))) {
    (*shmemid) = -1;
  } else {
    shmctl(*shmemid, IPC_RMID, NULL);
    memset((void *)(*shmem + (size_shared - numfill)), fill, numfill);
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
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  if (!((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0))) {
    shmem_fd = shm_open(shmem_name, O_RDWR, 0600);
    if (shmem_fd == -1) {
      printf("not enough shared memory\n");
      exit(2);
    }
  }
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  *shmem = (char *)mmap(NULL, size_shared, PROT_READ | PROT_WRITE, MAP_SHARED,
                        shmem_fd, 0);
  if (shmem == MAP_FAILED) {
    printf("not enough shared memory\n");
    exit(2);
  }
  close(shmem_fd);
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  memset((void *)(*shmem + (size_shared - numfill)), fill, numfill);
  (*shmemid) = -1;
#endif
  ext_mpi_node_barrier_mpi(-1, *shmem_comm_node_row, *shmem_comm_node_column, comm_code);
  *size_shared_old = size_shared;
  return 0;
error:
  return ERROR_SHMEM;
}
