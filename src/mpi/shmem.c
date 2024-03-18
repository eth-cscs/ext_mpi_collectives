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
    if (*((void **)(comm_code+header->size_to_return))) {
      shmem_comm_node_row = *((MPI_Comm *)(comm_code+header->size_to_return+sizeof(void*)));
    } else {
      shmem_comm_node_row = MPI_COMM_NULL;
    }
    if (*((void **)(comm_code+header->size_to_return+sizeof(MPI_Comm)+sizeof(void*)))) {
      shmem_comm_node_column = *((MPI_Comm *)(comm_code+header->size_to_return+sizeof(MPI_Comm)+2*sizeof(void*)));
    } else {
      shmem_comm_node_column = MPI_COMM_NULL;
    }
  }
  if (shmem_comm_node_row != MPI_COMM_NULL) {
    PMPI_Barrier(shmem_comm_node_row);
  }
  if (shmem_comm_node_column != MPI_COMM_NULL) {
    PMPI_Barrier(shmem_comm_node_column);
    if (shmem_comm_node_row != MPI_COMM_NULL) {
      PMPI_Barrier(shmem_comm_node_row);
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

int ext_mpi_setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row, int num_sockets_per_node,
                                int *size_shared, int **shmemid, char ***shmem, char fill, int numfill,
				MPI_Comm *shmem_comm_node_row) {
  int my_mpi_rank_row, my_mpi_size_row, shmemid_temp, i, j, k;
  char *shmem_temp;
#ifdef MMAP
  int shmem_fd = -1;
  char shmem_name[10000];
  static int shmem_name_index = 0;
#else
  int single_task;
  single_task = my_cores_per_node_row == 1 && num_sockets_per_node == 1;
#endif
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  PMPI_Allreduce(MPI_IN_PLACE, size_shared, 1, MPI_INT, MPI_MAX, comm_row);
  if (*size_shared > 0) {
    *size_shared = (((*size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  PMPI_Comm_split(comm_row, my_mpi_rank_row / (my_cores_per_node_row * num_sockets_per_node),
                  my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node), shmem_comm_node_row);
  *shmemid = (int *)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  *shmem = (char **)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(char *));
  for (i = 0; i < my_cores_per_node_row * num_sockets_per_node; i++) {
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
    (*shmemid)[i] = -1;
    (*shmem)[i] = NULL;
#ifndef MMAP
    if (i == my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node)) {
      if (!single_task) {
        (*shmemid)[i] = shmget(IPC_PRIVATE, *size_shared, IPC_CREAT | 0600);
      } else {
	(*shmemid)[i] = -2;
      }
    }
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
    MPI_Bcast(&((*shmemid)[i]), 1, MPI_INT, i, *shmem_comm_node_row);
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
    if (!single_task) {
      (*shmem)[i] = (char *)shmat((*shmemid)[i], NULL, 0);
    } else {
      (*shmem)[i] = (char *)malloc(*size_shared);
    }
    if ((*shmem)[i] == NULL)
      goto error;
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
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
  for (j = 0; j < (my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node)) / my_cores_per_node_row * my_cores_per_node_row; j++) {
    shmem_temp = (*shmem)[0];
    shmemid_temp = (*shmemid)[0];
    for (i = 0; i < my_cores_per_node_row * num_sockets_per_node - 1; i++) {
      (*shmem)[i] = (*shmem)[i + 1];
      (*shmemid)[i] = (*shmemid)[i + 1];
    }
    (*shmem)[my_cores_per_node_row * num_sockets_per_node - 1] = shmem_temp;
    (*shmemid)[my_cores_per_node_row * num_sockets_per_node - 1] = shmemid_temp;
  }
  for (k = 0; k < num_sockets_per_node; k++) {
    for (j = 0; j < my_mpi_rank_row % my_cores_per_node_row; j++) {
      shmem_temp = (*shmem)[my_cores_per_node_row * k];
      shmemid_temp = (*shmemid)[my_cores_per_node_row * k];
      for (i = my_cores_per_node_row * k; i < my_cores_per_node_row * (k + 1) - 1; i++) {
        (*shmem)[i] = (*shmem)[i + 1];
        (*shmemid)[i] = (*shmemid)[i + 1];
      }
      (*shmem)[my_cores_per_node_row * (k + 1) - 1] = shmem_temp;
      (*shmemid)[my_cores_per_node_row * (k + 1) - 1] = shmemid_temp;
    }
  }
  memset((void *)((*shmem)[0] + (*size_shared - numfill)), fill, numfill);
  ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
  return 0;
error:
  PMPI_Comm_free(shmem_comm_node_row);
  return ERROR_SHMEM;
}
