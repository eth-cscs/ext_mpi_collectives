#include <stdio.h>
#include <stdlib.h>
#ifdef __cplusplus
#include <hip/hip_runtime.h>
#else
#include <hip/hip_runtime_api.h>
#endif
#include "gpu_shmem.h"

int ext_mpi_gpu_sizeof_memhandle() { return (sizeof(struct hipIpcMemHandle_st)); }

int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    MPI_Comm comm_column, int my_cores_per_node_column,
                                    int size_shared, int num_segments,
                                    char ***shmem_gpu) {
  struct hipIpcMemHandle_st *shmemid_gpu;
  MPI_Comm my_comm_node_h, my_comm_node_v;
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column, my_mpi_size_column, i;
  MPI_Comm_size(comm, &my_mpi_size_row);
  MPI_Comm_rank(comm, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  MPI_Comm_split(comm, my_mpi_rank_row / my_cores_per_node_row,
                 my_mpi_rank_row % my_cores_per_node_row, &my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                   my_mpi_rank_column % my_cores_per_node_column,
                   &my_comm_node_v);
  }
  shmemid_gpu = (void *)malloc(num_segments * sizeof(struct hipIpcMemHandle_st));
  *shmem_gpu = (char **)malloc(num_segments * sizeof(char *));
  if ((shmemid_gpu == NULL) || (*shmem_gpu == NULL)) {
    exit(14);
  }
  for (i = 0; i < num_segments; i++) {
    if ((my_mpi_rank_row % my_cores_per_node_row == 0) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0)) {
      if (hipMalloc((void *)&((*shmem_gpu)[i]), size_shared) != 0)
        exit(16);
      if ((*shmem_gpu)[i] == NULL)
        exit(16);
      if (hipIpcGetMemHandle(&shmemid_gpu[i], (void *)((*shmem_gpu)[i])) != 0)
        exit(15);
    }
    MPI_Bcast(&(shmemid_gpu[i]), sizeof(struct hipIpcMemHandle_st), MPI_CHAR, 0,
              my_comm_node_h);
    MPI_Barrier(my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Bcast(&(shmemid_gpu[i]), sizeof(struct hipIpcMemHandle_st), MPI_CHAR, 0,
                my_comm_node_v);
      MPI_Barrier(my_comm_node_v);
    }
    if ((*shmem_gpu) == NULL) {
      if (hipIpcOpenMemHandle((void **)&((*shmem_gpu)[i]), shmemid_gpu[i],
                              hipIpcMemLazyEnablePeerAccess) != 0)
        exit(13);
    }
    if (shmem_gpu[i] == NULL)
      exit(2);
    MPI_Barrier(my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Barrier(my_comm_node_v);
      MPI_Barrier(my_comm_node_h);
    }
  }
  free(shmemid_gpu);
  MPI_Comm_free(&my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_free(&my_comm_node_v);
  }
  return 0;
}

int ext_mpi_gpu_destroy_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                      MPI_Comm comm_column, int my_cores_per_node_column,
                                      int num_segments, char **shmem_gpu) {
  int i;
  MPI_Comm my_comm_node_h, my_comm_node_v;
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column, my_mpi_size_column;
  MPI_Comm_size(comm, &my_mpi_size_row);
  MPI_Comm_rank(comm, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  MPI_Comm_split(comm, my_mpi_rank_row / my_cores_per_node_row,
                 my_mpi_rank_row % my_cores_per_node_row, &my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                   my_mpi_rank_column % my_cores_per_node_column,
                   &my_comm_node_v);
  }
  if ((*shmem_gpu) != NULL) {
    MPI_Comm_free(&my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Comm_free(&my_comm_node_v);
    }
    return 1;
  }
  for (i = 0; i < num_segments; i++) {
    if ((my_mpi_rank_row != 0) || (my_mpi_rank_column != 0)) {
      if (hipIpcCloseMemHandle((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
    MPI_Barrier(my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Barrier(my_comm_node_v);
      MPI_Barrier(my_comm_node_h);
    }
    if ((my_mpi_rank_row == 0) && (my_mpi_rank_column == 0)) {
      if (hipFree((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
    MPI_Barrier(my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Barrier(my_comm_node_v);
      MPI_Barrier(my_comm_node_h);
    }
  }
  free(shmem_gpu);
  MPI_Comm_free(&my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_free(&my_comm_node_v);
  }
  return 0;
}
