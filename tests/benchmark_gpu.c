#include "ext_mpi.h"
#include "ext_mpi_interface.h"
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

#define MAX_MESSAGE_SIZE 10000000
#define MPI_DATA_TYPE MPI_LONG
#define NUM_CORES 12
#define COLLECTIVE_TYPE 0

int main(int argc, char *argv[]) {
  int i, numprocs, rank, size, flag, type_size, bufsize, iterations, num_tasks, *counts, *displs;
  double latency_ref = 0.0;
  double latency = 0.0, t_start = 0.0, t_stop = 0.0;
  double timer_ref = 0.0;
  double timer = 0.0;
  double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
  void *sendbuf, *recvbuf;
  MPI_Comm new_comm;
  MPI_Request request;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Type_size(MPI_DATA_TYPE, &type_size);

  bufsize = type_size * MAX_MESSAGE_SIZE;

#ifdef GPU_ENABLED
  cudaMalloc(&sendbuf, bufsize);
  cudaMalloc(&recvbuf, bufsize);
#else
  sendbuf = malloc(bufsize);
  recvbuf = malloc(bufsize);
#endif
  counts = (int*)malloc(numprocs*sizeof(int));
  displs = (int*)malloc(numprocs*sizeof(int));

  if (rank == 0) {
    printf("# num_tasks message_size avg_time_ref min_time_ref max_time_ref avg_time min_time max_time\n");
  }

  ext_mpi_bit_reproducible = 1;
  for (num_tasks = numprocs; num_tasks > 0; num_tasks -= NUM_CORES) {
    if (rank < num_tasks) {
      MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &new_comm);
      for (size = 1; size <= MAX_MESSAGE_SIZE; size *= 2) {
        for (i = 0; i < numprocs; i++) {
          counts[i] = size;
        }
        displs[0] = 0;
        for (i = 0; i < numprocs-1; i++) {
          displs[i+1] = displs[i] + counts[i];
        }
        switch (COLLECTIVE_TYPE){
          case 0:
          MPI_Allreduce_init(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                             new_comm, MPI_INFO_NULL, &request);
          break;
          case 1:
          MPI_Reduce_init(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, 0,
                          new_comm, MPI_INFO_NULL, &request);
          break;
          case 2:
          MPI_Reduce_scatter_init(sendbuf, recvbuf, counts, MPI_DATA_TYPE, MPI_SUM,
                                  new_comm, MPI_INFO_NULL, &request);
          break;
          case 3:
          MPI_Allgatherv_init(sendbuf, size, MPI_DATA_TYPE, recvbuf, counts, displs, MPI_DATA_TYPE, new_comm, MPI_INFO_NULL, &request);
          break;
          case 4:
          MPI_Bcast_init(sendbuf, size, MPI_DATA_TYPE, 0, new_comm, MPI_INFO_NULL, &request);
          break;
          case 5:
          MPI_Gatherv_init(sendbuf, size, MPI_DATA_TYPE, recvbuf, counts, displs, MPI_DATA_TYPE, 0, new_comm, MPI_INFO_NULL, &request);
          break;
          case 6:
          MPI_Scatterv_init(sendbuf, counts, displs, MPI_DATA_TYPE, recvbuf, size, MPI_DATA_TYPE, 0, new_comm, MPI_INFO_NULL, &request);
          break;
        }
        MPI_Barrier(new_comm);
        iterations = 1;
        flag = 1;
        while (flag) {
          timer_ref = 0.0;
          timer = 0.0;
          for (i = 0; i < iterations; i++) {
            t_start = MPI_Wtime();
            switch (COLLECTIVE_TYPE){
              case 0:
              MPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                            new_comm);
              break;
              case 1:
              MPI_Reduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, 0,
                         new_comm);
              break;
              case 2:
              MPI_Reduce_scatter(sendbuf, recvbuf, counts, MPI_DATA_TYPE, MPI_SUM,
                                 new_comm);
              break;
              case 3:
              MPI_Allgatherv(sendbuf, size, MPI_DATA_TYPE, recvbuf, counts, displs, MPI_DATA_TYPE, new_comm);
              break;
              case 4:
              MPI_Bcast(sendbuf, size, MPI_DATA_TYPE, 0, new_comm);
              break;
              case 5:
              MPI_Gatherv(sendbuf, size, MPI_DATA_TYPE, recvbuf, counts, displs, MPI_DATA_TYPE, 0, new_comm);
              break;
              case 6:
              MPI_Scatterv(sendbuf, counts, displs, MPI_DATA_TYPE, recvbuf, size, MPI_DATA_TYPE, 0, new_comm);
              break;
            }
            t_stop = MPI_Wtime();
            timer_ref += t_stop - t_start;

            MPI_Barrier(new_comm);

            t_start = MPI_Wtime();
            MPI_Start(&request);
            MPI_Wait(&request, MPI_STATUS_IGNORE);
            t_stop = MPI_Wtime();
            timer += t_stop - t_start;

            MPI_Barrier(new_comm);
          }
          flag = (timer_ref < 2e0) && (timer < 2e0);
          MPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MIN, new_comm);
          MPI_Barrier(new_comm);
          iterations *= 2;
        }
        iterations /= 2;
        latency_ref = (double)(timer_ref * 1e6) / iterations;
        latency = (double)(timer * 1e6) / iterations;

        MPI_Request_free(&request);
        MPI_Barrier(new_comm);

        MPI_Reduce(&latency_ref, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0,
                   new_comm);
        MPI_Reduce(&latency_ref, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
                   new_comm);
        MPI_Reduce(&latency_ref, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   new_comm);
        avg_time = avg_time / num_tasks;

        if (rank == 0) {
          printf("# iterations %d\n", iterations);
          printf("%d ", num_tasks);
          printf("%d ", size * type_size);
          printf("%e %e %e ", avg_time, min_time, max_time);
        }

        MPI_Reduce(&latency, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0, new_comm);
        MPI_Reduce(&latency, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, new_comm);
        MPI_Reduce(&latency, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0, new_comm);
        avg_time = avg_time / num_tasks;

        if (rank == 0) {
          printf("%e %e %e\n", avg_time, min_time, max_time);
        }
        MPI_Barrier(new_comm);
      }
    } else {
      MPI_Comm_split(MPI_COMM_WORLD, 1, rank - num_tasks, &new_comm);
    }
    MPI_Comm_free(&new_comm);
  }

  free(displs);
  free(counts);
#ifdef GPU_ENABLED
  cudaFree(sendbuf);
  cudaFree(recvbuf);
#else
  free(sendbuf);
  free(recvbuf);
#endif

  MPI_Finalize();

  return 0;
}
