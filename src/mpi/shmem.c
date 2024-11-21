#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include "constants.h"
#include "byte_code.h"
#include "read_write.h"
#include "ext_mpi.h"
#include "dmm.h"
#include "shmem.h"
#include <mpi.h>

static int mpi_size_node_global = -1;
static int mpi_rank_node_global = -1;
static int *sizes_shared_global = NULL;
static int *shmemid_global = NULL;
static char **shmem_global = NULL;

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

void * ext_mpi_init_shared_memory(MPI_Comm comm_world, int size_shared) {
  int my_mpi_rank_row, my_mpi_size_row, my_cores_per_node, i;
  MPI_Comm shmem_comm_node;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  if (my_cores_per_node <= 1) {
    return NULL;
  }
  MPI_Comm_size(comm_world, &my_mpi_size_row);
  MPI_Comm_rank(comm_world, &my_mpi_rank_row);
  if (size_shared > 0) {
    size_shared = (((size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  PMPI_Comm_split(comm_world, my_mpi_rank_row / my_cores_per_node,
                  my_mpi_rank_row % my_cores_per_node, &shmem_comm_node);
  MPI_Comm_size(shmem_comm_node, &mpi_size_node_global);
  MPI_Comm_rank(shmem_comm_node, &mpi_rank_node_global);
  shmemid_global = (int *)malloc(my_cores_per_node * sizeof(int));
  shmem_global = (char **)malloc(my_cores_per_node * sizeof(char *));
  for (i = 0; i < my_cores_per_node; i++) {
    PMPI_Barrier(shmem_comm_node);
    shmemid_global[i] = -1;
    shmem_global[i] = NULL;
    if (i == my_mpi_rank_row % my_cores_per_node) {
      shmemid_global[i] = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0600);
      if (shmemid_global[i] == -1) {
	printf("shmget: not enough shared memory\n");
	exit(1);
      }
    }
    PMPI_Barrier(shmem_comm_node);
    MPI_Bcast(&(shmemid_global[i]), 1, MPI_INT, i, shmem_comm_node);
    PMPI_Barrier(shmem_comm_node);
    shmem_global[i] = (char *)shmat(shmemid_global[i], NULL, 0);
    if (shmem_global[i] == (void *) -1) {
      printf("error shmat\n");
      exit(1);
    }
    if (shmem_global[i] == NULL)
      goto error;
    PMPI_Barrier(shmem_comm_node);
  }
  sizes_shared_global = (int*)malloc(my_cores_per_node * sizeof(int));
  MPI_Allgather(&size_shared, 1, MPI_INT, sizes_shared_global, 1, MPI_INT, shmem_comm_node);
  memset((void *)(shmem_global[0]), 0, size_shared);
  PMPI_Barrier(shmem_comm_node);
  PMPI_Comm_free(&shmem_comm_node);
  return shmem_global[mpi_rank_node_global];
error:
  PMPI_Comm_free(&shmem_comm_node);
  return NULL;
}

int ext_mpi_done_shared_memory(MPI_Comm comm_world) {
  int my_cores_per_node, my_mpi_size, my_mpi_rank, i;
  MPI_Comm comm_node;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  if (my_cores_per_node <= 1) {
    return -1;
  }
  PMPI_Comm_size(comm_world, &my_mpi_size);
  PMPI_Comm_rank(comm_world, &my_mpi_rank);
  PMPI_Comm_split(comm_world, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &comm_node);
  for (i = 0; i < my_cores_per_node; i++) {
    if (shmemid_global[i] == -2) {
      free(shmem_global[i]);
    } else {
      PMPI_Barrier(comm_world);
      shmdt((void *)(shmem_global[i]));
      PMPI_Barrier(comm_world);
      if (shmemid_global[i] != -1) {
        shmctl(shmemid_global[i], IPC_RMID, NULL);
      }
      PMPI_Barrier(comm_world);
    }
  }
  if (PMPI_Comm_free(&comm_node) != MPI_SUCCESS) {
    printf("error in PMPI_Comm_free in shmem.c\n");
    exit(1);
  }
  free(shmemid_global);
  free(shmem_global);
  free(sizes_shared_global);
  return 0;
}

int ext_mpi_setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row, int num_sockets_per_node,
                                int size_shared, int **sizes_shared, int **shmemid, char ***shmem,
				MPI_Comm *shmem_comm_node_row) {
  int my_mpi_rank_row, my_mpi_size_row, shmemid_temp, single_task, size_shared_temp, *ranks_global, i, j, k;
  char *shmem_temp;
  long int offset, *offsets;
  single_task = my_cores_per_node_row == 1 && num_sockets_per_node == 1;
  MPI_Comm_size(comm_row, &my_mpi_size_row);
  MPI_Comm_rank(comm_row, &my_mpi_rank_row);
  if (size_shared > 0) {
    size_shared = (((size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  PMPI_Comm_split(comm_row, my_mpi_rank_row / (my_cores_per_node_row * num_sockets_per_node),
                  my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node), shmem_comm_node_row);
  *shmemid = (int *)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  memset(*shmemid, 0, my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  *shmem = (char **)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(char *));
  memset(*shmem, 0, my_cores_per_node_row * num_sockets_per_node * sizeof(char *));
  ranks_global = (int *)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  PMPI_Allgather(&mpi_rank_node_global, 1, MPI_INT, ranks_global, 1, MPI_INT, *shmem_comm_node_row);
  i = my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node);
  if (!single_task) {
    (*shmemid)[i] = -1;
    (*shmem)[i] = dmalloc(size_shared);
    if (!(*shmem)[i]) {
      printf("dalloc: not enough shared memory\n");
      exit(1);
    }
    offset = (*shmem)[i] - shmem_global[mpi_rank_node_global];
    offsets = (long int*)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(long int));
    PMPI_Allgather(&offset, 1, MPI_LONG, offsets, 1, MPI_LONG, *shmem_comm_node_row);
    for (i = 0; i < my_cores_per_node_row * num_sockets_per_node; i++) {
      (*shmem)[i] = shmem_global[ranks_global[i]] + offsets[i];
    }
    free(offsets);
  } else {
    (*shmemid)[i] = -2;
    (*shmem)[i] = malloc(size_shared);
  }
  free(ranks_global);
  *sizes_shared = (int*)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  PMPI_Allgather(&size_shared, 1, MPI_INT, *sizes_shared, 1, MPI_INT, *shmem_comm_node_row);
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
//error:
//  PMPI_Comm_free(shmem_comm_node_row);
//  return ERROR_SHMEM;
}

int ext_mpi_destroy_shared_memory(int num_segments, int *sizes_shared, int *shmemid,
                                  char **shmem, char *comm_code) {
  int i;
  ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
  if (shmemid[mpi_rank_node_global] == -2) {
    free(shmem[mpi_rank_node_global]);
  } else {
    for (i = 0; i < num_segments; i++) {
      if (shmemid[i] == -1) {
        dfree(shmem[i]);
      }
    }
  }
  ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
  free(shmemid);
  free(shmem);
  free(sizes_shared);
  return 0;
}
