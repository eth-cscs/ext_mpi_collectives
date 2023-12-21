#include "cost_copyin_measurement.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef GPU_ENABLED
#ifdef __cplusplus
#include <cuda_runtime.h>
#else
#include <cuda_runtime_api.h>
#endif
#endif

#define MAX_MESSAGE_SIZE 2097152/8
#define MPI_DATA_TYPE MPI_LONG

int main(int argc, char *argv[]) {
  int i, numprocs, rank, size, type_size, bufsize, num_tasks, num_tasks_, copyin_method, *copyin_factors = NULL, num_sockets_per_node;
  void *sendbuf, *recvbuf;
#ifdef GPU_ENABLED
  void *sendbuf_device, *recvbuf_device;
#endif
  MPI_Comm new_comm;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Type_size(MPI_DATA_TYPE, &type_size);

  bufsize = type_size * MAX_MESSAGE_SIZE;
//  bufsize = 1024*1024*1024;

  sendbuf = malloc(bufsize);
  recvbuf = malloc(bufsize);

#ifdef GPU_ENABLED
  cudaMalloc(&sendbuf_device, bufsize);
  cudaMalloc(&recvbuf_device, bufsize);
#endif

  for (num_tasks = numprocs; num_tasks > 0; num_tasks--) {
    if (rank < num_tasks) {
      MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &new_comm);
      for (size = 1; size <= MAX_MESSAGE_SIZE; size *= 8) {
        num_sockets_per_node = 1;
        num_tasks_ = num_tasks;
	free(copyin_factors);
        EXT_MPI_Allreduce_measurement(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, new_comm, &num_tasks_, MPI_COMM_NULL, 1, 12, &copyin_method, &copyin_factors, &num_sockets_per_node);
        MPI_Barrier(new_comm);
        if (rank == 0) {
          printf("%d %d %d", num_tasks, size * type_size, copyin_method);
          for (i = 0; copyin_factors[i]; i++) {
            printf(" %d", copyin_factors[i]);
          }
          printf("\n");
        }
        MPI_Barrier(new_comm);
      }
    } else {
      MPI_Comm_split(MPI_COMM_WORLD, 1, rank - num_tasks, &new_comm);
    }
    MPI_Comm_free(&new_comm);
  }

#ifdef GPU_ENABLED
  cudaFree(sendbuf_device);
  cudaFree(recvbuf_device);
#endif
  free(sendbuf);
  free(recvbuf);
  free(copyin_factors);

  MPI_Finalize();

  return 0;
}
