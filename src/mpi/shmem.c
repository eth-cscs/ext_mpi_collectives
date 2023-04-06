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
#include "read_write.h"
#include "shmem.h"
#include <mpi.h>

void ext_mpi_node_barrier_mpi(MPI_Comm shmem_comm_node_row,
                              MPI_Comm shmem_comm_node_column, char *comm_code) {
  struct header_byte_code *header;
  if (comm_code) {
    header = (struct header_byte_code *)comm_code;
    if (comm_code+header->size_to_return) {
      shmem_comm_node_row = *((MPI_Comm *)(comm_code+header->size_to_return));
    } else {
      shmem_comm_node_row = MPI_COMM_NULL;
    }
    if (comm_code+header->size_to_return+sizeof(MPI_Comm)) {
      shmem_comm_node_column = *((MPI_Comm *)(comm_code+header->size_to_return+sizeof(MPI_Comm)));
    } else {
      shmem_comm_node_column = MPI_COMM_NULL;
    }
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
		            char *name, int name_index) {
  int rank_global, size_global;
  char name_org[9000];
  MPI_Comm_rank(MPI_COMM_WORLD, &rank_global);
  MPI_Comm_size(MPI_COMM_WORLD, &size_global);
  PMPI_Allreduce(MPI_IN_PLACE, &rank_global, 1, MPI_INT, MPI_MIN, comm_node_row);
  if (comm_node_column != MPI_COMM_NULL) {
    PMPI_Allreduce(MPI_IN_PLACE, &rank_global, 1, MPI_INT, MPI_MIN,
                   comm_node_column);
  }
  strcpy(name_org, name);
  sprintf(name, "%s_%d", name_org, rank_global + size_global * name_index);
}
#endif

int ext_mpi_destroy_shared_memory(int size_shared, int num_segments, int *shmemid,
                                  char **shmem, char *comm_code) {
  int i;
  for (i = 0; i < num_segments; i++) {
#ifndef MMAP
    if (shmemid[i] == -2) {
      free(shmem[i]);
    } else {
      ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
      shmdt((void *)(shmem[i]));
      ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
      if (shmemid[i] != -1) {
        shmctl(shmemid[i], IPC_RMID, NULL);
      }
      ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
    }
#else
    munmap((void *)(shmem[i]), size_shared);
#endif
  }
  free(shmemid);
  free(shmem);
  return 0;
}

int ext_mpi_setup_shared_memory(MPI_Comm *shmem_comm_node_row,
                                MPI_Comm *shmem_comm_node_column,
                                MPI_Comm comm_row, int my_cores_per_node_row,
                                MPI_Comm comm_column,
                                int my_cores_per_node_column, int *size_shared,
                                int num_segments, int **shmemid,
                                char ***shmem, char fill, int numfill,
				char **comm_code) {
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column, my_mpi_size_column, i, ii;
#ifdef MMAP
  int shmem_fd = -1;
  char shmem_name[10000];
  static int shmem_name_index = 0;
#else
  int single_task;
  single_task = my_cores_per_node_row * my_cores_per_node_column == 1 && num_segments == 1;
#endif
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  PMPI_Allreduce(MPI_IN_PLACE, size_shared, 1, MPI_INT, MPI_MAX, comm_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
    PMPI_Allreduce(MPI_IN_PLACE, size_shared, 1, MPI_INT, MPI_MAX, comm_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  if (*size_shared > 0) {
    *size_shared = (((*size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  PMPI_Comm_split(comm_row, my_mpi_rank_row / (my_cores_per_node_row * num_segments),
                  my_mpi_rank_row % (my_cores_per_node_row * num_segments), shmem_comm_node_row);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                    my_mpi_rank_column % my_cores_per_node_column,
                    shmem_comm_node_column);
  } else {
    *shmem_comm_node_column = MPI_COMM_NULL;
  }
  *shmemid = (int *)malloc(num_segments * sizeof(int));
  *shmem = (char **)malloc(num_segments * sizeof(char *));
  for (i = 0; i < num_segments; i++) {
    ii = (num_segments - i + my_mpi_rank_row / my_cores_per_node_row) % num_segments;
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    (*shmemid)[ii] = -1;
    (*shmem)[ii] = NULL;
#ifndef MMAP
    if ((my_mpi_rank_row % (my_cores_per_node_row * num_segments) == i * my_cores_per_node_row) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0)) {
      if (!single_task) {
        (*shmemid)[ii] = shmget(IPC_PRIVATE, *size_shared, IPC_CREAT | 0600);
      } else {
	(*shmemid)[ii] = -2;
      }
    }
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    MPI_Bcast(&((*shmemid)[ii]), 1, MPI_INT, i * my_cores_per_node_row, *shmem_comm_node_row);
    if (*shmem_comm_node_column != MPI_COMM_NULL) {
      MPI_Bcast(&((*shmemid)[ii]), 1, MPI_INT, 0, *shmem_comm_node_column);
    }
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    if ((my_mpi_rank_row % (my_cores_per_node_row * num_segments)) / my_cores_per_node_row == i) {
      if (!single_task) {
        (*shmem)[ii] = (char *)shmat((*shmemid)[ii], NULL, 0);
      } else {
        (*shmem)[ii] = (char *)malloc(*size_shared);
      }
    } else {
      if (!single_task) {
        (*shmem)[ii] = (char *)shmat((*shmemid)[ii], NULL, SHM_RDONLY);
      } else {
        (*shmem)[ii] = (char *)malloc(*size_shared);
      }
    }
    if ((*shmem)[ii] == NULL)
      goto error;
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    if ((my_mpi_rank_row % (my_cores_per_node_row * num_segments) == i * my_cores_per_node_row) &&
         (my_mpi_rank_column % my_cores_per_node_column == 0)) {
      memset((void *)((*shmem)[ii] + (*size_shared - numfill)), fill, numfill);
    }
#else
    sprintf(shmem_name, "/ext_mpi");
    gen_shared_name(*shmem_comm_node_row, *shmem_comm_node_column, shmem_name, shmem_name_index++);
    if ((my_mpi_rank_row % (my_cores_per_node_row * num_segments) == i * my_cores_per_node_row) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0)) {
      shmem_fd = shm_open(shmem_name, O_RDWR | O_CREAT, 0600);
      if (shmem_fd == -1) {
        printf("not enough shared memory\n");
        goto error;
      }
      if (ftruncate(shmem_fd, *size_shared) != 0) {
        printf("not enough shared memory\n");
        goto error;
      }
    }
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    if (!((my_mpi_rank_row % (my_cores_per_node_row * num_segments) == i * my_cores_per_node_row) &&
         (my_mpi_rank_column % my_cores_per_node_column == 0))) {
      shmem_fd = shm_open(shmem_name, O_RDWR, 0600);
      if (shmem_fd == -1) {
        printf("not enough shared memory\n");
        goto error;
      }
    }
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    (*shmem)[ii] = (char *)mmap(NULL, *size_shared, PROT_READ | PROT_WRITE, MAP_SHARED,
                               shmem_fd, 0);
    if ((*shmem)[ii] == MAP_FAILED) {
      printf("not enough shared memory\n");
      goto error;
    }
    close(shmem_fd);
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
    memset((void *)((*shmem)[ii] + (*size_shared - numfill)), fill, numfill);
    (*shmemid)[ii] = -1;
#endif
  }
  ext_mpi_node_barrier_mpi(*shmem_comm_node_row, *shmem_comm_node_column, NULL);
  PMPI_Comm_free(shmem_comm_node_row);
  if (*shmem_comm_node_column != MPI_COMM_NULL) {
    PMPI_Comm_free(shmem_comm_node_column);
  }
  return 0;
error:
  return ERROR_SHMEM;
}
