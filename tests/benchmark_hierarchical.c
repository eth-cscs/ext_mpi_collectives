#include "ext_mpi_interface.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_MESSAGE_SIZE 10000000
#define MPI_DATA_TYPE MPI_LONG
#define NUM_CORES 32 
#define NUM_SOCKETS 2
#define COLLECTIVE_TYPE 0

static void my_subcommunicators(MPI_Comm comm_all, MPI_Comm *comm_network, MPI_Comm *comm_node){
  int rank, numprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_split(comm_all, rank%NUM_CORES, rank/NUM_CORES, comm_network);
  MPI_Comm_split(comm_all, rank/NUM_CORES, rank%NUM_CORES, comm_node);
}

int main(int argc, char *argv[]) {
  int numprocs, rank, size, flag, type_size, bufsize, iterations, num_tasks, *counts, *displs, i, j;
  double latency_ref = 0.0;
  double latency = 0.0, t_start = 0.0, t_stop = 0.0;
  double timer_ref = 0.0;
  double timer = 0.0;
  double avg_time = 0.0, max_time = 0.0, min_time = 0.0;
  void *sendbuf, *recvbuf, *tempbuf;
  MPI_Comm comm_new, comm_network, comm_node;
  MPI_Request requests[3] = {MPI_REQUEST_NULL};

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Type_size(MPI_DATA_TYPE, &type_size);

  bufsize = type_size * MAX_MESSAGE_SIZE * numprocs;

  sendbuf = malloc(bufsize);
  recvbuf = malloc(bufsize);
  tempbuf = malloc(bufsize);
  counts = (int*)malloc(numprocs*sizeof(int));
  displs = (int*)malloc(numprocs*sizeof(int));

  if (rank == 0) {
    printf("# num_tasks message_size avg_time_ref min_time_ref max_time_ref avg_time min_time max_time\n");
  }

  for (num_tasks = numprocs; num_tasks > 0; num_tasks -= NUM_CORES) {
    if (rank < num_tasks) {
      MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &comm_new);
      my_subcommunicators(comm_new, &comm_network, &comm_node);
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
          MPI_Reduce_init(sendbuf, tempbuf, size, MPI_DATA_TYPE, MPI_SUM, 0,
                          comm_node, MPI_INFO_NULL, &requests[0]);
          if (rank%NUM_CORES == 0) {
            MPI_Allreduce_init(tempbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                               comm_network, MPI_INFO_NULL, &requests[1]);
          }
          MPI_Bcast_init(recvbuf, size, MPI_DATA_TYPE, 0,
                         comm_node, MPI_INFO_NULL, &requests[2]);
          break;
          case 1:
          MPI_Reduce_scatter_init(sendbuf, recvbuf, counts, MPI_DATA_TYPE, MPI_SUM,
                                  comm_network, MPI_INFO_NULL, &requests[1]);
          break;
          case 3:
          MPI_Allgatherv_init(sendbuf, size, MPI_DATA_TYPE, recvbuf, counts, displs, MPI_DATA_TYPE, comm_network, MPI_INFO_NULL, &requests[1]);
          break;
        }
        MPI_Barrier(comm_new);
        iterations = 1;
        flag = 1;
        while (flag) {
          timer_ref = 0.0;
          timer = 0.0;
          for (i = 0; i < iterations; i++) {
            t_start = MPI_Wtime();
            switch (COLLECTIVE_TYPE){
              case 0:
              MPI_Reduce(sendbuf, tempbuf, size, MPI_DATA_TYPE, MPI_SUM, 0,
                         comm_node);
              if (rank%NUM_CORES == 0) {
                MPI_Allreduce(tempbuf, recvbuf, size, MPI_DATA_TYPE, MPI_SUM,
                              comm_network);
              }
              MPI_Bcast(recvbuf, size, MPI_DATA_TYPE, 0,
                        comm_node);
              break;
              case 1:
              MPI_Reduce_scatter(sendbuf, recvbuf, counts, MPI_DATA_TYPE, MPI_SUM,
                                 comm_network);
              break;
              case 2:
              MPI_Allgatherv(sendbuf, size, MPI_DATA_TYPE, recvbuf, counts, displs, MPI_DATA_TYPE, comm_network);
              break;
            }
            t_stop = MPI_Wtime();
            timer_ref += t_stop - t_start;

            MPI_Barrier(comm_new);

            t_start = MPI_Wtime();
            for (j = 0; j < 3; j++) {
              MPI_Start(&requests[j]);
              MPI_Wait(&requests[j], MPI_STATUS_IGNORE);
            }
            t_stop = MPI_Wtime();
            timer += t_stop - t_start;

            MPI_Barrier(comm_new);
          }
          flag = (timer_ref < 2e0) && (timer < 2e0);
          MPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MIN, comm_new);
          MPI_Barrier(comm_new);
          iterations *= 2;
        }
        iterations /= 2;
        latency_ref = (double)(timer_ref * 1e6) / iterations;
        latency = (double)(timer * 1e6) / iterations;

        for (j = 0; j < 3; j++) {
          MPI_Request_free(&requests[i]);
        }
        MPI_Barrier(comm_new);

        MPI_Reduce(&latency_ref, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0,
                   comm_new);
        MPI_Reduce(&latency_ref, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0,
                   comm_new);
        MPI_Reduce(&latency_ref, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0,
                   comm_new);
        avg_time = avg_time / num_tasks;

        if (rank == 0) {
          printf("# iterations %d\n", iterations);
          printf("%d ", num_tasks);
          printf("%d ", size * type_size);
          printf("%e %e %e ", avg_time, min_time, max_time);
        }

        MPI_Reduce(&latency, &min_time, 1, MPI_DOUBLE, MPI_MIN, 0, comm_new);
        MPI_Reduce(&latency, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, comm_new);
        MPI_Reduce(&latency, &avg_time, 1, MPI_DOUBLE, MPI_SUM, 0, comm_new);
        avg_time = avg_time / num_tasks;

        if (rank == 0) {
          printf("%e %e %e\n", avg_time, min_time, max_time);
        }
        MPI_Barrier(comm_new);
      }
    } else {
      MPI_Comm_split(MPI_COMM_WORLD, 1, rank - num_tasks, &comm_new);
    }
    MPI_Comm_free(&comm_network);
    MPI_Comm_free(&comm_node);
    MPI_Comm_free(&comm_new);
  }

  free(displs);
  free(counts);
  free(tempbuf);
  free(sendbuf);
  free(recvbuf);

  MPI_Finalize();

  return 0;
}
