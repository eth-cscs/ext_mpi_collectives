#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
#include <cuda_runtime.h>
#else
#include <cuda_runtime_api.h>
#endif
#include "gpu_shmem.h"
#include "shmem.h"

int ext_mpi_gpu_sizeof_memhandle() { return (sizeof(struct cudaIpcMemHandle_st)); }

int ext_mpi_gpu_setup_shared_memory(MPI_Comm comm, int my_cores_per_node_row,
                                    MPI_Comm comm_column, int my_cores_per_node_column,
                                    int size_shared, int num_segments,
                                    int **shmemidi_gpu, char ***shmem_gpu) {
  struct cudaIpcMemHandle_st *shmemid_gpu;
  MPI_Comm my_comm_node_h, my_comm_node_v;
  int my_mpi_rank_row, my_mpi_size_row, my_mpi_rank_column, my_mpi_size_column, i, ii, j, flag;
  MPI_Comm_size(comm, &my_mpi_size_row);
  MPI_Comm_rank(comm, &my_mpi_rank_row);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_size(comm_column, &my_mpi_size_column);
    MPI_Comm_rank(comm_column, &my_mpi_rank_column);
  } else {
    my_mpi_size_column = 1;
    my_mpi_rank_column = 0;
  }
  PMPI_Comm_split(comm, my_mpi_rank_row / (my_cores_per_node_row * num_segments),
                  my_mpi_rank_row % (my_cores_per_node_row * num_segments), &my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Comm_split(comm_column, my_mpi_rank_column / my_cores_per_node_column,
                    my_mpi_rank_column % my_cores_per_node_column, &my_comm_node_v);
  } else {
    my_comm_node_v = MPI_COMM_NULL;
  }
  shmemid_gpu = (void *)malloc(num_segments * sizeof(struct cudaIpcMemHandle_st));
  *shmem_gpu = (char **)malloc(num_segments * sizeof(char *));
  *shmemidi_gpu = (int *)malloc(num_segments * sizeof(int));
  if ((shmemid_gpu == NULL) || (*shmem_gpu == NULL) || (shmemidi_gpu == NULL)) {
    exit(14);
  }
  memset(shmemid_gpu, 0, num_segments * sizeof(struct cudaIpcMemHandle_st));
  memset(*shmem_gpu, 0, num_segments * sizeof(char *));
  memset(*shmemidi_gpu, 0, num_segments * sizeof(int));
  for (i = 0; i < num_segments; i++) {
    ii = (num_segments - i + my_mpi_rank_row / my_cores_per_node_row) % num_segments;
    if ((my_mpi_rank_row % (my_cores_per_node_row * num_segments) == i * my_cores_per_node_row) &&
        (my_mpi_rank_column % my_cores_per_node_column == 0)) {
      if (!size_shared) {
	(*shmem_gpu)[ii] = NULL;
	(*shmemidi_gpu)[ii] = 1;
      } else {
        if (cudaMalloc((void *)&((*shmem_gpu)[ii]), size_shared) != 0)
          exit(16);
        if ((*shmem_gpu)[ii] == NULL)
          exit(16);
        if (cudaIpcGetMemHandle(&shmemid_gpu[ii], (void *)((*shmem_gpu)[ii])) != 0)
          exit(15);
        (*shmemidi_gpu)[ii] |= 1;
      }
    }
    MPI_Bcast(&(shmemid_gpu[ii]), sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, i * my_cores_per_node_row,
              my_comm_node_h);
    MPI_Barrier(my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Bcast(&(shmemid_gpu[ii]), sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, 0,
                my_comm_node_v);
      MPI_Barrier(my_comm_node_v);
    }
    flag = 0;
    for (j = 0; j < sizeof(struct cudaIpcMemHandle_st); j++) {
      if (((char *)(&(shmemid_gpu[ii])))[j]) flag = 1;
    }
    if (flag) {
      if ((*shmem_gpu)[ii] == NULL) {
        if (cudaIpcOpenMemHandle((void **)&((*shmem_gpu)[ii]), shmemid_gpu[ii],
                                 cudaIpcMemLazyEnablePeerAccess) != 0)
          exit(13);
        (*shmemidi_gpu)[ii] |= 2;
      }
      if ((*shmem_gpu)[ii] == NULL)
        exit(2);
    } else {
      (*shmem_gpu)[ii] = NULL;
      (*shmemidi_gpu)[ii] |= 2;
    }
    MPI_Barrier(my_comm_node_h);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Barrier(my_comm_node_v);
      MPI_Barrier(my_comm_node_h);
    }
  }
  free(shmemid_gpu);
  PMPI_Comm_free(&my_comm_node_h);
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Comm_free(&my_comm_node_v);
  }
  return 0;
}

int ext_mpi_gpu_destroy_shared_memory(int num_segments, int *shmemid_gpu, char **shmem_gpu, char *comm_code) {
  int i;
  for (i = 0; i < num_segments; i++) {
    ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
    if ((shmemid_gpu[i] & 2) && shmem_gpu[i]) {
      if (cudaIpcCloseMemHandle((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
    ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
    if ((shmemid_gpu[i] & 1) && shmem_gpu[i]) {
      if (cudaFree((void *)(shmem_gpu[i])) != 0) {
        exit(13);
      }
    }
    ext_mpi_node_barrier_mpi(MPI_COMM_NULL, MPI_COMM_NULL, comm_code);
  }
  free(shmem_gpu);
  free(shmemid_gpu);
  return 0;
}

int ext_mpi_sendrecvbuf_init_gpu(MPI_Comm comm, int my_cores_per_node, int num_sockets, char *sendrecvbuf, int size, char ***sendrecvbufs) {
  MPI_Comm gpu_comm_node;
  struct cudaIpcMemHandle_st shmemid_gpu;
  int my_mpi_rank, my_mpi_size, i, j, k;
  char *a;
  if (!sendrecvbuf) {
    *sendrecvbufs = NULL;
    return -1;
  }
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &gpu_comm_node);
  PMPI_Comm_rank(gpu_comm_node, &my_mpi_rank);
  PMPI_Comm_size(gpu_comm_node, &my_mpi_size);
  *sendrecvbufs = (char **)malloc(my_mpi_size * sizeof(char *));
  PMPI_Allgather(&sendrecvbuf, 1, MPI_LONG, *sendrecvbufs, 1, MPI_LONG, gpu_comm_node);
  PMPI_Allreduce(MPI_IN_PLACE, &size, 1, MPI_INT, MPI_MAX, gpu_comm_node);
  for (i = 0; i < my_mpi_size; i++) {
    if (i == my_mpi_rank) {
      if (cudaIpcGetMemHandle(&shmemid_gpu, (void *)sendrecvbuf) != 0) {
	printf("error cudaIpcGetMemHandle in cuda_shmem.c\n");
	exit(1);
      }
    }
    MPI_Bcast(&shmemid_gpu, sizeof(struct cudaIpcMemHandle_st), MPI_CHAR, i, gpu_comm_node);
    if (i == my_mpi_rank) {
      (*sendrecvbufs)[i] = sendrecvbuf;
    } else {
      if (cudaIpcOpenMemHandle((void **)&((*sendrecvbufs)[i]), shmemid_gpu, cudaIpcMemLazyEnablePeerAccess) != 0) {
	printf("error cudaIpcOpenMemHandle in cuda_shmem.c\n");
	exit(1);
      }
    }
  }
  for (j = 0; j < my_mpi_rank / (my_mpi_size / num_sockets) * (my_mpi_size / num_sockets); j++) {
    a = (*sendrecvbufs)[0];
    for (i = 0; i < my_mpi_size - 1; i++) {
      (*sendrecvbufs)[i] = (*sendrecvbufs)[i + 1];
    }
    (*sendrecvbufs)[my_mpi_size - 1] = a;
  }
  for (k = 0; k < num_sockets; k++) {
    for (j = 0; j < my_mpi_rank % (my_mpi_size / num_sockets); j++) {
      a = (*sendrecvbufs)[(my_mpi_size / num_sockets) * k];
      for (i = (my_mpi_size / num_sockets) * k; i < (my_mpi_size / num_sockets) * (k + 1) - 1; i++) {
	(*sendrecvbufs)[i] = (*sendrecvbufs)[i + 1];
      }
      (*sendrecvbufs)[(my_mpi_size / num_sockets) * (k + 1) - 1] = a;
    }
  }
  (*sendrecvbufs)[0] = sendrecvbuf;
  if (PMPI_Comm_free(&gpu_comm_node) != MPI_SUCCESS) {
    printf("error PMPI_Comm_free in cuda_shmem.c\n");
    exit(1);
  }
  return 0;
}

int ext_mpi_sendrecvbuf_done_gpu(MPI_Comm comm, int my_cores_per_node, char **sendrecvbufs) {
  MPI_Comm gpu_comm_node;
  int my_mpi_rank, my_mpi_size, i;
  char *addr;
  if (comm == MPI_COMM_NULL) {
    return -1;
  }
  PMPI_Comm_rank(comm, &my_mpi_rank);
  PMPI_Comm_size(comm, &my_mpi_size);
  PMPI_Comm_split(comm, my_mpi_rank / my_cores_per_node,
                  my_mpi_rank % my_cores_per_node, &gpu_comm_node);
  PMPI_Comm_rank(gpu_comm_node, &my_mpi_rank);
  PMPI_Comm_size(gpu_comm_node, &my_mpi_size);
  for (i = 1; i < my_mpi_size; i++) {
    addr = sendrecvbufs[i];
    if (cudaIpcCloseMemHandle((void *)(addr)) != 0) {
      printf("error cudaIpcCloseMemHandle in cuda_shmem.c\n");
      exit(1);
    }
  }
  free(sendrecvbufs);
  if (PMPI_Comm_free(&gpu_comm_node) != MPI_SUCCESS) {
    printf("error PMPI_Comm_free in cuda_shmem.c\n");
    exit(1);
  }
  return 0;
}
