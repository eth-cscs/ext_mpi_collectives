#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GPU_ENABLED
#ifdef __cplusplus
#include <cuda_runtime.h>
#else
#include <cuda_runtime_api.h>
#endif
#endif

#define CORES_PER_NODE 12

int main(int argc, char **argv) {
  char *sendbuf, *recvbuf; // warn *tempbuf;
  int mpi_size, mpi_rank;
  int dest, source, sendcount, recvcount, cores, parallel, iterations, i, j, k,
      start, step_size, sendcount_array_max, sendcount_array[100000];
  double wtime, wtime_sum;
  MPI_Request request_array[2 * CORES_PER_NODE];
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);

  // Get the number of processes
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

  if (mpi_size < CORES_PER_NODE) {
    printf("wrong number of MPI ranks\n");
    exit(1);
  }

  // Get the rank of the process
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

  start = 50;
  iterations = 1000;
  sendcount_array_max = 0;
  for (i = 100; i < 10000; i += 100) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 10000; i < 100000; i += 10000) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 100000; i < 1000000; i += 100000) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 1000000; i < 10000000; i += 1000000) {
    sendcount_array[sendcount_array_max++] = i;
  }
  for (i = 10000000; i < 100000000; i += 10000000) {
    //        sendcount_array[sendcount_array_max++] = i;
  }

#ifdef GPU_ENABLED
  cudaMalloc((void**)&sendbuf, sendcount_array[sendcount_array_max - 1]);
  cudaMalloc((void**)&recvbuf, sendcount_array[sendcount_array_max - 1] * CORES_PER_NODE);
#else
  sendbuf = (char *)malloc(sendcount_array[sendcount_array_max - 1]);
  recvbuf = (char *)malloc(sendcount_array[sendcount_array_max - 1] * CORES_PER_NODE);
#endif
  parallel = 1;
  for (cores = 1; cores <= CORES_PER_NODE; cores++) {
    for (k = 0; k < sendcount_array_max; k++) {
      sendcount = sendcount_array[k];
      recvcount = sendcount;
      wtime_sum = 0e0;
      step_size = 1;
      for (i = 0; i < start; i++) {
        for (j = 0; j < cores; j++) {
          dest = (mpi_rank + step_size + j) % mpi_size;
          source = (2 * mpi_size + mpi_rank - step_size - j) % mpi_size;
          MPI_Irecv(recvbuf, recvcount + sendcount_array[sendcount_array_max - 1] * j, MPI_CHAR, source, 0, MPI_COMM_WORLD, &request_array[j * 2]);
	  MPI_Isend(sendbuf, sendcount, MPI_CHAR, dest, 0, MPI_COMM_WORLD, &request_array[j * 2 + 1]);
        }
	MPI_Waitall(2 * cores, request_array, MPI_STATUSES_IGNORE);
        step_size *= (cores + 1);
        if (step_size > mpi_size) {
          step_size = 1;
        }
      }
      for (i = 0; i < iterations; i++) {
        wtime = MPI_Wtime();
        for (j = 0; j < cores; j++) {
          dest = (mpi_rank + step_size + j) % mpi_size;
          source = (2 * mpi_size + mpi_rank - step_size - j) % mpi_size;
          MPI_Irecv(recvbuf, recvcount + sendcount_array[sendcount_array_max - 1] * j, MPI_CHAR, source, 0, MPI_COMM_WORLD, &request_array[j * 2]);
	  MPI_Isend(sendbuf, sendcount, MPI_CHAR, dest, 0, MPI_COMM_WORLD, &request_array[j * 2 + 1]);
        }
	MPI_Waitall(2 * cores, request_array, MPI_STATUSES_IGNORE);
        wtime = MPI_Wtime() - wtime;
        step_size *= (cores + 1);
        if (step_size > mpi_size) {
          step_size = 1;
        }
        wtime_sum += wtime;
      }
      if (mpi_rank == 0) {
        MPI_Reduce(MPI_IN_PLACE, &wtime_sum, 1, MPI_DOUBLE, MPI_MAX, 0,
                   MPI_COMM_WORLD);
        //                wtime_sum /= (mpi_size/CORES_PER_NODE*cores*parallel);
        wtime_sum /= iterations;
        printf("%d %d %d %d %e\n", mpi_size, cores, parallel, sendcount,
               wtime_sum);
      } else {
        MPI_Reduce(&wtime_sum, &wtime_sum, 1, MPI_DOUBLE, MPI_MAX, 0,
                   MPI_COMM_WORLD);
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }
  }

  // Finalize the MPI environment.
  MPI_Finalize();
}
