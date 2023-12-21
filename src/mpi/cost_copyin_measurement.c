#include "cost_copyin_measurement.h"
#include "ext_mpi_native.h"
#include "prime_factors.h"
#include "read_write.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#endif
#include <stdlib.h>
#include <stdio.h>

extern int ext_mpi_verbose;

static double execution_time(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
	              	     MPI_Op op, MPI_Comm comm_row_, int my_cores_per_node_row, MPI_Comm comm_column_,
			     int my_cores_per_node_column, int copyin_method, int *copyin_factors, int num_sockets_per_node, MPI_Comm comm_row) {
  double time;
  int num_ports_[5], groups_[5], handle, mpi_rank_row, iterations, flag, i;
  if (num_sockets_per_node == 1) {
    num_ports_[0] = -1;
    num_ports_[1] = 0;
    groups_[0] = -1;
    groups_[1] = 0;
  } else if (num_sockets_per_node == 2) {
    num_ports_[0] = -1;
    num_ports_[1] = 1;
    num_ports_[2] = 0;
    groups_[0] = -2;
    groups_[1] = -2;
    groups_[2] = 0;
  } else if (num_sockets_per_node == 4) {
    num_ports_[0] = -1;
    num_ports_[1] = -1;
    num_ports_[2] = 1;
    num_ports_[3] = 1;
    num_ports_[4] = 0;
    groups_[0] = -2;
    groups_[1] = -2;
    groups_[2] = -2;
    groups_[3] = -2;
    groups_[4] = 0;
  }
  MPI_Comm_rank(comm_row, &mpi_rank_row);
  handle = EXT_MPI_Allreduce_init_native(sendbuf, recvbuf, count, datatype, op,
    comm_row_, my_cores_per_node_row, comm_column_, my_cores_per_node_column, num_ports_,
    groups_, copyin_method, copyin_factors, 1, 0, 0, 0, 0, num_sockets_per_node, 0, NULL, NULL);
  iterations = 10;
  flag = 1;
  while (flag) {
    iterations *= 2;
    time = MPI_Wtime();
    for (i = 0; i < iterations; i++) {
      EXT_MPI_Start_native(handle);
      EXT_MPI_Wait_native(handle);
    }
    time = MPI_Wtime() - time;
    flag = time < 0.1e0;
    PMPI_Allreduce(MPI_IN_PLACE, &flag, 1, MPI_INT, MPI_MIN, comm_row);
  }
  time /= iterations;
  EXT_MPI_Done_native(handle);
  if (mpi_rank_row == 0) {
    MPI_Reduce(MPI_IN_PLACE, &time, 1, MPI_DOUBLE, MPI_MAX, 0, comm_row);
  } else {
    MPI_Reduce(&time, &time, 1, MPI_DOUBLE, MPI_MAX, 0, comm_row);
  }
  return time;
}

static double allreduce_measurement(const void *sendbuf, void *recvbuf, int count,
                          MPI_Datatype datatype, MPI_Op op,
                          MPI_Comm comm_row, int my_cores_per_node_row,
                          MPI_Comm comm_column, int my_cores_per_node_column,
                          int num_active_ports, int *copyin_method,
                          int *copyin_factors, int num_sockets_per_node) {
  int factors_max, factors[my_cores_per_node_row+1], mpi_rank_row, mpi_rank_column, mpi_size_row, i, j, k, copyin_factors_min[my_cores_per_node_row+1], msize, on_gpu = 0;
  double time, time_min = 1e20;
  MPI_Comm comm_row_, comm_column_;
  MPI_Comm_size(comm_row, &mpi_size_row);
  MPI_Comm_rank(comm_row, &mpi_rank_row);
  MPI_Type_size(datatype, &msize);
  msize *= count;
  *copyin_method = -1;
#ifdef GPU_ENABLED
  on_gpu = ext_mpi_gpu_is_device_pointer(recvbuf);
#endif
  PMPI_Comm_split(comm_row, mpi_rank_row/(my_cores_per_node_row*num_sockets_per_node), mpi_rank_row%(my_cores_per_node_row*num_sockets_per_node), &comm_row_);
  if (comm_column != MPI_COMM_NULL) {
    MPI_Comm_rank(comm_column, &mpi_rank_column);
    PMPI_Comm_split(comm_column, mpi_rank_column/my_cores_per_node_column, mpi_rank_column%my_cores_per_node_column, &comm_column_);
  } else {
    comm_column_ = MPI_COMM_NULL;
  }
  if (msize <= CACHE_LINE_SIZE - OFFSET_FAST && !on_gpu && mpi_size_row == my_cores_per_node_row) {
    *copyin_method = 0;
    factors_max = 0;
    copyin_factors_min[factors_max] = copyin_factors[factors_max] = 1; factors_max++;
    for (j = 1; j < my_cores_per_node_row; j *= 2){
      copyin_factors_min[factors_max] = copyin_factors[factors_max] = 2; factors_max++;
    }
    copyin_factors_min[factors_max] = copyin_factors[factors_max] = 0;
    time_min = execution_time(sendbuf, recvbuf, count, datatype, op, comm_row_, my_cores_per_node_row, comm_column_, my_cores_per_node_column, *copyin_method, copyin_factors, num_sockets_per_node, comm_row);
  } else {
    for (i = 1; i < my_cores_per_node_row; i *= 2)
      ;
    if (i != my_cores_per_node_row) {
      *copyin_method = 1;
      factors_max = 0;
      copyin_factors_min[factors_max] = copyin_factors[factors_max] = 1; factors_max++;
      for (j = 1; j < my_cores_per_node_row; j *= 2){
        copyin_factors_min[factors_max] = copyin_factors[factors_max] = 2; factors_max++;
      }
      copyin_factors_min[factors_max] = copyin_factors[factors_max] = 0;
      time_min = execution_time(sendbuf, recvbuf, count, datatype, op, comm_row_, my_cores_per_node_row, comm_column_, my_cores_per_node_column, *copyin_method, copyin_factors, num_sockets_per_node, comm_row);
    }
    factors_max = ext_mpi_plain_prime_factors(my_cores_per_node_row, factors);
    for (i = 0; i <= factors_max; i++){
      copyin_factors[0] = 1;
      for (j = 0; j < i; j++){
        copyin_factors[0] *= factors[j];
      }
      for (k = 1; j < factors_max; j++){
        copyin_factors[k++] = factors[j];
      }
      copyin_factors[k] = 0;
      time = execution_time(sendbuf, recvbuf, count, datatype, op, comm_row_, my_cores_per_node_row, comm_column_, my_cores_per_node_column, 0, copyin_factors, num_sockets_per_node, comm_row);
      k = time < time_min;
      MPI_Bcast(&k, 1, MPI_INT, 0, comm_row_);
      if (k) {
        time_min = time;
        if (*copyin_method != 2) {
          *copyin_method = 0;
        }
        for (j = 0; copyin_factors[j]; j++) {
  	  copyin_factors_min[j] = copyin_factors[j];
        }
        copyin_factors_min[j] = 0;
      }
    }
  }
  if (comm_column != MPI_COMM_NULL) {
    PMPI_Comm_free(&comm_column_);
  }
  PMPI_Comm_free(&comm_row_);
  MPI_Bcast(copyin_method, 1, MPI_INT, 0, comm_row);
  MPI_Bcast(copyin_factors_min, my_cores_per_node_row + 1, MPI_INT, 0, comm_row);
  for (j = 0; copyin_factors_min[j]; j++) {
    copyin_factors[j] = copyin_factors_min[j];
  }
  copyin_factors[j] = 0;
  return time_min;
}

int EXT_MPI_Allreduce_measurement(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int *my_cores_per_node_row,
                                  MPI_Comm comm_column, int my_cores_per_node_column,
                                  int num_active_ports, int *copyin_method,
                                  int **copyin_factors, int *num_sockets_per_node) {
  int mpi_rank_row, mpi_size_row, i, j, k, copyin_factors_min[*my_cores_per_node_row+1], msize, copyin_method_min, num_sockets_per_node_min, num_sockets_per_node_max = 1, one_socket_only = 0;
  double time, time_min;
  if (*num_sockets_per_node == -1) {
    *num_sockets_per_node = -*num_sockets_per_node;
    one_socket_only = 1;
  }
  MPI_Comm_size(comm_row, &mpi_size_row);
  MPI_Comm_rank(comm_row, &mpi_rank_row);
  MPI_Type_size(datatype, &msize);
  (*copyin_factors) = (int*)malloc((mpi_size_row + 1) * sizeof(int));
  msize *= count;
  if (*my_cores_per_node_row * my_cores_per_node_column == 1) {
    *copyin_method = 0;
    (*copyin_factors)[0] = 1;
    (*copyin_factors)[1] = 0;
  } else {
    time_min = allreduce_measurement(sendbuf, recvbuf, count, datatype, op, comm_row, *my_cores_per_node_row, comm_column, my_cores_per_node_column, num_active_ports, &copyin_method_min, copyin_factors_min, *num_sockets_per_node);
    num_sockets_per_node_min = 1;
    if (*my_cores_per_node_row == mpi_size_row && !one_socket_only) num_sockets_per_node_max = 4;
    for (j = 2; j <= num_sockets_per_node_max; j *= 2){
      if (*num_sockets_per_node == 1 && *my_cores_per_node_row % j == 0) {
        time = allreduce_measurement(sendbuf, recvbuf, count, datatype, op, comm_row, *my_cores_per_node_row/j, comm_column, my_cores_per_node_column, num_active_ports, copyin_method, *copyin_factors, *num_sockets_per_node*j);
        k = time < time_min;
        MPI_Bcast(&k, 1, MPI_INT, 0, comm_row);
        if (k) {
          time_min = time;
          copyin_method_min = *copyin_method;
          for (i = 0; (*copyin_factors)[i]; i++) {
            copyin_factors_min[i] = (*copyin_factors)[i];
          }
          copyin_factors_min[i] = (*copyin_factors)[i];
          num_sockets_per_node_min = j;
        }
      }
    }
    MPI_Bcast(&num_sockets_per_node_min, 1, MPI_INT, 0, comm_row);
    *copyin_method = copyin_method_min;
    MPI_Bcast(copyin_method, 1, MPI_INT, 0, comm_row);
    MPI_Bcast(copyin_factors_min, *my_cores_per_node_row + 1, MPI_INT, 0, comm_row);
    if (mpi_rank_row == 0 && ext_mpi_verbose) {
/*      printf("# EXT_MPI copyin %d;", *copyin_method); */
    }
    for (j = 0; copyin_factors_min[j]; j++) {
      (*copyin_factors)[j] = copyin_factors_min[j];
      if (mpi_rank_row == 0 && ext_mpi_verbose) {
/*        printf(" %d", copyin_factors[j]); */
      }
    }
    (*copyin_factors)[j] = 0;
    if (mpi_rank_row == 0 && ext_mpi_verbose) {
/*      printf("\n"); */
    }
    if (*num_sockets_per_node == 1) {
      *num_sockets_per_node = num_sockets_per_node_min;
      *my_cores_per_node_row /= num_sockets_per_node_min;
    }
  }
  return 0;
}
