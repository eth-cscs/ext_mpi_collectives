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

#define MAX_MESSAGE_SIZE 80000000
#define MPI_DATA_TYPE MPI_LONG

int main(int argc, char *argv[]) {
  int i, numprocs, rank, size, flag, type_size, bufsize, iterations, *counts, *displs, num_cores;
  double latency_blocking = 0.0, latency_persistent = 0.0;
  double t_start = 0.0, t_stop = 0.0;
  double timer_blocking = 0.0, timer_persistent = 0.0;
  double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
  void *sendbuf, *recvbuf;
#ifdef GPU_ENABLED
  void *sendbuf_device, *recvbuf_device;
#endif
  MPI_Comm comm_node;
  MPI_Request request;
  MPI_Info info;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Type_size(MPI_DATA_TYPE, &type_size);

  bufsize = MAX_MESSAGE_SIZE;

  sendbuf = malloc(bufsize);
  recvbuf = malloc(bufsize);
  counts = (int*)malloc(numprocs*sizeof(int));
  displs = (int*)malloc(numprocs*sizeof(int));

#ifdef GPU_ENABLED
  cudaMalloc(&sendbuf_device, bufsize);
  cudaMalloc(&recvbuf_device, bufsize);
#endif

  MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, rank, MPI_INFO_NULL, &comm_node);
  MPI_Comm_size(comm_node, &num_cores);
  MPI_Comm_free(&comm_node);

  if (numprocs != 128 || numprocs != num_cores) {
    if (rank == 0) {
      printf("error: requires 64 MPI tasks on one node\n");
    }
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (rank == 0) {
    printf("# message_size avg_time_blocking min_time_blocking max_time_blocking avg_time_persistent min_time_persistent max_time_persistent\n");
  }

  for (size = 1; size <= MAX_MESSAGE_SIZE / type_size; size *= 2) {
    for (i = 0; i < numprocs; i++) {
      counts[i] = size;
    }
    displs[0] = 0;
    for (i = 0; i < numprocs-1; i++) {
      displs[i+1] = displs[i] + counts[i];
    }
    MPI_Info_create(&info);
    if (size * type_size < 2048) {
      MPI_Info_set(info, "ext_mpi_copyin", "6;-64 -16 -8 8 16");
    } else if (size * type_size < 524288) {
      MPI_Info_set(info, "ext_mpi_copyin", "6;1 -2 -2 -2 -2 -2 -2 -2 2 2 2 2 2 2 2");
    } else {
      MPI_Info_set(info, "ext_mpi_copyin", "6;1 -128 128");
    }
    MPI_Allreduce_init(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                       MPI_COMM_WORLD, info, &request);
    MPI_Info_free(&info);
    MPI_Barrier(MPI_COMM_WORLD);
    iterations = 1;
    flag = 1;
    while (flag) {
      timer_blocking = 0.0;
      timer_persistent = 0.0;
      for (i = 0; i < iterations; i++) {
        t_start = MPI_Wtime();
#ifdef GPU_ENABLED
        cudaMemcpy(sendbuf, sendbuf_device, size * type_size, cudaMemcpyDeviceToHost);
#endif
        MPI_Allreduce(sendbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM, MPI_COMM_WORLD);
#ifdef GPU_ENABLED
        cudaMemcpy(recvbuf_device, recvbuf, size * type_size, cudaMemcpyHostToDevice);
#endif
        t_stop = MPI_Wtime();
        timer_blocking += t_stop - t_start;

        MPI_Barrier(MPI_COMM_WORLD);

        t_start = MPI_Wtime();
#ifdef GPU_ENABLED
        cudaMemcpy(sendbuf, sendbuf_device, size * type_size, cudaMemcpyDeviceToHost);
#endif
        MPI_Start(&request);
        MPI_Wait(&request, MPI_STATUS_IGNORE);
#ifdef GPU_ENABLED
        cudaMemcpy(recvbuf_device, recvbuf, size * type_size, cudaMemcpyHostToDevice);
#endif
        t_stop = MPI_Wtime();
        timer_persistent += t_stop - t_start;

        MPI_Barrier(MPI_COMM_WORLD);
      }
      flag = (timer_blocking < 2e0) && (timer_persistent < 2e0);
      MPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
      MPI_Barrier(MPI_COMM_WORLD);
      iterations *= 2;
    }
    iterations /= 2;
    latency_blocking = (double)(timer_blocking * 1e6) / iterations;
    latency_persistent = (double)(timer_persistent * 1e6) / iterations;

    MPI_Request_free(&request);
    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Reduce(&latency_blocking, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(&latency_blocking, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&latency_blocking, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    avg_time = avg_time / numprocs;

    if (rank == 0) {
      printf("# iterations %d\n", iterations);
      printf("%d ", size * type_size);
      printf("%e %e %e ", avg_time, min_time, max_time);
    }

    MPI_Reduce(&latency_persistent, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(&latency_persistent, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&latency_persistent, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    avg_time = avg_time / numprocs;

    if (rank == 0) {
      printf("%e %e %e\n", avg_time, min_time, max_time);
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }

  free(displs);
  free(counts);
#ifdef GPU_ENABLED
  cudaFree(sendbuf_device);
  cudaFree(recvbuf_device);
#endif
  free(sendbuf);
  free(recvbuf);

  MPI_Finalize();

  return 0;
}
