#include "ext_mpi_interface.h"
#include "ext_mpi.h"
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
#define NUM_CORES 1200
#define COLLECTIVE_TYPE 0

int main(int argc, char *argv[]) {
  int numprocs, rank, size, flag, type_size, bufsize, iterations, num_tasks, in_place = 0, i;
  double latency_ref = 0.0;
  double latency = 0.0, t_start = 0.0, t_stop = 0.0;
  double timer_ref = 0.0;
  double timer = 0.0;
  double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
  void *sendbuf, *recvbuf, *recvbuf_ref;
//#ifdef GPU_ENABLED
//  void *sendbuf_device, *recvbuf_device;
//#endif
  MPI_Comm new_comm;

  if (argc == 2 && argv[1][0] == 'i') {
    in_place = 1;
  }

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Type_size(MPI_DATA_TYPE, &type_size);

  bufsize = type_size * MAX_MESSAGE_SIZE * numprocs;
  bufsize = 100*1024*1024;

  if (in_place) {
    sendbuf = MPI_IN_PLACE;
  } else {
#ifndef GPU_ENABLED
    sendbuf = malloc(bufsize);
#else
    cudaMalloc(&sendbuf, bufsize);
#endif
  }
#ifndef GPU_ENABLED
  recvbuf = malloc(bufsize);
  recvbuf_ref = malloc(bufsize);
#else
  cudaMalloc(&recvbuf, bufsize);
  cudaMalloc(&recvbuf_ref, bufsize);
#endif

//#ifdef GPU_ENABLED
//  cudaMalloc(&sendbuf_device, bufsize);
//  cudaMalloc(&recvbuf_device, bufsize);
//#endif

  if (rank == 0) {
    printf("# num_tasks message_size avg_time_ref min_time_ref max_time_ref avg_time min_time max_time\n");
  }

  for (num_tasks = numprocs; num_tasks > 0; num_tasks -= NUM_CORES) {
    if (rank < num_tasks) {
      MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &new_comm);
      for (size = 1; size <= MAX_MESSAGE_SIZE; size *= 2) {
        MPI_Barrier(new_comm);
        if (!in_place) {
          for (i = 0; i < size; i++) {
            ((long int*)sendbuf)[i] = rand();
          }
        } else {
          for (i = 0; i < size; i++) {
            ((long int*)recvbuf)[i] = ((long int*)recvbuf_ref)[i] = rand();
          }
        }
        PMPI_Allreduce(sendbuf, recvbuf_ref, size, MPI_DATA_TYPE, MPI_SUM, new_comm);
        MPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, new_comm);
        for (i = 0; i < size; i++) {
          if (((long int*)recvbuf)[i] != ((long int*)recvbuf_ref)[i]) {
            printf("error in recvbuf\n");
            exit(1);
          }
        }
        for (i = 0; i < 20; i++) {
          PMPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, new_comm);
          MPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, new_comm);
	}
        iterations = 1;
        flag = 1;
        while (flag) {
          timer_ref = 0.0;
          timer = 0.0;
          for (i = 0; i < iterations; i++) {
            t_start = MPI_Wtime();
            switch (COLLECTIVE_TYPE){
              case 0:
//#ifdef GPU_ENABLED
//          cudaMemcpy(sendbuf, sendbuf_device, size * type_size, cudaMemcpyDeviceToHost);
//#endif
              PMPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                             new_comm);
//#ifdef GPU_ENABLED
//          cudaMemcpy(recvbuf_device, recvbuf, size * type_size, cudaMemcpyHostToDevice);
//#endif
              break;
            }
            t_stop = MPI_Wtime();
            timer_ref += t_stop - t_start;

            MPI_Barrier(new_comm);

            t_start = MPI_Wtime();
//#ifdef GPU_ENABLED
//          cudaMemcpy(sendbuf, sendbuf_device, size * type_size, cudaMemcpyDeviceToHost);
//#endif
            MPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                          new_comm);
//#ifdef GPU_ENABLED
//          cudaMemcpy(recvbuf_device, recvbuf, size * type_size, cudaMemcpyHostToDevice);
//#endif
            t_stop = MPI_Wtime();
            timer += t_stop - t_start;

            MPI_Barrier(new_comm);
          }
          flag = (timer_ref < 2e0) && (timer < 2e0);
          PMPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MIN, new_comm);
          MPI_Barrier(new_comm);
          iterations *= 2;
        }
        iterations /= 2;
        latency_ref = (double)(timer_ref * 1e6) / iterations;
        latency = (double)(timer * 1e6) / iterations;

        MPI_Barrier(new_comm);

        PMPI_Reduce(&latency_ref, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0,
                   new_comm);
        PMPI_Reduce(&latency_ref, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
                   new_comm);
        PMPI_Reduce(&latency_ref, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   new_comm);
        avg_time = avg_time / num_tasks;

        if (rank == 0) {
          printf("# iterations %d\n", iterations);
          printf("%d ", num_tasks);
          printf("%d ", size * type_size);
          printf("%e %e %e ", avg_time, min_time, max_time);
        }

        PMPI_Reduce(&latency, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0, new_comm);
        PMPI_Reduce(&latency, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, new_comm);
        PMPI_Reduce(&latency, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0, new_comm);
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

//#ifdef GPU_ENABLED
//  cudaFree(sendbuf_device);
//  cudaFree(recvbuf_device);
//#endif
  if (!in_place) {
#ifndef GPU_ENABLED
    free(sendbuf);
#else
    cudaFree(sendbuf);
#endif
  }
#ifndef GPU_ENABLED
  free(recvbuf_ref);
  free(recvbuf);
#else
  cudaFree(recvbuf_ref);
  cudaFree(recvbuf);
#endif

  MPI_Finalize();

  return 0;
}
