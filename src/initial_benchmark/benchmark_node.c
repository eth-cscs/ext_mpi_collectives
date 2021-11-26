#include "mpi/ext_mpi_native.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 100000000

int main(int argc, char *argv[]) {
  int handle, mpi_size, mpi_rank, size, num_tasks;
  long int sendbuf[MAX_SIZE], recvbuf[MAX_SIZE];
  int num_ports[10], num_ports_limit[10], num_active_ports, copyin;
  int flag, i;
  double t_start, t_end, t_my, t_mpi;
  MPI_Comm new_comm;
  MPI_Init(NULL, NULL);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
  num_ports[0] = 1;
  num_ports[1] = 0;
  num_ports_limit[0] = 1;
  num_ports_limit[1] = 0;
  num_active_ports = 1;
  if (mpi_rank == 0) {
    printf("ext_mpi_node_size_threshold_max=%d;\n", mpi_size);
    printf("ext_mpi_node_size_threshold=(int *)malloc(%d*sizeof(int));\n", mpi_size);
  }
  for (num_tasks = mpi_size; num_tasks > 0; num_tasks--) {
    if (mpi_rank < num_tasks) {
      MPI_Comm_split(MPI_COMM_WORLD, 0, mpi_rank, &new_comm);
      flag = 1;
      size = 1;
      while ((size <= MAX_SIZE) && flag) {
        copyin = 0;
        handle = EXT_MPI_Allreduce_init_native(
            sendbuf, recvbuf, size, MPI_LONG, MPI_SUM, new_comm, num_tasks,
            MPI_COMM_NULL, 1, num_ports, num_ports_limit, num_active_ports,
            copyin, 0, 0);
        MPI_Barrier(new_comm);
        t_start = MPI_Wtime();
        for (i = 0; i < 100000; i++) {
          EXT_MPI_Start_native(handle);
          MPI_Barrier(new_comm);
        }
        t_end = MPI_Wtime();
        t_my = t_end - t_start;
        MPI_Allreduce(MPI_IN_PLACE, &t_my, 1, MPI_DOUBLE, MPI_MAX, new_comm);
        EXT_MPI_Done_native(handle);
        MPI_Barrier(new_comm);
        copyin = 1;
        handle = EXT_MPI_Allreduce_init_native(
            sendbuf, recvbuf, size, MPI_LONG, MPI_SUM, new_comm, num_tasks,
            MPI_COMM_NULL, 1, num_ports, num_ports_limit, num_active_ports,
            copyin, 0, 0);
        MPI_Barrier(new_comm);
        t_start = MPI_Wtime();
        for (i = 0; i < 100000; i++) {
          EXT_MPI_Start_native(handle);
          MPI_Barrier(new_comm);
        }
        t_end = MPI_Wtime();
        t_mpi = t_end - t_start;
        MPI_Allreduce(MPI_IN_PLACE, &t_mpi, 1, MPI_DOUBLE, MPI_MAX, new_comm);
        EXT_MPI_Done_native(handle);
        MPI_Barrier(new_comm);
        if (mpi_rank == 0) {
          flag = (t_my < t_mpi);
          if (!flag) {
            //            printf("%d %e %e\n", size, t_mpi, t_my);
            printf("ext_mpi_node_size_threshold[%d]=%d;\n", num_tasks - 1,
                   (size + size / 2) / 2);
          }
        }
        MPI_Bcast(&flag, 1, MPI_INT, 0, new_comm);
        size *= 2;
      }
    } else {
      MPI_Comm_split(MPI_COMM_WORLD, 1, mpi_rank - num_tasks, &new_comm);
    }
    MPI_Comm_free(&new_comm);
  }
  MPI_Finalize();
}
