#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
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
    if (*((void **)(comm_code+header->size_to_return+sizeof(MPI_Comm)+2*sizeof(void*)))) {
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

int ext_mpi_destroy_shared_memory(int num_segments, int *sizes_shared, int *shmemid,
                                  char **shmem, char *comm_code) {
  int i;
  for (i = 0; i < num_segments; i++) {
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
  }
  free(shmemid);
  free(shmem);
  free(sizes_shared);
  return 0;
}

int ext_mpi_setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row, int num_sockets_per_node,
                                int size_shared, int **sizes_shared, int **shmemid, char ***shmem,
				MPI_Comm *shmem_comm_node_row) {
  int my_mpi_rank_row, my_mpi_size_row, shmemid_temp, single_task, size_shared_temp, i, j, k;
  char *shmem_temp;
  single_task = my_cores_per_node_row == 1 && num_sockets_per_node == 1;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (size_shared > 0) {
    size_shared = (((size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  PMPI_Comm_split(comm_row, my_mpi_rank_row / (my_cores_per_node_row * num_sockets_per_node),
                  my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node), shmem_comm_node_row);
  *shmemid = (int *)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  *shmem = (char **)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(char *));
  for (i = 0; i < my_cores_per_node_row * num_sockets_per_node; i++) {
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
    (*shmemid)[i] = -1;
    (*shmem)[i] = NULL;
    if (i == my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node)) {
      if (!single_task) {
        (*shmemid)[i] = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0600);
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
      (*shmem)[i] = (char *)malloc(size_shared);
    }
    if ((*shmem)[i] == NULL)
      goto error;
    ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
  }
  *sizes_shared = (int*)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  MPI_Allgather(&size_shared, 1, MPI_INT, *sizes_shared, 1, MPI_INT, *shmem_comm_node_row);
  for (j = 0; j < (my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node)) / my_cores_per_node_row * my_cores_per_node_row; j++) {
    shmem_temp = (*shmem)[0];
    shmemid_temp = (*shmemid)[0];
    size_shared_temp = (*sizes_shared)[0];
    for (i = 0; i < my_cores_per_node_row * num_sockets_per_node - 1; i++) {
      (*shmem)[i] = (*shmem)[i + 1];
      (*shmemid)[i] = (*shmemid)[i + 1];
      (*sizes_shared)[i] = (*sizes_shared)[i + 1];
    }
    (*shmem)[my_cores_per_node_row * num_sockets_per_node - 1] = shmem_temp;
    (*shmemid)[my_cores_per_node_row * num_sockets_per_node - 1] = shmemid_temp;
    (*sizes_shared)[my_cores_per_node_row * num_sockets_per_node - 1] = size_shared_temp;
  }
  for (k = 0; k < num_sockets_per_node; k++) {
    for (j = 0; j < my_mpi_rank_row % my_cores_per_node_row; j++) {
      shmem_temp = (*shmem)[my_cores_per_node_row * k];
      shmemid_temp = (*shmemid)[my_cores_per_node_row * k];
      size_shared_temp = (*sizes_shared)[my_cores_per_node_row * k];
      for (i = my_cores_per_node_row * k; i < my_cores_per_node_row * (k + 1) - 1; i++) {
        (*shmem)[i] = (*shmem)[i + 1];
        (*shmemid)[i] = (*shmemid)[i + 1];
	(*sizes_shared)[i] = (*sizes_shared)[i + 1];
      }
      (*shmem)[my_cores_per_node_row * (k + 1) - 1] = shmem_temp;
      (*shmemid)[my_cores_per_node_row * (k + 1) - 1] = shmemid_temp;
      (*sizes_shared)[my_cores_per_node_row * (k + 1) - 1] = size_shared_temp;
    }
  }
  memset((void *)((*shmem)[0]), 0, size_shared);
  ext_mpi_node_barrier_mpi(*shmem_comm_node_row, MPI_COMM_NULL, NULL);
  return 0;
error:
  PMPI_Comm_free(shmem_comm_node_row);
  return ERROR_SHMEM;
}
