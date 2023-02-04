#include "cost_copyin_measurement.h"
#include "ext_mpi_native.h"
#include "prime_factors.h"
#include "read_write.h"
#ifdef GPU_ENABLED
#include "gpu_core.h"
#endif
#include <stdio.h>

extern int ext_mpi_verbose;

static double execution_time(const void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
	              	     MPI_Op op, MPI_Comm comm_row_, int my_cores_per_node_row, MPI_Comm comm_column_,
			     int my_cores_per_node_column, int copyin_method, int *copyin_factors, int num_sockets_per_node) {
  double time;
  int num_ports_[2], groups_[2], handle, mpi_rank_row, i;
  num_ports_[0] = -1;
  num_ports_[1] = 0;
  groups_[0] = -1;
  groups_[1] = 0;
  MPI_Comm_rank(comm_row_, &mpi_rank_row);
  handle = EXT_MPI_Allreduce_init_native(sendbuf, recvbuf, count, datatype, op,
    comm_row_, my_cores_per_node_row, comm_column_, my_cores_per_node_column, num_ports_,
    groups_, 1, copyin_method, copyin_factors, 1, 0, 0, 0, 0, num_sockets_per_node);
  time = MPI_Wtime();
  for (i = 0; i < 5000; i++) {
    EXT_MPI_Start_native(handle);
    EXT_MPI_Wait_native(handle);
  }
  time = MPI_Wtime() - time;
  EXT_MPI_Done_native(handle);
  if (mpi_rank_row == 0) {
    MPI_Reduce(MPI_IN_PLACE, &time, 1, MPI_DOUBLE, MPI_MAX, 0, comm_row_);
  } else {
    MPI_Reduce(&time, &time, 1, MPI_DOUBLE, MPI_MAX, 0, comm_row_);
  }
  return time;
}

int EXT_MPI_Allreduce_measurement(const void *sendbuf, void *recvbuf, int count,
                                  MPI_Datatype datatype, MPI_Op op,
                                  MPI_Comm comm_row, int *my_cores_per_node_row,
                                  MPI_Comm comm_column, int my_cores_per_node_column,
                                  int num_active_ports, int *copyin_method,
                                  int *copyin_factors, int *num_sockets_per_node) {
  int factors_max, factors[*my_cores_per_node_row+1], mpi_rank_row, mpi_rank_column, mpi_size_row, i, j, k, copyin_factors_min[*my_cores_per_node_row+1], msize, on_gpu = 0;
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
  if (msize <= CACHE_LINE_SIZE - OFFSET_FAST && !on_gpu && mpi_size_row == *my_cores_per_node_row) {
    *copyin_method = 0;
    factors_max = 0;
    copyin_factors_min[factors_max] = copyin_factors[factors_max] = 1; factors_max++;
    for (j = 1; j < *my_cores_per_node_row; j *= 2){
      copyin_factors_min[factors_max] = copyin_factors[factors_max] = 2; factors_max++;
    }
    copyin_factors_min[factors_max] = copyin_factors[factors_max] = 0;
  } else {
    MPI_Comm_split(comm_row, mpi_rank_row/(*my_cores_per_node_row*(*num_sockets_per_node)), mpi_rank_row%(*my_cores_per_node_row*(*num_sockets_per_node)), &comm_row_);
    if (comm_column != MPI_COMM_NULL) {
      MPI_Comm_rank(comm_column, &mpi_rank_column);
      MPI_Comm_split(comm_column, mpi_rank_column/my_cores_per_node_column, mpi_rank_column%my_cores_per_node_column, &comm_column_);
    } else {
      comm_column_ = MPI_COMM_NULL;
    }
    for (i = 1; i < *my_cores_per_node_row; i *= 2)
      ;
    if (i != *my_cores_per_node_row) {
      *copyin_method = 1;
      factors_max = 0;
      copyin_factors_min[factors_max] = copyin_factors[factors_max] = 1; factors_max++;
      for (j = 1; j < *my_cores_per_node_row; j *= 2){
        copyin_factors_min[factors_max] = copyin_factors[factors_max] = 2; factors_max++;
      }
      copyin_factors_min[factors_max] = copyin_factors[factors_max] = 0;
      time_min = execution_time(sendbuf, recvbuf, count, datatype, op, comm_row_, *my_cores_per_node_row, comm_column_, my_cores_per_node_column, *copyin_method, copyin_factors, *num_sockets_per_node);
    }
    factors_max = ext_mpi_plain_prime_factors(*my_cores_per_node_row, factors);
    for (i = 0; i <= factors_max; i++){
      copyin_factors[0] = 1;
      for (j = 0; j < i; j++){
        copyin_factors[0] *= factors[j];
      }
      for (k = 1; j < factors_max; j++){
        copyin_factors[k++] = factors[j];
      }
      copyin_factors[k] = 0;
      time = execution_time(sendbuf, recvbuf, count, datatype, op, comm_row_, *my_cores_per_node_row, comm_column_, my_cores_per_node_column, 0, copyin_factors, *num_sockets_per_node);
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
    if (comm_column != MPI_COMM_NULL) {
      MPI_Comm_free(&comm_column_);
    }
    MPI_Comm_free(&comm_row_);
  }
  MPI_Bcast(copyin_method, 1, MPI_INT, 0, comm_row);
  MPI_Bcast(copyin_factors_min, *my_cores_per_node_row + 1, MPI_INT, 0, comm_row);
  if (mpi_rank_row == 0 && ext_mpi_verbose) {
    printf("# EXT_MPI copyin %d;", *copyin_method);
  }
  for (j = 0; copyin_factors_min[j]; j++) {
    copyin_factors[j] = copyin_factors_min[j];
    if (mpi_rank_row == 0 && ext_mpi_verbose) {
      printf(" %d", copyin_factors[j]);
    }
  }
  copyin_factors[j] = 0;
  if (mpi_rank_row == 0 && ext_mpi_verbose) {
    printf("\n");
  }
  return 0;
}
