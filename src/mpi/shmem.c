#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include "constants.h"
#include "byte_code.h"
#include "read_write.h"
#include "ext_mpi_native.h"
#include "ext_mpi.h"
#include "dmm.h"
#include "shmem.h"
#include <mpi.h>

extern MPI_Comm ext_mpi_COMM_WORLD_dup;

static int mpi_size_node_global = -1;
static int mpi_rank_node_global = -1;
static int *sizes_shared_global = NULL;
static int *shmemid_global = NULL;
static char **shmem_global = NULL;
static long int *shmem_offsets = NULL;

long int * ext_mpi_get_shmem_offsets() {
  return shmem_offsets;
}

void * ext_mpi_init_shared_memory(MPI_Comm comm_world, size_t size_shared) {
  int my_mpi_rank_row, my_mpi_size_row, my_cores_per_node, i;
  MPI_Comm shmem_comm_node;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  if (my_cores_per_node <= 1) {
    return NULL;
  }
  ext_mpi_call_mpi(MPI_Comm_size(comm_world, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_world, &my_mpi_rank_row));
  if (size_shared > 0) {
    size_shared = (((size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  ext_mpi_call_mpi(PMPI_Comm_split(comm_world, my_mpi_rank_row / my_cores_per_node,
                  my_mpi_rank_row % my_cores_per_node, &shmem_comm_node));
  ext_mpi_call_mpi(MPI_Comm_size(shmem_comm_node, &mpi_size_node_global));
  ext_mpi_call_mpi(MPI_Comm_rank(shmem_comm_node, &mpi_rank_node_global));
  shmemid_global = (int *)malloc(my_cores_per_node * sizeof(int));
  shmem_global = (char **)malloc(my_cores_per_node * sizeof(char *));
  for (i = 0; i < my_cores_per_node; i++) {
    ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
    shmemid_global[i] = -1;
    shmem_global[i] = NULL;
    if (i == my_mpi_rank_row % my_cores_per_node) {
      shmemid_global[i] = shmget(IPC_PRIVATE, size_shared, IPC_CREAT | 0600);
      if (shmemid_global[i] == -1) {
	printf("shmget: not enough shared memory\n");
	exit(1);
      }
    }
    ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
    ext_mpi_call_mpi(PMPI_Bcast(&(shmemid_global[i]), 1, MPI_INT, i, shmem_comm_node));
    ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
    shmem_global[i] = (char *)shmat(shmemid_global[i], NULL, 0);
    if (shmem_global[i] == (void *) -1) {
      printf("error shmat\n");
      exit(1);
    }
    if (shmem_global[i] == NULL)
      goto error;
    ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
  }
  shmem_offsets = (long int *)malloc(my_cores_per_node * sizeof(long int));
  ext_mpi_call_mpi(PMPI_Allgather(&shmem_global[mpi_rank_node_global], 1, MPI_LONG, shmem_offsets, 1, MPI_LONG, shmem_comm_node));
  for (i = 0; i < my_cores_per_node; i++) {
    shmem_offsets[i] = shmem_global[i] - (char*)(shmem_offsets[i]);
  }
  sizes_shared_global = (int*)malloc(my_cores_per_node * sizeof(int));
  ext_mpi_call_mpi(PMPI_Allgather(&size_shared, 1, MPI_INT, sizes_shared_global, 1, MPI_INT, shmem_comm_node));
  memset((void *)(shmem_global[mpi_rank_node_global]), 0, size_shared);
  ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
  ext_mpi_call_mpi(PMPI_Comm_free(&shmem_comm_node));
  return shmem_global[mpi_rank_node_global];
error:
  ext_mpi_call_mpi(PMPI_Comm_free(&shmem_comm_node));
  return NULL;
}

int ext_mpi_done_shared_memory(MPI_Comm comm_world) {
  int my_cores_per_node, my_mpi_size, my_mpi_rank, i;
  MPI_Comm comm_node;
  my_cores_per_node = ext_mpi_get_num_tasks_per_socket(comm_world, 1);
  if (my_cores_per_node <= 1) {
    return -1;
  }
  ext_mpi_call_mpi(PMPI_Comm_size(comm_world, &my_mpi_size));
  ext_mpi_call_mpi(PMPI_Comm_rank(comm_world, &my_mpi_rank));
  ext_mpi_call_mpi(PMPI_Comm_split(comm_world, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &comm_node));
  for (i = 0; i < my_cores_per_node; i++) {
    if (shmemid_global[i] == -2) {
      free(shmem_global[i]);
    } else {
      ext_mpi_call_mpi(PMPI_Barrier(comm_world));
      shmdt((void *)(shmem_global[i]));
      ext_mpi_call_mpi(PMPI_Barrier(comm_world));
      if (shmemid_global[i] != -1) {
        shmctl(shmemid_global[i], IPC_RMID, NULL);
      }
      ext_mpi_call_mpi(PMPI_Barrier(comm_world));
    }
  }
  ext_mpi_call_mpi(PMPI_Comm_free(&comm_node));
  free(shmemid_global);
  free(shmem_global);
  free(sizes_shared_global);
  return 0;
}

int ext_mpi_setup_shared_memory(MPI_Comm comm_row, int my_cores_per_node_row, int num_sockets_per_node,
                                int size_shared, int initialize, int **sizes_shared, int **shmemid, char ***shmem) {
  int my_mpi_rank_row, my_mpi_size_row, shmemid_temp, single_task, size_shared_temp, *ranks_global, i, j, k;
  char *shmem_temp;
  long int offset, *offsets;
  MPI_Comm shmem_comm_node;
  single_task = my_cores_per_node_row == 1 && num_sockets_per_node == 1;
  ext_mpi_call_mpi(MPI_Comm_size(comm_row, &my_mpi_size_row));
  ext_mpi_call_mpi(MPI_Comm_rank(comm_row, &my_mpi_rank_row));
  if (size_shared > 0) {
    size_shared = (((size_shared - 1) / CACHE_LINE_SIZE) + 1) * CACHE_LINE_SIZE;
  }
  ext_mpi_call_mpi(PMPI_Comm_split(comm_row, my_mpi_rank_row / (my_cores_per_node_row * num_sockets_per_node),
                  my_mpi_rank_row % (my_cores_per_node_row * num_sockets_per_node), &shmem_comm_node));
  *shmemid = (int *)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  memset(*shmemid, 0, my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  *shmem = (char **)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(char *));
  memset(*shmem, 0, my_cores_per_node_row * num_sockets_per_node * sizeof(char *));
  ranks_global = (int *)malloc(my_cores_per_node_row * num_sockets_per_node * sizeof(int));
  ext_mpi_call_mpi(PMPI_Allgather(&mpi_rank_node_global, 1, MPI_INT, ranks_global, 1, MPI_INT, shmem_comm_node));
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
    ext_mpi_call_mpi(PMPI_Allgather(&offset, 1, MPI_LONG, offsets, 1, MPI_LONG, shmem_comm_node));
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
  ext_mpi_call_mpi(PMPI_Allgather(&size_shared, 1, MPI_INT, *sizes_shared, 1, MPI_INT, shmem_comm_node));
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
  if (initialize) {
    memset((void *)((*shmem)[0]), 0, size_shared);
  }
  ext_mpi_call_mpi(PMPI_Barrier(shmem_comm_node));
  ext_mpi_call_mpi(PMPI_Comm_free(&shmem_comm_node));
  return 0;
//error:
//  PMPI_Comm_free(shmem_comm_node_row);
//  return ERROR_SHMEM;
}

int ext_mpi_destroy_shared_memory(int num_segments, int *ranks_node, int *sizes_shared, int *shmemid, char **shmem) {
  MPI_Comm newcomm;
  MPI_Group world_group, group;
  int *ranks_node_l, i;
  if (num_segments > 1 && ranks_node) {
    ranks_node_l = (int*)malloc(num_segments * sizeof(int));
    for (i = 0; i < num_segments; i++) {
      ranks_node_l[i] = ranks_node[i];
    }
    ext_mpi_call_mpi(PMPI_Comm_group(ext_mpi_COMM_WORLD_dup, &world_group));
    ext_mpi_call_mpi(PMPI_Group_incl(world_group, num_segments, ranks_node_l, &group));
    ext_mpi_call_mpi(PMPI_Comm_create_group(ext_mpi_COMM_WORLD_dup, group, 0, &newcomm));
    ext_mpi_call_mpi(PMPI_Barrier(newcomm));
    ext_mpi_call_mpi(PMPI_Comm_free(&newcomm));
    ext_mpi_call_mpi(PMPI_Group_free(&group));
    ext_mpi_call_mpi(PMPI_Group_free(&world_group));
    free(ranks_node_l);
  }
  for (i = 0; i < num_segments; i++) {
    if (shmemid[i] == -2) {
      free(shmem[i]);
    } else if (shmemid[i] == -1) {
      dfree(shmem[i]);
    }
  }
  free(shmemid);
  free(shmem);
  free(sizes_shared);
  return 0;
}
